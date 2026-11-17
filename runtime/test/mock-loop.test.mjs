import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync, renameSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { MockDriver } from '../src/mock-driver.mjs';
import { MockLoop } from '../src/mock-loop.mjs';
import { manifest, rules } from './fixture.mjs';

test('样本→报警→历史→JSONL，坏质量不清报警，恢复后完成闭环', () => {
  const directory=mkdtempSync(join(tmpdir(),'ltp-loop-test-'));
  let loop;
  try {
    loop=new MockLoop({directory,manifest:manifest(),rules:rules()});
    const driver=new MockDriver(manifest()); driver.on('sample',s=>loop.accept(s));
    driver.connect(); driver.advance(100);
    driver.setValue('temperature',12); driver.advance(100);
    driver.setValue('temperature',1,{quality:{level:'bad',reason:'source_bad'}}); driver.advance(100);
    driver.setValue('temperature',5); driver.advance(100);
    driver.disconnect(); loop.close(); loop=null;
    const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    try {
      assert.equal(db.prepare('SELECT count(*) n FROM history_raw').get().n,8);
      const alarm=db.prepare('SELECT * FROM alarm_occurrences').get();
      assert.equal(alarm.condition,'cleared'); assert.equal(alarm.acknowledgement,'unacknowledged');
      const transitions=db.prepare('SELECT transition FROM alarm_transitions ORDER BY sequence').all().map(r=>r.transition);
      assert.deepEqual(transitions,['activate','quality_unknown','clear','quality_unknown']);
      assert.equal(db.prepare('SELECT count(*) n FROM log_outbox').get().n,0);
      assert.deepEqual(db.prepare('PRAGMA foreign_key_check').all(),[]);
      const lines=readFileSync(join(directory,'events.jsonl'),'utf8').trim().split('\n').map(JSON.parse);
      assert.equal(lines.length,8);
      assert.ok(lines.some(l=>l.event_code==='alarm.activate'));
      assert.ok(lines.some(l=>l.context.tag==='temperature' && l.fields.quality.reason==='disconnected'));
      assert.ok(lines.every(l=>Object.keys(l.context).sort().join(',')==='device,scan_cycle,session_id,tag'));
      assert.equal(db.prepare('SELECT count(*) n FROM log_index').get().n,lines.length);
    } finally {db.close();}
  } finally {if(loop) loop.close();rmSync(directory,{recursive:true,force:true});}
});

test('重新启动后序号高水位不回退，已有历史和未确认报警保留', () => {
  const directory=mkdtempSync(join(tmpdir(),'ltp-loop-restart-'));
  try {
    for(let round=0;round<2;round++) {
      const loop=new MockLoop({directory,manifest:manifest(),rules:rules()});
      const driver=new MockDriver(manifest()); driver.on('sample',s=>loop.accept(s));
      driver.connect();driver.setValue('temperature',12);driver.advance(100);loop.close();
    }
    const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    try {
      const rows=db.prepare('SELECT sequence,boot_id FROM history_raw ORDER BY sequence').all();
      assert.equal(rows.length,4);
      assert.ok(rows[2].sequence>rows[1].sequence);
      assert.notEqual(rows[0].boot_id,rows[2].boot_id);
      assert.equal(db.prepare('SELECT count(*) n FROM alarm_occurrences').get().n,1);
    } finally {db.close();}
  } finally {rmSync(directory,{recursive:true,force:true});}
});

test('未知 tag 在事务前拒绝，存储不留下半条历史', () => {
  const directory=mkdtempSync(join(tmpdir(),'ltp-loop-invalid-'));
  const loop=new MockLoop({directory,manifest:manifest(),rules:rules()});
  try { assert.throws(()=>loop.accept({tag_id:'missing'})); }
  finally {loop.close();}
  const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
  try {assert.equal(db.prepare('SELECT count(*) n FROM history_raw').get().n,0);}
  finally {db.close();rmSync(directory,{recursive:true,force:true});}
});

test('同一数据目录拒绝第二个写入者；未经受控变更不得更换订阅配置', () => {
  const directory=mkdtempSync(join(tmpdir(),'ltp-loop-lock-'));
  let first;
  try {
    first=new MockLoop({directory,manifest:manifest(),rules:rules()});
    assert.throws(()=>new MockLoop({directory,manifest:manifest(),rules:rules()}));
    first.close();first=null;
    const changed=manifest();changed.groups[0].tags[0].deadband.value=2;
    assert.throws(()=>new MockLoop({directory,manifest:changed,rules:rules()}));
    first=new MockLoop({directory,manifest:manifest(),rules:rules()});
    first.close();first=null;
  } finally {if(first)first.close();rmSync(directory,{recursive:true,force:true});}
});

test('超过 JS 安全整数的持久序号仍精确写入 JSONL 和 SQLite', () => {
  const directory=mkdtempSync(join(tmpdir(),'ltp-loop-bigint-'));
  try {
    let loop=new MockLoop({directory,manifest:manifest()});loop.close();
    const seed=new DatabaseSync(join(directory,'runtime.sqlite'));
    seed.prepare("UPDATE sequence_counters SET high_water=? WHERE name='record'").run(9007199254740993n);seed.close();
    loop=new MockLoop({directory,manifest:manifest()});
    const driver=new MockDriver(manifest());driver.on('sample',s=>loop.accept(s));driver.connect();driver.advance(100);loop.close();
    const lines=readFileSync(join(directory,'events.jsonl'),'utf8').trim().split('\n').map(JSON.parse);
    assert.equal(lines[0].fields.sample_sequence,'9007199254740994');
    assert.equal(lines[0].stamp.sequence,'9007199254740995');
  } finally {rmSync(directory,{recursive:true,force:true});}
});

test('日志索引落盘失败后拒绝继续接收，不重复追加，保留待恢复 intent', () => {
  const directory=mkdtempSync(join(tmpdir(),'ltp-loop-fault-'));
  const loop=new MockLoop({directory,manifest:manifest()});
  try {
    const injector=new DatabaseSync(join(directory,'runtime.sqlite'));
    injector.exec("CREATE TRIGGER fail_log_index BEFORE INSERT ON log_index BEGIN SELECT RAISE(ABORT,'injected index failure'); END;");
    injector.close();
    const driver=new MockDriver(manifest());driver.on('sample',s=>loop.accept(s));driver.connect();
    assert.throws(()=>driver.advance(100),/injected index failure/);
    assert.throws(()=>driver.advance(100));
    assert.throws(()=>loop.close());
    const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    try {
      assert.equal(db.prepare('SELECT count(*) n FROM history_raw').get().n,1);
      assert.equal(db.prepare('SELECT count(*) n FROM log_outbox').get().n,1);
      assert.equal(db.prepare('SELECT count(*) n FROM log_index').get().n,0);
      assert.equal(db.prepare('SELECT clean_shutdown FROM runtime_boots').get().clean_shutdown,0);
      assert.equal(readFileSync(join(directory,'events.jsonl'),'utf8').trim().split('\n').length,1);
    } finally {db.close();}
  } finally {try{loop.close();}catch{}rmSync(directory,{recursive:true,force:true});}
});

test('墙钟前后跳不改序号顺序，历史分段且日志留下不连续证据', () => {
  const directory=mkdtempSync(join(tmpdir(),'ltp-clock-loop-'));
  const loop=new MockLoop({directory,manifest:manifest()});
  try {
    const driver=new MockDriver(manifest(),{wall_origin_ms:1700000000000});
    driver.on('sample',s=>loop.accept(s));driver.connect();driver.advance(100);
    driver.jumpWallTime(-3600000);driver.setValue('temperature',12);driver.advance(100);
    driver.jumpWallTime(7200000);driver.setValue('temperature',5);driver.advance(100);
    loop.close();
    const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    try {
      const rows=db.prepare('SELECT sequence,clock_epoch,observed_at_us FROM history_raw ORDER BY sequence').all();
      assert.deepEqual(rows.map(r=>r.clock_epoch),[0,0,1,2]);
      assert.ok(rows[2].observed_at_us<rows[0].observed_at_us);
      assert.ok(rows.every((r,i)=>i===0||r.sequence>rows[i-1].sequence));
      assert.equal(db.prepare('SELECT count(*) n FROM clock_events').get().n,2);
      const logs=readFileSync(join(directory,'events.jsonl'),'utf8').trim().split('\n').map(JSON.parse);
      assert.equal(logs.filter(r=>r.event_code==='clock.discontinuity').length,2);
    } finally {db.close();}
  } finally {loop.close();rmSync(directory,{recursive:true,force:true});}
});

test('历史事务失败不会消耗时钟段，重试仍产生同一不连续证据', () => {
  const directory=mkdtempSync(join(tmpdir(),'ltp-clock-rollback-'));
  const loop=new MockLoop({directory,manifest:manifest()});
  try {
    const driver=new MockDriver(manifest(),{wall_origin_ms:1700000000000});const samples=[];
    driver.on('sample',s=>samples.push(s));driver.connect();driver.advance(100);loop.accept(samples[0]);
    driver.jumpWallTime(-3600000);driver.setValue('temperature',12);driver.advance(100);const second=samples.at(-1);
    const injector=new DatabaseSync(join(directory,'runtime.sqlite'));
    try {
      injector.exec("CREATE TRIGGER reject_history BEFORE INSERT ON history_raw BEGIN SELECT RAISE(ABORT,'injected history failure'); END;");
      assert.throws(()=>loop.accept(second),/injected history failure/);
      assert.equal(injector.prepare('SELECT count(*) n FROM clock_events').get().n,0);
      injector.exec('DROP TRIGGER reject_history');
    } finally {injector.close();}
    loop.accept(second);loop.close();
    const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    try {
      assert.equal(db.prepare('SELECT count(*) n FROM clock_events').get().n,1);
      assert.equal(db.prepare('SELECT max(clock_epoch) epoch FROM history_raw').get().epoch,1);
    } finally {db.close();}
  } finally {loop.close();rmSync(directory,{recursive:true,force:true});}
});

test('旧版T3数据库迁移保留历史，并从既有epoch继续跨启动分段', () => {
  const directory=mkdtempSync(join(tmpdir(),'ltp-clock-migrate-'));
  try {
    let loop=new MockLoop({directory,manifest:manifest()});
    let driver=new MockDriver(manifest());driver.on('sample',s=>loop.accept(s));driver.connect();driver.advance(100);loop.close();
    renameSync(join(directory,'runtime.sqlite'),join(directory,'old.sqlite'));
    const seed=new DatabaseSync(join(directory,'runtime.sqlite'));
    try {
      seed.exec(readFileSync(new URL('../../db/001-infrastructure.sql',import.meta.url),'utf8'));
      seed.exec(`ATTACH DATABASE '${join(directory,'old.sqlite').replaceAll("'","''")}' AS old`);
      seed.exec('PRAGMA foreign_keys=OFF;BEGIN;');
      for(const {name}of seed.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name!='schema_migrations'").all()) {
        const columns=seed.prepare(`PRAGMA table_info("${name}")`).all().map(c=>`"${c.name}"`).join(',');
        seed.exec(`DELETE FROM "${name}"`);
        seed.exec(`INSERT INTO "${name}"(${columns}) SELECT ${columns} FROM old."${name}"`);
      }
      seed.exec('UPDATE history_raw SET clock_epoch=7;COMMIT;DETACH DATABASE old;');
    } finally {seed.close();}
    loop=new MockLoop({directory,manifest:manifest()});
    driver=new MockDriver(manifest());driver.on('sample',s=>loop.accept(s));driver.connect();driver.advance(100);loop.close();
    const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    try {
      assert.equal(db.prepare('SELECT max(version) v FROM schema_migrations').get().v,3);
      assert.equal(db.prepare('SELECT count(*) n FROM history_raw').get().n,4);
      assert.equal(db.prepare('SELECT max(clock_epoch) epoch FROM history_raw').get().epoch,8);
    } finally {db.close();}
  } finally {rmSync(directory,{recursive:true,force:true});}
});
