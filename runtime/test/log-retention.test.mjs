import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,unlinkSync}from'node:fs';
import {tmpdir}from'node:os';
import {join}from'node:path';
import {DatabaseSync}from'node:sqlite';
import {MockLoop}from'../src/mock-loop.mjs';
import {MockDriver}from'../src/mock-driver.mjs';
import {manifest}from'./fixture.mjs';

const setup=(policy={})=>{
  const directory=mkdtempSync(join(tmpdir(),'ltp-log-retain-'));
  const loop=new MockLoop({directory,manifest:manifest(),log_policy:policy});
  const driver=new MockDriver(manifest(),{wall_origin_ms:10000});
  driver.on('sample',s=>loop.accept(s));driver.connect();
  return{directory,loop,driver,close(){loop.close();rmSync(directory,{recursive:true,force:true});}};
};
test('零匹配也能返回明确覆盖，matched_count为完整筛选的总数',()=>{
  const h=setup();try{
    h.driver.advance(100);h.loop.checkpoint({observed_at_ms:10200,monotonic_ns:'200000000'});
    const empty=h.loop.queryLogs({from_ms:10100,to_ms:10200,text:'不存在的故障'});
    assert.equal(empty.matched_count,0);assert.equal(empty.status,'complete');assert.equal(empty.time_window_actually_covered.length,1);
    const logs=h.loop.queryLogs({from_ms:10100,to_ms:10200,limit:1});
    assert.equal(logs.matched_count,2);assert.equal(logs.items.length,1);assert.equal(logs.truncated,true);
  }finally{h.close();}
});
test('大小轮转和保留墓碑生效，墙钟前跳不能提前删除保留窗口',()=>{
  const h=setup({segment_max_bytes:700,retention_ms:1000,segment_max_age_ms:10000});
  try{
    h.driver.advance(100);h.driver.setValue('temperature',8);h.driver.advance(100);
    h.loop.checkpoint({observed_at_ms:3600000,monotonic_ns:'300000000'});h.loop.pruneLogs();
    let db=new DatabaseSync(join(h.directory,'runtime.sqlite'),{readOnly:true});
    assert.equal(db.prepare("SELECT count(*) n FROM log_segments WHERE state='deleted'").get().n,0);db.close();
    h.loop.checkpoint({observed_at_ms:3602000,monotonic_ns:'2300000000'});h.loop.pruneLogs();
    const result=h.loop.queryLogs({from_ms:10100,to_ms:10201});
    assert.ok(result.gaps.some(g=>g.reason==='rotated'));
    db=new DatabaseSync(join(h.directory,'runtime.sqlite'),{readOnly:true});
    try{assert.ok(db.prepare("SELECT count(*) n FROM log_segments WHERE state='deleted'").get().n>0);}finally{db.close();}
  }finally{h.close();}
});
test('按运行时长轮转，长时间无检查点的空洞不冒充完整记录',()=>{
  const h=setup({segment_max_age_ms:100,coverage_max_gap_ms:100});try{
    h.driver.advance(100);h.loop.checkpoint({observed_at_ms:10300,monotonic_ns:'300000000'});
    const result=h.loop.queryLogs({from_ms:10100,to_ms:10300});
    assert.notEqual(result.status,'complete');assert.ok(result.gaps.some(g=>g.reason==='not_recorded'));
    const db=new DatabaseSync(join(h.directory,'runtime.sqlite'),{readOnly:true});
    try{assert.ok(db.prepare("SELECT count(*) n FROM log_segments WHERE state='sealed'").get().n>0);}finally{db.close();}
  }finally{h.close();}
});

test('正文缺失时返回证据错误而非零匹配，并拒绝向已删除文件确认写入',()=>{
  const h=setup();let failed=false;
  try{
    h.driver.advance(100);unlinkSync(join(h.directory,'events.jsonl'));
    assert.throws(()=>h.loop.queryLogs({from_ms:10100,to_ms:10200}),error=>error.code==='LOG_EVIDENCE_CORRUPT');
    assert.equal(h.loop.health().persistence,'degraded');
    h.driver.setValue('temperature',9);assert.throws(()=>h.driver.advance(100));failed=true;
    assert.equal(h.loop.health().persistence,'unavailable');
  }finally{if(failed){assert.throws(()=>h.loop.close());rmSync(h.directory,{recursive:true,force:true});}else h.close();}
});
test('精确计数超预算显式报错，不能把被截断扫描当成总匹配数',()=>{
  const h=setup({max_query_records:1});try{
    h.driver.advance(100);assert.throws(()=>h.loop.queryLogs({from_ms:10100,to_ms:10200}),/精确计数预算/);
  }finally{h.close();}
});
test('低磁盘预留阈值显式降级，但不靠删年轻日志掩盖容量不足',()=>{
  const h=setup({reserve_bytes:Number.MAX_SAFE_INTEGER});try{
    h.driver.advance(100);assert.equal(h.loop.health().persistence,'degraded');assert.equal(h.loop.health().logs.disk_below_reserve,true);
    const result=h.loop.pruneLogs();assert.equal(result.deleted_segments,0);
  }finally{h.close();}
});
