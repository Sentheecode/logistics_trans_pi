import{mkdtempSync}from'node:fs';import{tmpdir}from'node:os';import{join}from'node:path';
import{MockService}from'../src/mock-service.mjs';import{manifest,rules}from'./mock-config.mjs';
const args=process.argv.slice(2),option=name=>{const i=args.indexOf(name);return i<0?null:args[i+1];};
const directory=option('--directory')??mkdtempSync(join(tmpdir(),'ltp-service-'));
const duration=option('--duration-ms')===null?null:Number(option('--duration-ms'));
if(duration!==null&&(!Number.isSafeInteger(duration)||duration<1))throw new Error('无效运行时长');
const service=new MockService({directory,manifest:manifest(),rules:rules(),watchdog_kill:args.includes('--watchdog-kill')});
let stopping=false,timer;
async function stop(reason){if(stopping)return;stopping=true;clearTimeout(timer);try{await service.stop();console.log(JSON.stringify({event:'service.stopped',reason,directory,status:service.status()}));}catch(error){console.error(JSON.stringify({event:'service.stop_failed',message:error.message}));process.exitCode=1;}}
service.on('fault',error=>{process.exitCode=1;void stop(error.message);});
process.once('SIGTERM',()=>void stop('SIGTERM'));process.once('SIGINT',()=>void stop('SIGINT'));
await service.start();const started={event:'service.started',directory,pid:process.pid,status:service.status()};
console.log(JSON.stringify(started));if(process.send)process.send(started);
if(duration!==null)timer=setTimeout(()=>void stop('duration'),duration);
