import{EventEmitter}from'node:events';
import{MockDriver}from'./mock-driver.mjs';import{MockLoop}from'./mock-loop.mjs';
import{GroupScheduler}from'./group-scheduler.mjs';import{Watchdog}from'./watchdog.mjs';

/** 可实际定时运行的mock服务。无现场协议、业务握手或LLM。 */
export class MockService extends EventEmitter {
  #loop;#driver;#scheduler;#watchdog;#manifest;#started=false;#stopping=false;#fault=null;#beat=false;#lastHeartbeat=0n;
  #finalStatus=null;
  constructor({directory,manifest,rules=[],log_policy={},watchdog_timeout_ms=5000,watchdog_kill=false}) {
    super();this.#manifest=structuredClone(manifest);
    if(!Number.isSafeInteger(watchdog_timeout_ms)||watchdog_timeout_ms<1||typeof watchdog_kill!=='boolean'||manifest.groups.some(g=>g.id==='@maintenance'))throw new TypeError('无效服务/看门狗参数');
    this.#driver=new MockDriver(manifest,{clock:()=>this.#loop.now()});
    this.#loop=new MockLoop({directory,manifest,rules,log_policy,sample_logs:false});
    try{this.#watchdog=new Watchdog({timeout_ms:watchdog_timeout_ms,interval_ms:Math.max(1,Math.min(100,Math.floor(watchdog_timeout_ms/5))),terminate_on_timeout:watchdog_kill});}
    catch(error){this.#loop.close();throw error;}
    this.#watchdog.on('timeout',event=>this.#fail(new Error('运行时看门狗超时'),event));
    this.#watchdog.on('fault',error=>this.#fail(error));
    this.#driver.on('sample',sample=>{
      this.#loop.accept(sample);
      if(sample.tag_id==='heartbeat'&&sample.quality.level==='good')this.#lastHeartbeat=BigInt(sample.monotonic_ns);
      this.emit('sample',sample);
    });
    this.#scheduler=new GroupScheduler({groups:[...manifest.groups.map(g=>({id:g.id,interval_ms:g.interval_ms,poll:()=>{
      if(g.tags.some(t=>t.id==='heartbeat')){this.#beat=!this.#beat;this.#driver.setValue('heartbeat',this.#beat);}
      this.#driver.pollGroup(g.id,this.#loop.now());this.#watchdog.beat();
    }})),{id:'@maintenance',interval_ms:1000,poll:()=>{
      this.#watchdog.beat();this.#loop.checkpoint(this.#loop.now());
      const state=this.#driver.snapshot().connection;
      if(state.link!=='up'||state.session!=='active')this.#driver.connect();
      else if(state.subscription!=='healthy')this.#driver.rebuildSubscriptions();
      else if(manifest.groups.some(g=>g.tags.some(t=>t.id==='heartbeat'))&&BigInt(this.#loop.now().monotonic_ns)-this.#lastHeartbeat>3000000000n) {
        this.#driver.loseSubscriptions();this.#driver.rebuildSubscriptions();this.#loop.writeEvent('subscription.rebuilt','模拟心跳超时，已按清单重建订阅');
      }
      this.#loop.pruneLogs();this.#loop.writeEvent('runtime.health','运行时周期健康摘要',{groups:this.#scheduler.stats(),...this.#loop.health()});
    }}],onFault:event=>this.#fail(event.error),onOverrun:event=>this.#loop.writeEvent('scheduler.overrun','采集组错过周期，已跳过旧deadline',event,'warn')});
  }
  #fail(error,details={}){if(this.#fault)return;this.#fault=error;try{this.#loop.writeEvent('runtime.fault','运行时出现故障',{message:error.message,...details},'error');}catch{}this.emit('fault',error);}
  async start(){if(this.#started)throw new Error('服务已启动');await this.#watchdog.ready();this.#watchdog.beat();this.#started=true;this.#driver.connect();this.#loop.writeEvent('runtime.started','独立mock运行时已启动',{logs:this.#loop.health().logs});this.#scheduler.start();}
  status(){return this.#finalStatus?structuredClone(this.#finalStatus):{mode:'mock-only',connection:this.#driver.snapshot().connection,groups:this.#scheduler.stats(),fault:this.#fault?.message??null,...this.#loop.health()};}
  async stop(){if(this.#stopping)return;this.#stopping=true;
    try{await this.#scheduler.stop();if(!this.#fault){this.#driver.disconnect();this.#loop.writeEvent('runtime.stopped','独立mock运行时已停止');}}
    finally{try{await this.#watchdog.close();}finally{try{this.#finalStatus=this.status();}finally{this.#loop.close();}}}
  }
}
