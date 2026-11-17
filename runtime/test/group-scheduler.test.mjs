import test from'node:test';import assert from'node:assert/strict';
import{GroupScheduler}from'../src/group-scheduler.mjs';
test('分组按单调deadline调度，迟到跳过旧周期而不无限追赶',async()=>{
  let now=0n;const calls=[],skips=[];
  const s=new GroupScheduler({now:()=>now,onOverrun:x=>skips.push(x),groups:[
    {id:'fast',interval_ms:100,poll:ctx=>calls.push(['fast',ctx.skipped_cycles])},
    {id:'slow',interval_ms:1000,poll:()=>calls.push(['slow',0])}]});
  s.start({timers:false});now=100000000n;await s.pump();assert.deepEqual(calls,[['fast',0]]);
  now=1050000000n;await s.pump();assert.deepEqual(calls.slice(1),[['fast',8],['slow',0]]);
  assert.equal(skips[0].missed_cycles,8);await s.stop();
});
test('异步慢组不会重入，故障显式通知并停止该组',async()=>{
  let now=0n,release;const errors=[];let starts=0;
  const s=new GroupScheduler({now:()=>now,onFault:x=>errors.push(x),groups:[
    {id:'slow',interval_ms:100,timeout_ms:1000,poll:()=>{starts++;return new Promise(r=>release=r);}},
    {id:'bad',interval_ms:100,poll:()=>{throw new Error('injected poll failure');}}]});
  s.start({timers:false});now=100000000n;const pending=s.pump();await Promise.resolve();
  now=200000000n;await s.pump();assert.equal(starts,1);assert.equal(errors.length,1);
  assert.match(errors[0].error.message,/injected/);release();await pending;await s.stop();
});
test('真实定时器运行后能停止，不残留重复调度',async()=>{
  let count=0;let done;
  const complete=new Promise(r=>done=r);
  const s=new GroupScheduler({groups:[{id:'real',interval_ms:10,poll:()=>{if(++count===3)done();}}]});
  s.start();await complete;await s.stop();const before=count;
  await new Promise(r=>setTimeout(r,30));assert.equal(count,before);
});
