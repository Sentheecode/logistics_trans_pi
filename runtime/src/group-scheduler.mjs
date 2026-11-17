/** 单调deadline，组间独立、组内不重入；迟到不无限追赶。 */
export class GroupScheduler {
  #groups;#now;#onFault;#onOverrun;#running=false;#timer;#timers=false;
  constructor({groups,now=()=>process.hrtime.bigint(),onFault=()=>{},onOverrun=()=>{}}) {
    this.#now=now;this.#onFault=onFault;this.#onOverrun=onOverrun;
    const ids=new Set();
    this.#groups=groups.map(g=>{
      if(!g.id||ids.has(g.id)||!Number.isSafeInteger(g.interval_ms)||g.interval_ms<1||typeof g.poll!=='function')throw new TypeError('无效调度组');
      const timeout=g.timeout_ms??Math.max(g.interval_ms*3,1000);
      if(!Number.isSafeInteger(timeout)||timeout<1)throw new TypeError('无效调度超时');ids.add(g.id);
      return{...g,interval:BigInt(g.interval_ms)*1000000n,timeout:BigInt(timeout)*1000000n,
        next:0n,busy:false,faulted:false,runs:0,missed:0,failures:0,last_started:0n,last_finished:0n,controller:null,job:null};
    });
    if(!this.#groups.length)throw new TypeError('调度组不能为空');
  }
  start({timers=true}={}) {
    if(this.#running)throw new Error('调度器已启动');this.#running=true;this.#timers=timers;
    const now=this.#now();for(const g of this.#groups)g.next=now+g.interval;
    if(timers)this.#arm();
  }
  #fault(g,error){if(g.faulted)return;g.faulted=true;g.failures++;g.controller?.abort(error);this.#onFault({group_id:g.id,error});}
  pump() {
    if(!this.#running)return Promise.resolve();const jobs=[],now=this.#now();
    for(const g of this.#groups) {
      if(g.faulted)continue;
      if(g.busy&&now-g.last_started>g.timeout){this.#fault(g,new Error(`采集组超时：${g.id}`));continue;}
      if(now<g.next)continue;
      const due=(now-g.next)/g.interval+1n;
      if(due>BigInt(Number.MAX_SAFE_INTEGER))throw new Error('错过周期计数溢出');
      const missed=Number(due)-(g.busy?0:1),scheduled=g.next+(due-1n)*g.interval;g.next+=due*g.interval;
      if(missed){g.missed+=missed;this.#onOverrun({group_id:g.id,missed_cycles:missed,observed_at_ns:String(now)});}
      if(g.busy)continue;
      g.busy=true;g.controller=new AbortController();g.last_started=now;g.runs++;
      g.job=Promise.resolve().then(()=>g.poll({group_id:g.id,scheduled_ns:scheduled,started_ns:this.#now(),skipped_cycles:missed,signal:g.controller.signal}))
        .catch(error=>this.#fault(g,error)).finally(()=>{g.busy=false;g.last_finished=this.#now();});
      jobs.push(g.job);
    }
    return Promise.all(jobs);
  }
  #arm() {
    if(!this.#running||!this.#timers)return;
    const active=this.#groups.filter(g=>!g.faulted);if(!active.length)return;
    const deadline=active.reduce((n,g)=>g.next<n?g.next:n,active[0].next);
    const ms=Math.max(1,Math.min(2147483647,Math.ceil(Number(deadline-this.#now())/1e6)));
    this.#timer=setTimeout(()=>{const work=this.pump();this.#arm();work.catch(error=>{
      this.#running=false;clearTimeout(this.#timer);this.#onFault({group_id:null,error});
    });},ms);
  }
  stats(){return this.#groups.map(g=>({group_id:g.id,runs:g.runs,missed_cycles:g.missed,failures:g.failures,busy:g.busy,
    next_due_ns:String(g.next),last_started_ns:String(g.last_started),last_finished_ns:String(g.last_finished)}));}
  async stop({timeout_ms=2000}={}) {
    this.#running=false;clearTimeout(this.#timer);for(const g of this.#groups)g.controller?.abort();
    let timer;
    try {
      await Promise.race([Promise.allSettled(this.#groups.filter(g=>g.busy).map(g=>g.job)),
        new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('调度器停止超时')),timeout_ms);})]);
    } finally {clearTimeout(timer);}
  }
}
