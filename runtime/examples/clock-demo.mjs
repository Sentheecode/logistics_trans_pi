import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { MockLoop } from '../src/mock-loop.mjs';
import { MockDriver } from '../src/mock-driver.mjs';
import { manifest } from './mock-config.mjs';

const directory=mkdtempSync(join(tmpdir(),'ltp-clock-demo-'));
const loop=new MockLoop({directory,manifest:manifest()});
const driver=new MockDriver(manifest(),{wall_origin_ms:1700000000000});
driver.on('sample',s=>loop.accept(s));
try {
  driver.connect();driver.advance(100);
  driver.jumpWallTime(-3600000);driver.setValue('temperature',12);driver.advance(100);
  driver.jumpWallTime(7200000);driver.setValue('temperature',5);driver.advance(100);
} finally {loop.close();}
const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
try {
  const query=db.prepare('SELECT sequence,clock_epoch,observed_at_us,monotonic_ns FROM history_raw ORDER BY sequence');query.setReadBigInts(true);
  const rows=query.all();
  assert.ok(rows[2].observed_at_us<rows[0].observed_at_us);
  assert.ok(rows.every((r,i)=>i===0||r.sequence>rows[i-1].sequence));
  const clocks=db.prepare('SELECT clock_epoch,reasons_json FROM clock_events ORDER BY sequence').all();
  assert.deepEqual(clocks.map(c=>c.clock_epoch),[1,2]);
  const logs=readFileSync(join(directory,'events.jsonl'),'utf8').trim().split('\n').map(JSON.parse);
  assert.equal(logs.filter(l=>l.event_code==='clock.discontinuity').length,2);
  console.log(JSON.stringify({mode:'mock-clock-injection',node:process.version,directory,
    schema_version:db.prepare('SELECT MAX(version) version FROM schema_migrations').get().version,
    wall_adjustments_ms:[-3600000,7200000],history:rows.map(r=>Object.fromEntries(Object.entries(r).map(([k,v])=>[k,String(v)]))),
    clock_events:clocks.map(c=>({epoch:c.clock_epoch,reasons:JSON.parse(c.reasons_json)})),
    jsonl_rows:logs.length,pending_logs:db.prepare('SELECT count(*) n FROM log_outbox').get().n,
    integrity_check:db.prepare('PRAGMA integrity_check').get().integrity_check,
    limits:['未修改系统时钟','未做物理掉电','当前在样本接收时检测，不是独立后台时钟监视器']},null,2));
} finally {db.close();}
