import{EventEmitter}from'node:events';import{fork}from'node:child_process';import{fileURLToPath}from'node:url';
/** 仅管理自己启动的mock runtime；不接受外部PID或任意命令。 */
export class ServiceSupervisor extends EventEmitter {
  #directory;#delay;#max;#startup;#child=null;#running=false;#started=false;#generation=0;#restarts=0;#failures=[];#restartTimer;#startupTimer;#resolve;#reject;#settled=false;
  constructor({directory,restart_delay_ms=1000,max_restarts=3,startup_timeout_ms=600000}) {
    super();for(const n of [restart_delay_ms,startup_timeout_ms])if(!Number.isSafeInteger(n)||n<1)throw new TypeError('无效监督超时');
    if(!Number.isSafeInteger(max_restarts)||max_restarts<0||!directory)throw new TypeError('无效监督配置');
    this.#directory=directory;this.#delay=restart_delay_ms;this.#max=max_restarts;this.#startup=startup_timeout_ms;
  }
  start(){if(this.#started)throw new Error('监督进程已启动');this.#started=true;this.#running=true;
    const ready=new Promise((resolve,reject)=>{this.#resolve=value=>{this.#settled=true;resolve(value);};this.#reject=error=>{this.#settled=true;reject(error);};});this.#spawn();return ready;
  }
  #spawn(){
    if(!this.#running)return;this.#generation++;
    const child=fork(fileURLToPath(new URL('../examples/service.mjs',import.meta.url)),['--directory',this.#directory,'--watchdog-kill'],{stdio:['ignore','pipe','pipe','ipc']});
    this.#child=child;let handled=false;
    child.stdout.on('data',bytes=>this.emit('output',{stream:'stdout',text:bytes.toString()}));
    child.stderr.on('data',bytes=>this.emit('output',{stream:'stderr',text:bytes.toString()}));
    this.#startupTimer=setTimeout(()=>child.kill('SIGKILL'),this.#startup);
    child.on('message',message=>{if(this.#running&&this.#child===child&&message.event==='service.started'){
      clearTimeout(this.#startupTimer);const state=this.status();this.#resolve(state);this.emit('ready',state);
    }});
    const ended=(code,signal,error=null)=>{
      if(handled)return;handled=true;clearTimeout(this.#startupTimer);if(this.#child===child)this.#child=null;
      this.emit('exit',{code,signal,message:error?.message??null});if(!this.#running)return;
      const now=process.hrtime.bigint();this.#failures=this.#failures.filter(t=>now-t<60000000000n);
      if(this.#failures.length>=this.#max){this.#running=false;const fault=new Error('runtime超过60秒内重启预算，停止自动拉起');this.#reject(fault);this.emit('fault',fault);return;}
      this.#failures.push(now);this.#restarts++;this.#restartTimer=setTimeout(()=>this.#spawn(),this.#delay);
    };
    child.once('error',error=>ended(null,null,error));child.once('exit',(code,signal)=>ended(code,signal));
  }
  status(){return{running:this.#running,child_pid:this.#child?.pid??null,generation:this.#generation,restarts:this.#restarts};}
  async stop(){
    this.#running=false;if(this.#started&&!this.#settled)this.#reject(new Error('监督启动被取消'));clearTimeout(this.#restartTimer);clearTimeout(this.#startupTimer);const child=this.#child;if(!child)return;
    let timer;
    const exited=new Promise(resolve=>{child.once('exit',resolve);child.once('error',resolve);});
    child.kill('SIGTERM');timer=setTimeout(()=>child.kill('SIGKILL'),5000);
    try{await exited;}finally{clearTimeout(timer);if(this.#child===child)this.#child=null;}
  }
}
