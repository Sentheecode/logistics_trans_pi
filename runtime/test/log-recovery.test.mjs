import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, appendFileSync, rmSync,writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { DatabaseSync } from 'node:sqlite';
import { MockDriver } from '../src/mock-driver.mjs';
import { MockLoop } from '../src/mock-loop.mjs';
import { manifest } from './fixture.mjs';

test('索引未提交的完整尾行恢复为唯一记录，重复启动不再次追加',()=>{
  const directory=mkdtempSync(join(tmpdir(),'ltp-recover-index-'));
  let loop;
  try {
    loop=new MockLoop({directory,manifest:manifest()});
    const db=new DatabaseSync(join(directory,'runtime.sqlite'));
    db.exec("CREATE TRIGGER fail_index BEFORE INSERT ON log_index BEGIN SELECT RAISE(ABORT,'crash window'); END;");db.close();
    const d=new MockDriver(manifest());d.on('sample',s=>loop.accept(s));d.connect();
    assert.throws(()=>d.advance(100));assert.throws(()=>loop.close());loop=null;
    const fix=new DatabaseSync(join(directory,'runtime.sqlite'));fix.exec('DROP TRIGGER fail_index');fix.close();
    for(let i=0;i<2;i++){loop=new MockLoop({directory,manifest:manifest()});loop.close();loop=null;}
    const verify=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    try {
      assert.equal(verify.prepare('SELECT count(*) n FROM log_outbox').get().n,0);
      assert.equal(verify.prepare('SELECT count(*) n FROM log_index').get().n,1);
      assert.equal(readFileSync(join(directory,'events.jsonl'),'utf8').trim().split('\n').length,1);
    } finally {verify.close();}
  } finally {if(loop)loop.close();rmSync(directory,{recursive:true,force:true});}
});

test('未确认的半行先保存恢复副本再截断，已确认前缀保持原样',()=>{
  const directory=mkdtempSync(join(tmpdir(),'ltp-recover-partial-'));
  try {
    let loop=new MockLoop({directory,manifest:manifest()});const d=new MockDriver(manifest());
    d.on('sample',s=>loop.accept(s));d.connect();d.advance(100);loop.close();
    const before=readFileSync(join(directory,'events.jsonl'));
    appendFileSync(join(directory,'events.jsonl'),'{"stamp":');
    loop=new MockLoop({directory,manifest:manifest()});loop.close();
    assert.deepEqual(readFileSync(join(directory,'events.jsonl')),before);
    const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    try {
      const action=db.prepare("SELECT * FROM log_recovery_actions WHERE action='tail_trim'").get();
      assert.ok(action);assert.equal(readFileSync(join(directory,action.backup_path),'utf8'),'{"stamp":');
    } finally {db.close();}
  } finally {rmSync(directory,{recursive:true,force:true});}
});

test('未知完整尾行不能冒充未确认半行被静默删除',()=>{
  const directory=mkdtempSync(join(tmpdir(),'ltp-recover-unknown-'));
  try {
    const loop=new MockLoop({directory,manifest:manifest()});loop.close();
    appendFileSync(join(directory,'events.jsonl'),'{"stamp":{"sequence":"99999"}}\n');
    const bytes=readFileSync(join(directory,'events.jsonl'));
    assert.throws(()=>new MockLoop({directory,manifest:manifest()}));
    assert.deepEqual(readFileSync(join(directory,'events.jsonl')),bytes);
  } finally {rmSync(directory,{recursive:true,force:true});}
});

test('重复尾行后夹带未知完整记录时也不能整段截断',()=>{
  const directory=mkdtempSync(join(tmpdir(),'ltp-recover-mixed-'));
  try{
    const loop=new MockLoop({directory,manifest:manifest()});const d=new MockDriver(manifest());d.on('sample',s=>loop.accept(s));d.connect();d.advance(100);loop.close();
    const path=join(directory,'events.jsonl'),line=readFileSync(path,'utf8').split('\n')[0];
    appendFileSync(path,line+'\n'+JSON.stringify({stamp:{sequence:'99999'}})+'\n');
    const before=readFileSync(path);assert.throws(()=>new MockLoop({directory,manifest:manifest()}));assert.deepEqual(readFileSync(path),before);
  }finally{rmSync(directory,{recursive:true,force:true});}
});
test('已确认记录被改动时拒绝覆盖，保留损坏证据',()=>{
  const directory=mkdtempSync(join(tmpdir(),'ltp-recover-corrupt-'));
  try{
    const loop=new MockLoop({directory,manifest:manifest()});const d=new MockDriver(manifest());d.on('sample',s=>loop.accept(s));d.connect();d.advance(100);loop.close();
    const path=join(directory,'events.jsonl');writeFileSync(path,readFileSync(path,'utf8').replace('模拟点位','篡改点位'));
    const before=readFileSync(path);assert.throws(()=>new MockLoop({directory,manifest:manifest()}));assert.deepEqual(readFileSync(path),before);
  }finally{rmSync(directory,{recursive:true,force:true});}
});
