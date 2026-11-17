import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { MockDriver } from '../src/mock-driver.mjs';
import { MockLoop } from '../src/mock-loop.mjs';
import { manifest, rules } from './mock-config.mjs';

const directory=mkdtempSync(join(tmpdir(),'ltp-mock-demo-'));
const loop=new MockLoop({directory,manifest:manifest(),rules:rules()});
const driver=new MockDriver(manifest());
let delivered=0;const reconnects=[];
driver.on('sample',sample=>{loop.accept(sample);delivered++;});
try {
  driver.connect();driver.advance(100);
  driver.setValue('temperature',12);driver.advance(100);
  driver.setValue('temperature',5);driver.advance(100);
  for(let i=0;i<10;i++) {
    driver.disconnect();driver.restoreSession();
    const before=delivered;driver.advance(200);
    assert.equal(delivered,before);
    driver.rebuildSubscriptions();driver.advance(100);
    assert.ok(delivered>before);
    reconnects.push({cycle:i+1,generation:driver.snapshot().generation,resumed_samples:delivered-before});
  }
  driver.loseSubscriptions();const before=delivered;driver.advance(1000);
  assert.equal(delivered,before);
  const stale=driver.snapshot().connection;
  driver.rebuildSubscriptions();driver.advance(1000);
  assert.ok(delivered>before);
  loop.close();
  const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
  try {
    const logs=readFileSync(join(directory,'events.jsonl'),'utf8').trim().split('\n').map(JSON.parse);
    const history=db.prepare('SELECT count(*) n FROM history_raw').get().n;
    const alarmTransitions=db.prepare('SELECT transition FROM alarm_transitions ORDER BY sequence').all().map(r=>r.transition);
    assert.equal(history,delivered);assert.equal(logs.length,history);
    assert.ok(alarmTransitions.includes('activate') && alarmTransitions.includes('clear'));
    assert.equal(db.prepare('PRAGMA integrity_check').get().integrity_check,'ok');
    console.log(JSON.stringify({mode:'mock-only',node:process.version,directory,
      history_rows:history,jsonl_rows:logs.length,pending_logs:db.prepare('SELECT count(*) n FROM log_outbox').get().n,
      reconnects,subscription_loss_state:stale,final_state:driver.snapshot().connection,
      final_subscription_generation:driver.snapshot().generation,
      integrity_check:'ok',limits:['虚拟时钟与显式恢复调用','不是实际网络断开或自动健康检测','没有 PLC、LLM、HTTP 或 HMI']},null,2));
  } finally {db.close();}
} finally {loop.close();}
