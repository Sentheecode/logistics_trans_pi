import{EventEmitter}from'node:events';import{Worker}from'node:worker_threads';
export class Watchdog extends EventEmitter {
  #worker;#heartbeat;#ready;#closing=false;
  constructor({timeout_ms=5000,interval_ms=100,terminate_on_timeout=false}={}) {
    super();if(!Number.isSafeInteger(timeout_ms)||!Number.isSafeInteger(interval_ms)||interval_ms<1||timeout_ms<interval_ms||typeof terminate_on_timeout!=='boolean')throw new TypeError('无效看门狗参数');
    const buffer=new SharedArrayBuffer(8);this.#heartbeat=new BigInt64Array(buffer);this.beat();
    this.#worker=new Worker(new URL('./watchdog-worker.mjs',import.meta.url),{workerData:{buffer,timeout_ms,interval_ms,terminate_on_timeout}});
    this.#ready=new Promise((resolve,reject)=>{
      this.#worker.on('message',m=>{if(m.kind==='ready')resolve();else if(m.kind==='watchdog.timeout')this.emit('timeout',m);});
      this.#worker.on('error',error=>{reject(error);this.emit('fault',error);});
      this.#worker.on('exit',code=>{if(!this.#closing){const error=new Error(`看门狗异常退出：${code}`);reject(error);this.emit('fault',error);}});
    });
  }
  ready(){return this.#ready;}
  beat(){Atomics.store(this.#heartbeat,0,process.hrtime.bigint());}
  async close(){if(this.#closing)return;this.#closing=true;await this.#worker.terminate();}
}
