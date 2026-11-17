// 仅测试子进程：只操作父测试传入的新临时目录。
import{DatabaseSync}from'node:sqlite';import{appendFileSync,readFileSync}from'node:fs';import{join}from'node:path';
import{MockLoop}from'../../src/mock-loop.mjs';import{MockDriver}from'../../src/mock-driver.mjs';
import{Watchdog}from'../../src/watchdog.mjs';import{manifest}from'../fixture.mjs';
const[directory,mode]=process.argv.slice(2),policy=mode==='deleting'?{segment_max_bytes:700}:{};
const loop=new MockLoop({directory,manifest:manifest(),log_policy:policy});
const driver=new MockDriver(manifest());driver.on('sample',s=>loop.accept(s));
if(mode==='after-fsync') {
  const db=new DatabaseSync(join(directory,'runtime.sqlite'));db.exec("CREATE TRIGGER crash_index BEFORE INSERT ON log_index BEGIN SELECT RAISE(ABORT,'test crash window'); END;");db.close();
  driver.connect();try{driver.advance(100);}catch(error){if(!error.message.includes('test crash window'))throw error;}
} else {
  driver.connect();driver.advance(100);
  if(['pending','partial','duplicate'].includes(mode)) {
    const db=new DatabaseSync(join(directory,'runtime.sqlite'));db.exec('BEGIN IMMEDIATE');
    const q=db.prepare("UPDATE sequence_counters SET high_water=high_water+1 WHERE name='record' RETURNING high_water");q.setReadBigInts(true);const seq=q.get().high_water;
    const boot=db.prepare('SELECT boot_id FROM runtime_boots ORDER BY rowid DESC LIMIT 1').get().boot_id;
    const payload=JSON.stringify({stamp:{sequence:String(seq),observed_at:new Date().toISOString(),boot_id:boot,monotonic_ns:'200000000'},
      level:'info',event_code:'crash.injected',message_zh:'用于验证崩溃窗口的合成记录',context:{device:null,tag:null,scan_cycle:null,session_id:null},fields:{mode}});
    db.prepare('INSERT INTO log_outbox VALUES (?,?,?)').run(seq,Date.now()*1000,payload);db.exec('COMMIT');db.close();
    if(mode==='partial')appendFileSync(join(directory,'events.jsonl'),Buffer.from(payload).subarray(0,70));
    if(mode==='duplicate')appendFileSync(join(directory,'events.jsonl'),readFileSync(join(directory,'events.jsonl'),'utf8').split('\n')[0]+'\n');
  }
  if(mode==='deleting') {
    const db=new DatabaseSync(join(directory,'runtime.sqlite'));const row=db.prepare("SELECT segment_id FROM log_segments WHERE state='sealed' LIMIT 1").get();
    if(!row)throw new Error('未产生测试轮转段');
    db.exec('BEGIN IMMEDIATE');db.prepare("UPDATE log_segments SET state='deleting' WHERE segment_id=?").run(row.segment_id);
    db.prepare("UPDATE log_recording_spans SET status='rotated' WHERE segment_id=?").run(row.segment_id);db.exec('COMMIT');db.close();
  }
}
if(mode==='watchdog') {
  const watchdog=new Watchdog({timeout_ms:100,interval_ms:10,terminate_on_timeout:true});await watchdog.ready();watchdog.beat();
  process.send({ready:true,mode});const end=process.hrtime.bigint()+2000000000n;
  while(process.hrtime.bigint()<end){} // 仅故意阻塞此测试子进程，worker须独立终止它。
}else{process.send({ready:true,mode});setInterval(()=>{},1000);}
