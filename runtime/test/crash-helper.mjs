import assert from'node:assert/strict';import{fork}from'node:child_process';
import{mkdtempSync,readFileSync,rmSync}from'node:fs';import{tmpdir}from'node:os';import{join}from'node:path';
import{fileURLToPath}from'node:url';import{DatabaseSync}from'node:sqlite';
import{MockLoop}from'../src/mock-loop.mjs';import{MockDriver}from'../src/mock-driver.mjs';import{manifest}from'./fixture.mjs';

export async function exerciseCrash(mode,{keep=false}={}) {
  const directory=mkdtempSync(join(tmpdir(),`ltp-kill-${mode}-`));
  const child=fork(fileURLToPath(new URL('./fixtures/crash-worker.mjs',import.meta.url)),[directory,mode],{stdio:['ignore','pipe','pipe','ipc']});
  child.stdout.resume();let stderr='',exited=false;child.stderr.on('data',b=>{stderr=(stderr+b).slice(-4096);});
  const exit=new Promise(resolve=>child.once('exit',(code,signal)=>{exited=true;resolve({code,signal});}));
  let timer;
  try {
    await new Promise((resolve,reject)=>{
      timer=setTimeout(()=>reject(new Error('崩溃子进程准备超时：'+stderr)),5000);
      child.once('message',resolve);child.once('error',reject);child.once('exit',()=>reject(new Error('子进程提前退出：'+stderr)));
    });clearTimeout(timer);
    assert.throws(()=>new MockLoop({directory,manifest:manifest()}),'活动子进程的目录不能被第二写入者接管');
    let db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    const before=db.prepare('SELECT count(*) n FROM history_raw').get().n;
    const seqQuery=db.prepare("SELECT high_water FROM sequence_counters WHERE name='record'");seqQuery.setReadBigInts(true);const high=seqQuery.get().high_water;db.close();
    if(mode!=='watchdog')child.kill('SIGKILL');
    const ended=await Promise.race([exit,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('子进程终止超时')),5000);})]);clearTimeout(timer);
    assert.equal(ended.signal,'SIGKILL');
    if(mode==='after-fsync'){db=new DatabaseSync(join(directory,'runtime.sqlite'));db.exec('DROP TRIGGER crash_index');db.close();}
    const policy=mode==='deleting'?{segment_max_bytes:700}:{};
    let loop=new MockLoop({directory,manifest:manifest(),log_policy:policy});
    const driver=new MockDriver(manifest());driver.on('sample',s=>loop.accept(s));driver.connect();driver.advance(100);loop.close();
    // 再启动一次，确认恢复不会重复导出。
    loop=new MockLoop({directory,manifest:manifest(),log_policy:policy});loop.close();
    db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    try {
      assert.equal(db.prepare('SELECT count(*) n FROM history_raw').get().n,before+2);
      assert.equal(db.prepare('SELECT count(*) n FROM log_outbox').get().n,0);
      assert.equal(db.prepare('PRAGMA integrity_check').get().integrity_check,'ok');
      assert.deepEqual(db.prepare('PRAGMA foreign_key_check').all(),[]);
      const query=db.prepare("SELECT high_water FROM sequence_counters WHERE name='record'");query.setReadBigInts(true);const after=query.get().high_water;assert.ok(after>high);
      const records=db.prepare("SELECT relative_path FROM log_segments WHERE state IN ('active','sealed')").all().flatMap(s=>readFileSync(join(directory,s.relative_path),'utf8').trim().split('\n').filter(Boolean).map(JSON.parse));
      assert.equal(new Set(records.map(r=>r.stamp.sequence)).size,records.length);
      assert.equal(db.prepare('SELECT count(*) n FROM log_index').get().n,records.length);
      if(['pending','partial','duplicate'].includes(mode))assert.equal(records.filter(r=>r.event_code==='crash.injected').length,1);
      if(mode==='deleting')assert.ok(db.prepare("SELECT count(*) n FROM log_segments WHERE state='deleted'").get().n>0);
      return{mode,directory,exit_signal:ended.signal,committed_history_before:before,history_after_restart:before+2,
        high_water_before:String(high),high_water_after:String(after),physical_log_records:records.length,
        unique_log_sequences:true,pending_logs:0,integrity_check:'ok',second_writer_rejected:true};
    }finally{db.close();}
  }finally{clearTimeout(timer);if(!exited){child.kill('SIGKILL');await exit;}if(!keep)rmSync(directory,{recursive:true,force:true});}
}
