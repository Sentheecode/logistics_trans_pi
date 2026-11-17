import {parentPort,workerData}from'node:worker_threads';
const heartbeat=new BigInt64Array(workerData.buffer);
Atomics.store(heartbeat,0,process.hrtime.bigint());parentPort.postMessage({kind:'ready'});
const timer=setInterval(()=>{
  const elapsed=process.hrtime.bigint()-Atomics.load(heartbeat,0);
  if(elapsed>=BigInt(workerData.timeout_ms)*1000000n) {
    clearInterval(timer);parentPort.postMessage({kind:'watchdog.timeout',elapsed_ms:Number(elapsed/1000000n)});
    if(workerData.terminate_on_timeout)process.kill(process.pid,'SIGKILL');
  }
},workerData.interval_ms);
