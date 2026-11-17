/** T3 的独立 mock 闭环；不是业务执行器，也不提供 PLC 写入口。 */
import { DatabaseSync } from 'node:sqlite';
import { randomUUID, createHash } from 'node:crypto';
import { mkdirSync, readFileSync, openSync, writeSync, fsyncSync, fstatSync, closeSync, unlinkSync } from 'node:fs';
import { join } from 'node:path';
import { ClockTracker, toMicroseconds as toUs } from './clock-tracker.mjs';

export class MockLoop {
  #db; #fd; #lockFd; #directory; #manifest; #rules; #tags; #revision;
  #boot=randomUUID(); #closed=false; #fault=null;
  #clock;
  constructor({directory,manifest,rules=[]}) {
    this.#directory=directory; this.#manifest=structuredClone(manifest); this.#rules=structuredClone(rules);
    this.#tags=new Map(manifest.groups.flatMap(g=>g.tags.map(t=>[t.id,{...t,group:g.id}])));
    const ids=new Set();
    for(const r of this.#rules) {
      if(!r.id || ids.has(r.id) || this.#tags.get(r.tag_id)?.type!=='number' || !['high','low'].includes(r.kind)
        || !Number.isFinite(r.trigger) || !Number.isFinite(r.clear)
        || (r.kind==='high'?r.clear>=r.trigger:r.clear<=r.trigger)) throw new TypeError('无效 mock 报警规则');
      ids.add(r.id);
    }
    this.#revision=createHash('sha256').update(JSON.stringify({manifest:this.#manifest,rules:this.#rules})).digest('hex');
    mkdirSync(directory,{recursive:true});
    try {
      this.#lockFd=openSync(join(directory,'runtime.lock'),'wx',0o600);
      this.#db=new DatabaseSync(join(directory,'runtime.sqlite'));
      if(!this.#db.prepare("SELECT name FROM sqlite_master WHERE name='schema_migrations'").get()) {
        this.#db.exec(readFileSync(new URL('../../db/001-infrastructure.sql',import.meta.url),'utf8'));
      }
      this.#db.exec('PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON; PRAGMA busy_timeout=5000;');
      if(this.#db.prepare('PRAGMA journal_mode').get().journal_mode!=='wal') throw new Error('WAL 未启用');
      let schemaVersion=this.#db.prepare('SELECT MAX(version) version FROM schema_migrations').get().version;
      if(schemaVersion===1) {
        this.#db.exec(readFileSync(new URL('../../db/002-clock-tracking.sql',import.meta.url),'utf8'));
        schemaVersion=2;
      }
      if(schemaVersion!==2) throw new Error('不支持的数据库版本');
      this.#clock=new ClockTracker({state:JSON.parse(this.#db.prepare('SELECT state_json FROM clock_tracker_state WHERE singleton=1').get().state_json)});
      const existing=this.#db.prepare('SELECT config_revision FROM devices WHERE device_id=?').get(this.#manifest.device_id);
      if(existing && existing.config_revision!==this.#revision) throw new Error('mock 数据目录已有不同配置，必须走受控变更或使用新测试目录');
      const now=Date.now()*1000;
      this.#run('INSERT OR IGNORE INTO runtime_identity VALUES (1,?,?)',randomUUID(),now);
      this.#run('INSERT INTO runtime_boots VALUES (?,?,NULL,0)',this.#boot,now);
      this.#run('INSERT OR IGNORE INTO config_revisions VALUES (?,?,?,?)',this.#revision,'mock-unpublished',JSON.stringify({manifest:this.#manifest,rules:this.#rules}),now);
      this.#seed();
      this.#run("INSERT OR IGNORE INTO retention_policies VALUES ('mock-no-rotation','{}',1,?)",now);
      this.#run("INSERT OR IGNORE INTO log_segments(segment_id,relative_path,state,policy_revision) VALUES ('mock-events','events.jsonl','active','mock-no-rotation')");
      this.#fd=openSync(join(directory,'events.jsonl'),'a+',0o600);
      const durable=this.#db.prepare("SELECT durable_bytes FROM log_segments WHERE segment_id='mock-events'").get().durable_bytes;
      if(fstatSync(this.#fd).size!==durable) throw new Error('日志尾部与已确认长度不符，需要 T4 恢复；禁止自动忽略');
      this.#exportPending();
    } catch(error) {
      if(this.#fd!==undefined) closeSync(this.#fd);
      if(this.#db) this.#db.close();
      this.#releaseLock();throw error;
    }
  }
  #releaseLock() {
    if(this.#lockFd===undefined) return;
    closeSync(this.#lockFd);this.#lockFd=undefined;
    unlinkSync(join(this.#directory,'runtime.lock'));
  }
  #run(sql,...args) { return this.#db.prepare(sql).run(...args); }
  #next() {
    const statement=this.#db.prepare("UPDATE sequence_counters SET high_water=high_water+1 WHERE name='record' RETURNING high_water");
    statement.setReadBigInts(true); return statement.get().high_water;
  }
  #seed() {
    const m=this.#manifest;
    this.#run("INSERT INTO devices VALUES (?,?,'mock','{}',1,?) ON CONFLICT(device_id) DO UPDATE SET config_revision=excluded.config_revision",m.device_id,'仅供测试的模拟设备',this.#revision);
    for(const group of m.groups) {
      this.#run('INSERT INTO scan_groups VALUES (?,?,?,?,10,1,?) ON CONFLICT(group_id) DO UPDATE SET config_revision=excluded.config_revision',group.id,m.device_id,group.interval_ms,group.interval_ms,this.#revision);
      for(const tag of group.tags) this.#run(`INSERT INTO tags VALUES (?,?,?,?,?,?,NULL,?,?,?,?,?,'ordinary',1,1,?)
        ON CONFLICT(tag_id) DO UPDATE SET config_revision=excluded.config_revision`,tag.id,m.device_id,group.id,tag.id,'{}',tag.type,
        tag.engineering_low??null,tag.engineering_high??null,tag.deadband.mode,tag.deadband.value,group.interval_ms*3,this.#revision);
    }
    for(const r of this.#rules) {
      this.#run('INSERT INTO alarm_rules VALUES (?,?,?,?,?,2,0,0,1) ON CONFLICT(rule_id) DO UPDATE SET config_revision=excluded.config_revision',r.id,r.tag_id,this.#revision,r.kind,JSON.stringify({trigger:r.trigger,clear:r.clear}));
      this.#run("INSERT OR IGNORE INTO alarm_rule_state(rule_id,evaluation,suppression) VALUES (?,'normal','none')",r.id);
    }
  }
  accept(sample) {
    if(this.#closed) throw new Error('mock 闭环已关闭');
    if(this.#fault) throw this.#fault;
    const tag=this.#tags.get(sample.tag_id);
    if(!tag || sample.device_id!==this.#manifest.device_id || sample.group_id!==tag.group || sample.value?.type!==tag.type) throw new TypeError('样本与订阅清单不匹配');
    const observed=toUs(sample.observed_at_ms), source=toUs(sample.source_at_ms), server=toUs(sample.server_at_ms), event=toUs(sample.event_at_ms);
    const stamp={observed_at:new Date(sample.observed_at_ms).toISOString(),boot_id:this.#boot,monotonic_ns:sample.monotonic_ns};
    const clock=this.#clock.preview({...sample,boot_id:this.#boot});
    const context={device:sample.device_id,tag:sample.tag_id,scan_cycle:sample.scan_cycle,session_id:sample.session_id};
    this.#db.exec('BEGIN IMMEDIATE');
    try {
      if(clock.reasons.length) {
        const clockSequence=this.#next();
        this.#run('INSERT INTO clock_events VALUES (?,?,?,?,?,?,?,?)',clockSequence,clock.epoch,this.#boot,
          BigInt(sample.monotonic_ns),observed,sample.tag_id,JSON.stringify(clock.reasons),JSON.stringify(clock.details));
        const record={stamp:{sequence:String(clockSequence),...stamp},level:'warn',event_code:'clock.discontinuity',
          message_zh:'时间映射不连续，已切换记录时间段',context,
          fields:{clock_epoch:clock.epoch,reasons:clock.reasons,details:clock.details}};
        this.#run('INSERT INTO log_outbox VALUES (?,?,?)',clockSequence,observed,JSON.stringify(record));
      }
      const sequence=this.#next();
      this.#run('INSERT INTO history_raw VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',sequence,sample.tag_id,this.#revision,this.#boot,
        sample.scan_cycle,sample.session_id,BigInt(sample.monotonic_ns),observed,source,server,event,sample.timestamp_basis,clock.epoch,
        JSON.stringify(sample.value),sample.quality.level,sample.quality.reason,sample.quality.native_code??null);
      const transitions=[];
      for(const r of this.#rules.filter(r=>r.tag_id===sample.tag_id)) {
        const active=this.#db.prepare("SELECT occurrence_id,revision FROM alarm_occurrences WHERE rule_id=? AND condition='active'").get(r.id);
        let transition=null, occurrence=active?.occurrence_id??null;
        if(sample.quality.level!=='good') transition='quality_unknown';
        else if(!active && (r.kind==='high'?sample.value.value>=r.trigger:sample.value.value<=r.trigger)) transition='activate';
        else if(active && (r.kind==='high'?sample.value.value<=r.clear:sample.value.value>=r.clear)) transition='clear';
        if(transition) {
          const alarmSequence=this.#next();
          if(transition==='activate') {
            occurrence=randomUUID();
            this.#run("INSERT INTO alarm_occurrences VALUES (?,?,?,1,'active','unacknowledged',?,?,NULL,NULL,NULL,NULL,NULL)",occurrence,r.id,this.#revision,alarmSequence,observed);
          } else if(transition==='clear') {
            this.#run("UPDATE alarm_occurrences SET condition='cleared',revision=revision+1,cleared_sequence=?,cleared_at_us=? WHERE occurrence_id=?",alarmSequence,observed,occurrence);
          }
          this.#run('INSERT INTO alarm_transitions VALUES (?,?,?,?,?,?,?,?,?)',alarmSequence,r.id,occurrence,this.#boot,BigInt(sample.monotonic_ns),observed,transition,null,JSON.stringify({sample_sequence:String(sequence)}));
          transitions.push(transition);
        }
        const evaluation=sample.quality.level!=='good'?'unknown':(transition==='activate'||(active&&transition!=='clear'))?'active':'normal';
        this.#run('UPDATE alarm_rule_state SET evaluation=?,last_sample_sequence=? WHERE rule_id=?',evaluation,sequence,r.id);
      }
      const logSequence=this.#next();
      const record={stamp:{sequence:String(logSequence),...stamp},level:sample.quality.level==='good'?'info':'warn',
        event_code:transitions.length?`alarm.${transitions[0]}`:'tag.changed',message_zh:'模拟点位变化已入库',
        context,
        fields:{sample_sequence:String(sequence),quality:sample.quality,transitions,config_revision:this.#revision,clock_epoch:clock.epoch}};
      this.#run('INSERT INTO log_outbox VALUES (?,?,?)',logSequence,observed,JSON.stringify(record));
      this.#run('UPDATE clock_tracker_state SET state_json=? WHERE singleton=1',JSON.stringify(clock.next));
      this.#db.exec('COMMIT');
    } catch(error) {this.#db.exec('ROLLBACK');throw error;}
    this.#clock.commit(clock);
    try {this.#exportPending();} catch(error) {this.#fault=error;throw error;}
  }
  #exportPending() {
    const statement=this.#db.prepare('SELECT sequence,payload_json FROM log_outbox ORDER BY sequence'); statement.setReadBigInts(true);
    for(const row of statement.all()) {
      const record=JSON.parse(row.payload_json); const bytes=Buffer.from(row.payload_json+'\n');
      const offset=fstatSync(this.#fd).size;
      for(let written=0;written<bytes.length;) {
        const n=writeSync(this.#fd,bytes,written,bytes.length-written);
        if(n<=0) throw new Error('JSONL 追加没有进展'); written+=n;
      }
      fsyncSync(this.#fd);
      this.#db.exec('BEGIN IMMEDIATE');
      try {
        this.#run('INSERT INTO log_index VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)',row.sequence,'mock-events',offset,bytes.length,
          toUs(Date.parse(record.stamp.observed_at)),record.stamp.boot_id,BigInt(record.stamp.monotonic_ns),record.level,record.event_code,
          record.context.device,record.context.tag,record.context.scan_cycle,record.context.session_id);
        this.#run('UPDATE log_segments SET first_sequence=COALESCE(first_sequence,?),last_sequence=?,durable_bytes=? WHERE segment_id=?',row.sequence,row.sequence,offset+bytes.length,'mock-events');
        this.#run('DELETE FROM log_outbox WHERE sequence=?',row.sequence);
        this.#db.exec('COMMIT');
      } catch(error) {this.#db.exec('ROLLBACK');throw error;}
    }
  }
  close() {
    if(this.#closed) return;
    try {
      if(this.#fault) throw this.#fault;
      this.#exportPending();
      this.#run('UPDATE runtime_boots SET ended_at_us=?,clean_shutdown=1 WHERE boot_id=?',Date.now()*1000,this.#boot);
      this.#db.exec('PRAGMA wal_checkpoint(TRUNCATE)');
    } finally {
      this.#closed=true;
      try {closeSync(this.#fd);this.#db.close();} finally {this.#releaseLock();}
    }
  }
}
