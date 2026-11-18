import { randomUUID,createHash } from 'node:crypto';
import { join } from 'node:path';
import { openSync,closeSync,readSync,fsyncSync,fstatSync,existsSync,lstatSync,unlinkSync,ftruncateSync,statfsSync } from 'node:fs';
import { writeAll,syncDirectory } from './file-durability.mjs';
import { coverageFor } from './log-coverage.mjs';
import { toMicroseconds } from './clock-tracker.mjs';

const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const bigAll=(db,sql,...args)=>{const q=db.prepare(sql);q.setReadBigInts(true);return q.all(...args);};
const size=n=>{const x=Number(n);if(!Number.isSafeInteger(x)||x<0)throw new Error('非法文件大小/偏移');return x;};
const defaults={segment_max_bytes:128*1024*1024,segment_max_age_ms:86400000,retention_ms:90*86400000,
  max_record_bytes:1024*1024,max_query_records:10000,coverage_max_gap_ms:5000,budget_bytes:128*1024**3,reserve_bytes:256*1024*1024};
const readBytes=(fd,offset,length)=>{
  const data=Buffer.alloc(length);let got=0;
  while(got<length){const n=readSync(fd,data,got,length-got,offset+got);if(!n)throw new Error('日志文件短于已确认索引');got+=n;}
  return data;
};

export class LogStore {
  #db;#directory;#boot;#policy;#revision;#fd;#active;#closed=false;#evidenceFault=null;#directorySync=true;
  constructor({db,directory,boot_id,policy={}}) {
    this.#db=db;this.#directory=directory;this.#boot=boot_id;this.#policy={...defaults,...policy};
    for(const [k,v]of Object.entries(this.#policy))if(!Object.hasOwn(defaults,k)||!Number.isSafeInteger(v)||v<=0)throw new TypeError(`无效日志策略：${k}`);
    this.#revision='log-'+hash(JSON.stringify(this.#policy));
    const high=bigAll(db,"SELECT high_water FROM sequence_counters WHERE name='record'")[0].high_water;
    this.#run('INSERT OR IGNORE INTO retention_policies VALUES (?,?,?,?)',this.#revision,JSON.stringify(this.#policy),high+1n,Date.now()*1000);
    try {
      for(const row of bigAll(db,"SELECT * FROM log_segments WHERE state='deleting'"))this.#finishDelete(row);
      for(const row of bigAll(db,"SELECT * FROM log_segments WHERE state IN ('active','sealed') ORDER BY relative_path"))this.#recover(row);
      this.#active=bigAll(db,"SELECT * FROM log_segments WHERE state='active'")[0];
      if(!this.#active)this.#createSegment();else this.#openActive();
      if(this.#active.policy_revision!==this.#revision)this.#rotate();
      this.flush();
    }catch(error){if(this.#fd!==undefined)closeSync(this.#fd);this.#fd=undefined;throw error;}
  }
  #run(sql,...args){return this.#db.prepare(sql).run(...args);}
  #syncDirectory(){this.#directorySync=syncDirectory(this.#directory)&&this.#directorySync;}
  #path(relative){return segmentPath(this.#directory,relative);}
  #action(action,segment,details,backup=null){this.#run('INSERT INTO log_recovery_actions VALUES (?,?,?,?,?,?)',randomUUID(),action,segment,Date.now()*1000,backup,JSON.stringify(details));}
  #recover(segment) {
    const path=this.#path(segment.relative_path),durable=size(segment.durable_bytes);
    if(!existsSync(path)&&durable>0)throw new Error(`已确认日志段缺失：${segment.relative_path}`);
    const fd=openSync(path,segment.state==='active'?'a+':'r',0o600);
    try {
      if(fstatSync(fd).size<durable)throw new Error('已确认日志前缀缺失，禁止覆盖');
      let offset=0;
      for(const row of bigAll(this.#db,'SELECT * FROM log_index WHERE segment_id=? ORDER BY byte_offset',segment.segment_id)){
        if(size(row.byte_offset)!==offset)throw new Error('日志索引不连续');
        if(size(row.byte_length)>this.#policy.max_record_bytes)throw new Error('已确认日志长度超出恢复预算');
        const bytes=readBytes(fd,offset,size(row.byte_length));const record=JSON.parse(bytes.toString('utf8'));
        if(record.stamp?.sequence!==String(row.sequence)||record.event_code!==row.event_code)throw new Error('已确认日志内容与索引冲突');
        if(row.record_sha256&&row.record_sha256!==hash(bytes))throw new Error('已确认日志校验失败');
        if(!row.record_sha256)this.#run('UPDATE log_index SET record_sha256=? WHERE sequence=?',hash(bytes),row.sequence);
        offset+=bytes.length;
      }
      if(offset!==durable)throw new Error('已确认长度与日志索引冲突');
      if(segment.state==='sealed') {
        if(fstatSync(fd).size!==durable)throw new Error('封存日志大小变化');
        if(segment.sha256&&segment.sha256!==this.#fileHash(fd,durable))throw new Error('封存日志校验失败');return;
      }
      const tailSize=fstatSync(fd).size-durable;
      if(tailSize>this.#policy.segment_max_bytes+this.#policy.max_record_bytes)throw new Error('恢复尾部过大，拒绝无界读取');
      const tail=readBytes(fd,durable,tailSize);let cursor=0;
      while(cursor<tail.length) {
        const end=tail.indexOf(10,cursor);
        if(end<0){this.#trim(fd,segment,offset,tail.subarray(cursor),'partial_line');break;}
        const bytes=tail.subarray(cursor,end+1);let record;
        try{record=JSON.parse(bytes.toString('utf8'));}catch{throw new Error('未知完整尾行无效，保留原文件');}
        const sequence=record.stamp?.sequence;
        if(typeof sequence!=='string'||!/^[1-9][0-9]*$/.test(sequence))throw new Error('未知完整尾行缺少序号');
        const pending=this.#db.prepare('SELECT payload_json FROM log_outbox WHERE sequence=?').get(BigInt(sequence));
        if(pending) {
          if(!bytes.equals(Buffer.from(pending.payload_json+'\n')))throw new Error('完整尾行与outbox冲突');
          fsyncSync(fd);this.#index(segment,record,bytes,offset);offset+=bytes.length;cursor=end+1;
        } else {
          const indexed=this.#db.prepare('SELECT record_sha256 FROM log_index WHERE sequence=?').get(BigInt(sequence));
          if(indexed?.record_sha256===hash(bytes)) {this.#validateDiscardTail(tail.subarray(cursor));this.#trim(fd,segment,offset,tail.subarray(cursor),'duplicate_tail');break;}
          throw new Error('未知完整尾行没有持久intent，禁止删除');
        }
      }
    } finally {closeSync(fd);}
  }
  #validateDiscardTail(tail) {
    for(let offset=0;offset<tail.length;) {
      const end=tail.indexOf(10,offset);if(end<0)return;
      const bytes=tail.subarray(offset,end+1);let record;
      try{record=JSON.parse(bytes.toString('utf8'));}catch{throw new Error('重复尾行后存在未知完整记录，保留原文件');}
      const seq=record.stamp?.sequence;if(typeof seq!=='string'||!/^[1-9][0-9]*$/.test(seq))throw new Error('重复尾行后存在未知序号');
      const pending=this.#db.prepare('SELECT payload_json FROM log_outbox WHERE sequence=?').get(BigInt(seq));
      const indexed=this.#db.prepare('SELECT record_sha256 FROM log_index WHERE sequence=?').get(BigInt(seq));
      if(!(pending&&bytes.equals(Buffer.from(pending.payload_json+'\n')))&&indexed?.record_sha256!==hash(bytes))throw new Error('重复尾行后存在无intent完整记录，禁止删除');
      offset=end+1;
    }
  }
  #trim(fd,segment,offset,tail,reason) {
    const backup=`recovery-${randomUUID()}.bin`;const copy=openSync(join(this.#directory,backup),'wx',0o600);
    try{writeAll(copy,tail);fsyncSync(copy);}finally{closeSync(copy);}this.#syncDirectory();
    this.#action('tail_trim',segment.segment_id,{phase:'intent',reason,bytes:tail.length,sha256:hash(tail)},backup);
    ftruncateSync(fd,offset);fsyncSync(fd);
    this.#action('tail_trim_complete',segment.segment_id,{reason,durable_bytes:offset},backup);
  }
  #fileHash(fd,length){const h=createHash('sha256');for(let offset=0;offset<length;){const n=Math.min(65536,length-offset);h.update(readBytes(fd,offset,n));offset+=n;}return h.digest('hex');}
  #createSegment() {
    const first=!this.#db.prepare('SELECT 1 FROM log_segments LIMIT 1').get();
    const id=first?'mock-events':randomUUID(),relative=first?'events.jsonl':`events.${id}.jsonl`;
    const uptime=bigAll(this.#db,'SELECT uptime_ns FROM log_maintenance_state WHERE singleton=1')[0].uptime_ns;
    this.#run("INSERT INTO log_segments(segment_id,relative_path,state,policy_revision,opened_uptime_ns) VALUES (?,?,'active',?,?)",id,relative,this.#revision,uptime);
    this.#active=bigAll(this.#db,'SELECT * FROM log_segments WHERE segment_id=?',id)[0];this.#openActive();
  }
  #openActive(){const path=this.#path(this.#active.relative_path);this.#fd=openSync(path,'a+',0o600);fsyncSync(this.#fd);this.#syncDirectory();}
  #index(segment,record,bytes,offset) {
    this.#db.exec('BEGIN IMMEDIATE');
    try {
      const seq=BigInt(record.stamp.sequence);
      this.#run('INSERT INTO log_index VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)',seq,segment.segment_id,offset,bytes.length,
        Date.parse(record.stamp.observed_at)*1000,record.stamp.boot_id,BigInt(record.stamp.monotonic_ns),record.level,record.event_code,
        record.context.device,record.context.tag,record.context.scan_cycle,record.context.session_id,hash(bytes));
      this.#run('UPDATE log_segments SET first_sequence=COALESCE(first_sequence,?),last_sequence=?,durable_bytes=? WHERE segment_id=?',seq,seq,offset+bytes.length,segment.segment_id);
      this.#run('DELETE FROM log_outbox WHERE sequence=?',seq);this.#db.exec('COMMIT');
    }catch(error){this.#db.exec('ROLLBACK');throw error;}
  }
  flush() {
    if(this.#closed)throw new Error('日志存储已关闭');
    const path=this.#path(this.#active.relative_path),opened=fstatSync(this.#fd,{bigint:true});
    if(!existsSync(path))throw new Error('活动日志文件缺失，禁止确认向已删除文件的写入');
    const named=lstatSync(path,{bigint:true});if(opened.ino!==named.ino||opened.dev!==named.dev)throw new Error('活动日志文件被替换，禁止继续追加');
    for(const row of bigAll(this.#db,'SELECT sequence,payload_json FROM log_outbox ORDER BY sequence')) {
      const bytes=Buffer.from(row.payload_json+'\n');if(bytes.length>this.#policy.max_record_bytes)throw new Error('单条日志超过限制，intent保留');
      const current=fstatSync(this.#fd).size;
      if(current>0&&current+bytes.length>this.#policy.segment_max_bytes)this.#rotate();
      const offset=fstatSync(this.#fd).size;writeAll(this.#fd,bytes);fsyncSync(this.#fd);
      this.#index(this.#active,JSON.parse(row.payload_json),bytes,offset);
    }
  }
  #rotate() {
    fsyncSync(this.#fd);const length=fstatSync(this.#fd).size;
    const uptime=bigAll(this.#db,'SELECT uptime_ns FROM log_maintenance_state WHERE singleton=1')[0].uptime_ns;
    this.#run("UPDATE log_segments SET state='sealed',sha256=?,sealed_uptime_ns=? WHERE segment_id=?",this.#fileHash(this.#fd,length),uptime,this.#active.segment_id);
    closeSync(this.#fd);this.#fd=undefined;this.#createSegment();
  }
  #finishDelete(segment) {
    const path=this.#path(segment.relative_path);if(existsSync(path)){unlinkSync(path);this.#syncDirectory();}
    this.#db.exec('BEGIN IMMEDIATE');
    try {this.#run('DELETE FROM log_index WHERE segment_id=?',segment.segment_id);this.#run("UPDATE log_segments SET state='deleted',deleted_at_us=? WHERE segment_id=?",Date.now()*1000,segment.segment_id);this.#db.exec('COMMIT');}
    catch(error){this.#db.exec('ROLLBACK');throw error;}
  }
  checkpoint({observed_at_ms,monotonic_ns,clock_epoch}) {
    const wall=toMicroseconds(observed_at_ms),mono=BigInt(monotonic_ns);
    if(wall===null||mono<0n||!Number.isSafeInteger(clock_epoch)||clock_epoch<0)throw new TypeError('无效日志检查点');
    const state=bigAll(this.#db,'SELECT * FROM log_maintenance_state WHERE singleton=1')[0];
    let uptime=state.uptime_ns;
    if(state.boot_id===this.#boot) {
      if(mono<state.last_monotonic_ns)throw new Error('日志维护单调时间倒退');
      uptime+=mono-state.last_monotonic_ns;
    }
    this.#run('UPDATE log_maintenance_state SET uptime_ns=?,boot_id=?,last_monotonic_ns=? WHERE singleton=1',uptime,this.#boot,mono);
    if(uptime-BigInt(this.#active.opened_uptime_ns)>=BigInt(this.#policy.segment_max_age_ms)*1000000n && fstatSync(this.#fd).size>0)this.#rotate();
    const previous=bigAll(this.#db,'SELECT * FROM log_recording_spans ORDER BY rowid DESC LIMIT 1')[0];
    const closeEnough=previous&&previous.boot_id===this.#boot&&mono-previous.last_monotonic_ns<=BigInt(this.#policy.coverage_max_gap_ms)*1000000n;
    if(closeEnough && Number(previous.clock_epoch)===clock_epoch && wall>=Number(previous.to_us)-1) {
      if(previous.segment_id===this.#active.segment_id) {
        this.#run('UPDATE log_recording_spans SET to_us=?,last_monotonic_ns=? WHERE span_id=?',wall+1,mono,previous.span_id);return;
      }
      if(wall>Number(previous.to_us))this.#run('UPDATE log_recording_spans SET to_us=? WHERE span_id=?',wall,previous.span_id);
    } else if(previous && (Number(previous.clock_epoch)!==clock_epoch || wall<Number(previous.to_us)-1)) {
      const start=Math.min(Number(previous.to_us)-1,wall),end=Math.max(Number(previous.to_us),wall+1);
      this.#run("INSERT INTO log_time_gaps VALUES (?,?,?,'clock_discontinuity')",randomUUID(),start,end);
    }
    this.#run("INSERT INTO log_recording_spans VALUES (?,?,?,?,?,?,?,'recorded',?)",randomUUID(),this.#active.segment_id,this.#boot,clock_epoch,wall,wall+1,mono,this.#revision);
  }
  prune() {
    const uptime=bigAll(this.#db,'SELECT uptime_ns FROM log_maintenance_state WHERE singleton=1')[0].uptime_ns;
    let deleted=0;
    for(const segment of bigAll(this.#db,"SELECT s.*,p.policy_json FROM log_segments s JOIN retention_policies p ON p.revision=s.policy_revision WHERE s.state='sealed'")) {
      const keep=JSON.parse(segment.policy_json).retention_ms;
      if(!Number.isSafeInteger(keep)||keep<=0||segment.sealed_uptime_ns===null||uptime-segment.sealed_uptime_ns<BigInt(keep)*1000000n)continue;
      this.#db.exec('BEGIN IMMEDIATE');
      try {
        this.#run("UPDATE log_recording_spans SET status='rotated' WHERE segment_id=?",segment.segment_id);
        this.#run("UPDATE log_segments SET state='deleting' WHERE segment_id=?",segment.segment_id);this.#db.exec('COMMIT');
      }catch(error){this.#db.exec('ROLLBACK');throw error;}
      this.#finishDelete(segment);deleted++;
    }
    return {deleted_segments:deleted,...this.health()};
  }
  health() {
    const total=bigAll(this.#db,"SELECT COALESCE(SUM(durable_bytes),0) bytes FROM log_segments WHERE state!='deleted'")[0].bytes;
    let free=null;try{const fs=statfsSync(this.#directory,{bigint:true});free=fs.bavail*fs.bsize;}catch(error){if(!['ENOSYS','ENOTSUP'].includes(error.code))throw error;}
    return {policy_revision:this.#revision,retained_bytes:String(total),budget_exceeded:total>BigInt(this.#policy.budget_bytes),evidence_fault:this.#evidenceFault,directory_sync_supported:this.#directorySync,
      disk_free_bytes:free===null?null:String(free),disk_below_reserve:free===null?null:free<BigInt(this.#policy.reserve_bytes),
      recovery_summary:this.#db.prepare('SELECT action,count(*) count FROM log_recovery_actions GROUP BY action').all(),
      policy:structuredClone(this.#policy)};
  }
  query(options) {
    try {return readLogWindow(this.#db,this.#directory,this.#revision,this.#policy.max_query_records,options);}
    catch(error){if(error.code==='LOG_EVIDENCE_CORRUPT')this.#evidenceFault=error.message;throw error;}
  }
  close(){if(this.#closed)return;this.#closed=true;if(this.#fd!==undefined){closeSync(this.#fd);this.#fd=undefined;}}
}
function segmentPath(directory,relative) {
    if(!/^events(?:\.[a-zA-Z0-9-]+)?\.jsonl$/.test(relative))throw new Error('非法日志段路径');
    const path=join(directory,relative);
    if(existsSync(path)&&lstatSync(path).isSymbolicLink())throw new Error('日志段不能是符号链接');return path;
  }
export function readLogWindow(db,directory,revision,maxRecords,{from_ms,to_ms,device,tag,event_code,text,limit=200,device_ids,tag_ids}) {
    const from=toMicroseconds(from_ms),to=toMicroseconds(to_ms);
    if(from===null||to===null||to<=from||!Number.isInteger(limit)||limit<1||limit>1000)throw new TypeError('无效日志查询窗口/条数');
    for(const x of [device,tag,event_code,text])if(x!==undefined&&typeof x!=='string')throw new TypeError('无效日志筛选');
    const rows=bigAll(db,`SELECT i.*,s.relative_path FROM log_index i JOIN log_segments s ON s.segment_id=i.segment_id
      WHERE s.state IN ('active','sealed') AND i.observed_at_us>=? AND i.observed_at_us<? ORDER BY i.sequence LIMIT ?`,from,to,maxRecords+1);
    const pending=bigAll(db,'SELECT sequence,payload_json FROM log_outbox WHERE observed_at_us>=? AND observed_at_us<? ORDER BY sequence LIMIT ?',from,to,maxRecords+1);
    if(rows.length+pending.length>maxRecords)throw new Error('日志查询超出精确计数预算，请缩小窗口');
    const records=new Map();
    for(const row of rows) {
      let fd;
      try {
        fd=openSync(segmentPath(directory,row.relative_path),'r');const bytes=readBytes(fd,size(row.byte_offset),size(row.byte_length));
        if(row.record_sha256!==hash(bytes))throw new Error('日志正文校验不符');
        const record=JSON.parse(bytes.toString('utf8'));if(record.stamp.sequence!==String(row.sequence))throw new Error('日志序号不符');
        records.set(record.stamp.sequence,record);
      }catch(cause){const error=new Error('日志证据文件缺失或损坏，不能返回零匹配',{cause});error.code='LOG_EVIDENCE_CORRUPT';throw error;}
      finally{if(fd!==undefined)closeSync(fd);}
    }
    for(const row of pending)records.set(String(row.sequence),JSON.parse(row.payload_json));
    const matches=[...records.values()].filter(r=>(!device_ids?.length||device_ids.includes(r.context.device))&&(!tag_ids?.length||tag_ids.includes(r.context.tag))&&(device===undefined||r.context.device===device)&&(tag===undefined||r.context.tag===tag)
      &&(event_code===undefined||r.event_code===event_code)&&(text===undefined||JSON.stringify(r).includes(text)))
      .sort((a,b)=>BigInt(a.stamp.sequence)<BigInt(b.stamp.sequence)?-1:1);
    const spans=bigAll(db,'SELECT * FROM log_recording_spans WHERE from_us<? AND to_us>?',to,from);
    const gaps=bigAll(db,'SELECT * FROM log_time_gaps WHERE from_us<? AND to_us>?',to,from);
    return {...coverageFor(from,to,spans,gaps),retention_policy_revision:revision,matched_count:matches.length,count_is_exact:true,
      items:matches.slice(0,limit),truncated:matches.length>limit};
  }
