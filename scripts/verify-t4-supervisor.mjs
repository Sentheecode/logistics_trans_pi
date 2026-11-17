import{once}from'node:events';import{mkdtempSync}from'node:fs';import{tmpdir}from'node:os';import{join}from'node:path';import{DatabaseSync}from'node:sqlite';
import{ServiceSupervisor}from'../runtime/src/service-supervisor.mjs';
const directory=mkdtempSync(join(tmpdir(),'ltp-supervisor-proof-'));
const supervisor=new ServiceSupervisor({directory,restart_delay_ms:50});let before,first,second;
try{
  await supervisor.start();first=supervisor.status().child_pid;await new Promise(r=>setTimeout(r,600));
  let db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});before=db.prepare('SELECT count(*) n FROM history_raw').get().n;db.close();
  const ready=once(supervisor,'ready');process.kill(first,'SIGKILL');await ready;second=supervisor.status().child_pid;await new Promise(r=>setTimeout(r,600));
}finally{await supervisor.stop();}
const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
try{
  const after=db.prepare('SELECT count(*) n FROM history_raw').get().n;
  if(after<=before||first===second||supervisor.status().restarts!==1)throw new Error('自动重启验证失败');
  console.log(JSON.stringify({directory,first_child_pid:first,second_child_pid:second,history_before_kill:before,history_after_restart:after,
    supervisor:supervisor.status(),boots:db.prepare('SELECT clean_shutdown,count(*) count FROM runtime_boots GROUP BY clean_shutdown').all(),
    integrity_check:db.prepare('PRAGMA integrity_check').get().integrity_check,limit:'仅验证本监督进程管理的mock子进程；不是Windows服务安装或物理掉电'},null,2));
}finally{db.close();}
