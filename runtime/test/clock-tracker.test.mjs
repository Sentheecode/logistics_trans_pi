import test from 'node:test';
import assert from 'node:assert/strict';
import { ClockTracker } from '../src/clock-tracker.mjs';

const sample=(overrides={})=>({boot_id:'b1',tag_id:'tag-1',observed_at_ms:10000,monotonic_ns:'0',
  event_at_ms:10000,timestamp_basis:'observed',...overrides});

test('正常长间隔不误判；墙钟回跳保留原时间并切新段',()=>{
  const t=new ClockTracker();t.commit(t.preview(sample()));
  let p=t.preview(sample({observed_at_ms:20000,event_at_ms:20000,monotonic_ns:'10000000000'}));
  assert.deepEqual(p.reasons,[]);t.commit(p);
  p=t.preview(sample({observed_at_ms:5000,event_at_ms:5000,monotonic_ns:'10100000000'}));
  assert.equal(p.epoch,1);assert.ok(p.reasons.includes('wall_backward'));
  assert.equal(p.next.last_observation.observed_us,'5000000');
});

test('向前跳与boot切换均分段，不比较不同boot的单调时间',()=>{
  const t=new ClockTracker();t.commit(t.preview(sample()));
  let p=t.preview(sample({observed_at_ms:3600000,event_at_ms:3600000,monotonic_ns:'100000000'}));
  assert.ok(p.reasons.includes('wall_jump'));t.commit(p);
  p=t.preview(sample({boot_id:'b2',observed_at_ms:100,event_at_ms:100,monotonic_ns:'0'}));
  assert.deepEqual(p.reasons,['boot_boundary']);assert.equal(p.epoch,2);
});

test('源事件时间不连续与接收墙钟分开判断，不覆盖源时间',()=>{
  const t=new ClockTracker();t.commit(t.preview(sample({timestamp_basis:'source',event_at_ms:9000})));
  const p=t.preview(sample({observed_at_ms:10100,monotonic_ns:'100000000',timestamp_basis:'source',event_at_ms:1000}));
  assert.deepEqual(p.reasons,['event_backward']);assert.equal(p.next.streams[0].event_us,'1000000');
});

test('预计算不改变已提交状态；持久state重建后保持boot边界',()=>{
  const t=new ClockTracker();const p=t.preview(sample());
  assert.equal(t.state().last_observation,null);t.commit(p);
  t.preview(sample({observed_at_ms:1,event_at_ms:1,monotonic_ns:'100'}));
  assert.equal(t.state().epoch,0);
  const restored=new ClockTracker({state:t.state()});
  assert.equal(restored.preview(sample({boot_id:'b2'})).epoch,1);
});

test('拒绝单调值倒退、非法数字或state版本，不为无效记录推进epoch',()=>{
  const t=new ClockTracker();t.commit(t.preview(sample({monotonic_ns:'1000'})));
  assert.throws(()=>t.preview(sample({monotonic_ns:'999'})));
  assert.throws(()=>t.preview(sample({observed_at_ms:Infinity})));
  assert.throws(()=>t.preview(sample({monotonic_ns:'-1'})));
  assert.throws(()=>new ClockTracker({state:{version:99}}));
  assert.equal(t.state().epoch,0);
});

test('源时间向前突变与时间域切换都有明确原因，过期preview不能覆盖新state',()=>{
  const t=new ClockTracker();t.commit(t.preview(sample({timestamp_basis:'source'})));
  const forward=t.preview(sample({observed_at_ms:10100,event_at_ms:3600000,monotonic_ns:'100000000',timestamp_basis:'source'}));
  assert.deepEqual(forward.reasons,['event_time_discontinuity']);t.commit(forward);
  assert.throws(()=>t.commit(forward),/过期/);
  const changed=t.preview(sample({observed_at_ms:10200,event_at_ms:10200,monotonic_ns:'200000000',timestamp_basis:'server'}));
  assert.deepEqual(changed.reasons,['event_basis_changed']);
});
