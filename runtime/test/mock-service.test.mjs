import test from'node:test';import assert from'node:assert/strict';
import{mkdtempSync,rmSync}from'node:fs';import{join}from'node:path';import{tmpdir}from'node:os';
import{DatabaseSync}from'node:sqlite';import{MockService}from'../src/mock-service.mjs';import{manifest}from'./fixture.mjs';
test('真实定时mock服务独立运行，健康摘要和分组数据落盘，停止后释放写入租约',{timeout:5000},async()=>{
  const directory=mkdtempSync(join(tmpdir(),'ltp-service-test-'));
  const service=new MockService({directory,manifest:manifest()});
  try{
    await service.start();await new Promise(resolve=>setTimeout(resolve,1150));
    assert.equal(service.status().persistence,'healthy');assert.equal(service.status().fault,null);
    assert.ok(service.status().groups.find(g=>g.group_id==='fast').runs>=5);
    await service.stop();
    const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    try{
      assert.ok(db.prepare("SELECT count(*) n FROM history_raw WHERE tag_id='heartbeat'").get().n>=5);
      assert.equal(db.prepare("SELECT count(*) n FROM log_index WHERE event_code='runtime.health'").get().n,1);
      assert.equal(db.prepare('SELECT clean_shutdown FROM runtime_boots').get().clean_shutdown,1);
    }finally{db.close();}
  }finally{await service.stop();rmSync(directory,{recursive:true,force:true});}
});
