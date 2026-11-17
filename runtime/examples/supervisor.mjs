import{mkdtempSync}from'node:fs';import{tmpdir}from'node:os';import{join}from'node:path';
import{ServiceSupervisor}from'../src/service-supervisor.mjs';
const args=process.argv.slice(2),index=args.indexOf('--directory');
const directory=index<0?mkdtempSync(join(tmpdir(),'ltp-supervised-')):args[index+1];
const supervisor=new ServiceSupervisor({directory});let stopped=false;
supervisor.on('ready',state=>console.log(JSON.stringify({event:'supervisor.ready',directory,...state})));
supervisor.on('exit',state=>console.log(JSON.stringify({event:'supervisor.child_exit',...state})));
supervisor.on('output',output=>(output.stream==='stderr'?process.stderr:process.stdout).write(output.text));
supervisor.on('fault',error=>{console.error(JSON.stringify({event:'supervisor.fault',message:error.message}));process.exitCode=1;});
async function stop(){if(stopped)return;stopped=true;await supervisor.stop();console.log(JSON.stringify({event:'supervisor.stopped',directory}));}
process.once('SIGINT',()=>void stop());process.once('SIGTERM',()=>void stop());await supervisor.start();
