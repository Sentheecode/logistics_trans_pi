import test from 'node:test';
import assert from 'node:assert/strict';
import { MockDriver } from '../src/mock-driver.mjs';

import { manifest } from './fixture.mjs';

test('不同采集组独立调度，源时间为零仍优先，回退到服务器时间', () => {
  const d=new MockDriver(manifest()); const samples=[];
  d.on('sample', s=>samples.push(s)); d.connect();
  d.setValue('temperature',5,{source_at_ms:0,server_at_ms:99});
  d.advance(100);
  assert.equal(samples.find(s=>s.tag_id==='temperature').event_at_ms,0);
  assert.equal(samples.filter(s=>s.tag_id==='status').length,0);
  d.setValue('temperature',7,{source_at_ms:null,server_at_ms:88});
  d.advance(100);
  assert.equal(samples.at(-1).timestamp_basis,'server');
  assert.equal(samples.at(-1).event_at_ms,88);
  d.advance(800);
  assert.equal(samples.filter(s=>s.tag_id==='status').length,1);
});

test('死区只抑制数值通知，质量改变仍必须送达', () => {
  const d=new MockDriver(manifest()); const samples=[];
  d.on('sample',s=>samples.push(s)); d.connect(); d.advance(100);
  d.setValue('temperature',5.25); d.advance(100);
  assert.equal(samples.filter(s=>s.tag_id==='temperature').length,1);
  d.setValue('temperature',5.25,{quality:{level:'bad',reason:'source_bad'}}); d.advance(100);
  assert.equal(samples.at(-1).quality.level,'bad');
  d.setValue('temperature',5.25,{quality:{level:'good',reason:'source_good'}}); d.advance(100);
  assert.equal(samples.at(-1).quality.level,'good');
});

test('断线后链路/会话可恢复，但必须按清单重建订阅才恢复样本', () => {
  const input=manifest(); const d=new MockDriver(input); const samples=[];
  input.groups[0].tags.length=0;
  d.on('sample',s=>samples.push(s)); d.connect(); d.advance(100);
  d.disconnect();
  assert.deepEqual(d.snapshot().connection,{link:'down',session:'absent',subscription:'absent'});
  assert.ok(samples.some(s=>s.quality.reason==='disconnected'));
  d.restoreSession();
  assert.deepEqual(d.snapshot().connection,{link:'up',session:'active',subscription:'stale'});
  const before=samples.length; d.advance(1000); assert.equal(samples.length,before);
  d.rebuildSubscriptions(); d.advance(100);
  assert.ok(samples.length>before);
  assert.equal(d.snapshot().manifest.groups[0].tags.length,2);
  assert.equal(d.snapshot().generation,2);
});

test('丢失订阅不会伪造 TCP 断线，主动重建后恢复首样本', () => {
  const d=new MockDriver(manifest()); const samples=[];
  d.on('sample',s=>samples.push(s)); d.connect(); d.advance(100);
  const session=samples[0].session_id;
  d.loseSubscriptions();
  assert.deepEqual(d.snapshot().connection,{link:'up',session:'active',subscription:'stale'});
  d.rebuildSubscriptions();
  assert.equal(d.snapshot().connection.subscription,'healthy');
  d.advance(100);
  assert.equal(samples.at(-1).session_id,session);
});

test('拒绝重复 tag、非数值死区、无量程百分比、无效采样间隔及错误值类型', () => {
  let m=manifest(); m.groups[1].tags[0].id='temperature'; assert.throws(()=>new MockDriver(m));
  m=manifest(); m.groups[0].tags[1].deadband={mode:'absolute',value:1}; assert.throws(()=>new MockDriver(m));
  m=manifest(); m.groups[0].tags[0].deadband={mode:'percent',value:1}; assert.throws(()=>new MockDriver(m));
  m=manifest(); m.groups[0].interval_ms=0; assert.throws(()=>new MockDriver(m));
  const d=new MockDriver(manifest()); assert.throws(()=>d.setValue('temperature','wrong'));
  assert.throws(()=>d.setValue('missing',1));
});

test('百分比死区按工程量程计算，比较上次已发送值，BOOL按变化送达', () => {
  const m=manifest();Object.assign(m.groups[0].tags[0],{engineering_low:0,engineering_high:100,deadband:{mode:'percent',value:10}});
  const d=new MockDriver(m); const samples=[]; d.on('sample',s=>samples.push(s));d.connect();d.advance(100);
  d.setValue('temperature',10);d.advance(100);
  assert.equal(samples.filter(s=>s.tag_id==='temperature').length,1);
  d.setValue('temperature',15);d.setValue('heartbeat',true);d.advance(100);
  assert.equal(samples.filter(s=>s.tag_id==='temperature').length,2);
  assert.equal(samples.at(-1).value.value,true);
  assert.equal(samples.at(-1).timestamp_basis,'observed');
});
