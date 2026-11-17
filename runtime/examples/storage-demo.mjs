import assert from'node:assert/strict';import{mkdtempSync}from'node:fs';import{tmpdir}from'node:os';import{join}from'node:path';import{DatabaseSync}from'node:sqlite';
import{MockLoop}from'../src/mock-loop.mjs';import{MockDriver}from'../src/mock-driver.mjs';import{manifest}from'./mock-config.mjs';
const directory=mkdtempSync(join(tmpdir(),'ltp-storage-demo-'));
const policy={segment_max_bytes:1200,segment_max_age_ms:1000,retention_ms:2000};
const loop=new MockLoop({directory,manifest:manifest(),log_policy:policy});
const driver=new MockDriver(manifest(),{wall_origin_ms:10000});driver.on('sample',s=>loop.accept(s));
let before,after,missing,pruned;
try{
  driver.connect();driver.advance(100);
  for(let i=1;i<=6;i++){driver.setValue('temperature',5+i*2);driver.advance(100);}
  loop.checkpoint({observed_at_ms:11000,monotonic_ns:'1000000000'});
  before=loop.queryLogs({from_ms:10100,to_ms:11000,limit:1});
  missing=loop.queryLogs({from_ms:10100,to_ms:11000,text:'不存在的故障'});
  assert.equal(before.status,'complete');assert.equal(missing.matched_count,0);assert.equal(missing.status,'complete');
  assert.ok(before.matched_count>before.items.length);
  loop.checkpoint({observed_at_ms:14000,monotonic_ns:'4000000000'});pruned=loop.pruneLogs();
  after=loop.queryLogs({from_ms:10100,to_ms:11000});assert.ok(after.gaps.some(g=>g.reason==='rotated'));
}finally{loop.close();}
const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
try{
  console.log(JSON.stringify({mode:'synthetic-storage-policy',directory,policy,
    before:{matched_count:before.matched_count,returned:before.items.length,status:before.status,time_window_actually_covered:before.time_window_actually_covered},
    zero_match:{matched_count:missing.matched_count,status:missing.status},
    after:{matched_count:after.matched_count,status:after.status,time_window_actually_covered:after.time_window_actually_covered,gaps:after.gaps},
    deleted_segments:pruned.deleted_segments,segments:db.prepare('SELECT state,count(*) count FROM log_segments GROUP BY state').all(),
    integrity_check:db.prepare('PRAGMA integrity_check').get().integrity_check,
    limits:['2秒保留期仅合成验证，正常默认90天','保留期按可验证单调运行时长累计，停机时间不计入，保守多保留','不是真实电源故障或Windows验收']},null,2));
}finally{db.close();}
