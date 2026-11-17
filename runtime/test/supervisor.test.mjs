import test from'node:test';import assert from'node:assert/strict';import{once}from'node:events';
import{mkdtempSync,rmSync}from'node:fs';import{join}from'node:path';import{tmpdir}from'node:os';
import{ServiceSupervisor}from'../src/service-supervisor.mjs';
test('监督进程在自己管理的runtime被杀后自动重启，停止时不再拉起',{timeout:8000,skip:process.platform!=='linux'},async()=>{
  const directory=mkdtempSync(join(tmpdir(),'ltp-supervisor-'));
  const supervisor=new ServiceSupervisor({directory,restart_delay_ms:20});
  try{
    await supervisor.start();const first=supervisor.status().child_pid;
    const ready=once(supervisor,'ready');process.kill(first,'SIGKILL');await ready;
    assert.notEqual(supervisor.status().child_pid,first);assert.equal(supervisor.status().restarts,1);
    await supervisor.stop();await new Promise(r=>setTimeout(r,50));assert.equal(supervisor.status().child_pid,null);
  }finally{await supervisor.stop();rmSync(directory,{recursive:true,force:true});}
});
test('连续故障超过重启预算时显式失败，不无限重启',{timeout:8000,skip:process.platform!=='linux'},async()=>{
  const directory=mkdtempSync(join(tmpdir(),'ltp-supervisor-budget-'));
  const supervisor=new ServiceSupervisor({directory,restart_delay_ms:10,max_restarts:1});
  try{
    await supervisor.start();let ready=once(supervisor,'ready');process.kill(supervisor.status().child_pid,'SIGKILL');await ready;
    const failed=once(supervisor,'fault');process.kill(supervisor.status().child_pid,'SIGKILL');
    const[error]=await failed;assert.match(error.message,/重启预算/);assert.equal(supervisor.status().running,false);
  }finally{await supervisor.stop();rmSync(directory,{recursive:true,force:true});}
});
test('启动过程中取消会拒绝就绪等待，不留下悬空promise或子进程',{timeout:8000},async()=>{
  const directory=mkdtempSync(join(tmpdir(),'ltp-supervisor-cancel-'));
  const supervisor=new ServiceSupervisor({directory});
  try{const starting=supervisor.start();const rejected=assert.rejects(starting,/取消/);await supervisor.stop();await rejected;assert.equal(supervisor.status().child_pid,null);}
  finally{await supervisor.stop();rmSync(directory,{recursive:true,force:true});}
});
