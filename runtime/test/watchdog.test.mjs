import test from'node:test';import assert from'node:assert/strict';
import{Watchdog}from'../src/watchdog.mjs';
test('独立worker发现失去心跳，不依赖主线程的检测定时器',async()=>{
  const watchdog=new Watchdog({timeout_ms:80,interval_ms:10});
  try {
    const result=new Promise(resolve=>watchdog.once('timeout',resolve));
    await watchdog.ready();watchdog.beat();
    const event=await result;assert.ok(event.elapsed_ms>=80);assert.equal(event.kind,'watchdog.timeout');
  } finally {await watchdog.close();}
});
