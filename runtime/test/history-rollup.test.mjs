import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {DatabaseSync} from 'node:sqlite';
import {AcquisitionStore} from '../src/acquisition-store.mjs';
import {startDiagnosticApi} from '../src/diagnostic-api.mjs';
import {manifest} from '../examples/mock-config.mjs';

test('100ms样本跨秒、迟到、坏质量与重启后分桶，可经历史API检索',async()=>{
  const directory=await mkdtemp(join(tmpdir(),'ltp-rollup-'));
  const options={directory,manifest:manifest()};
  let store=new AcquisitionStore(options),api,db;
  const at=Date.parse('2026-01-01T00:00:00Z');
  function ingest(i,offset,level='good'){
    const observed=at+i*100;
    store.accept({device_id:'mock-only',group_id:'fast',tag_id:'temperature',scan_cycle:String(i),
      session_id:'test',observed_at_ms:observed,source_at_ms:at+offset,server_at_ms:null,
      event_at_ms:at+offset,timestamp_basis:'source',monotonic_ns:String(i*100000000),
      quality:{level,reason:'test'},value:{type:'number',value:i}});
  }
  try{
    for(let i=0;i<11;i++)ingest(i,i*100,i===2?'bad':i===3?'uncertain':'good');
    ingest(11,850); // 11号样本迟于下一秒到达，进入第一秒的新时钟段。
    const token='x'.repeat(32);
    api=await startDiagnosticApi(directory,token);
    for(const resolution of ['1s','1m','1h']){
      const u=new URL('/api/v1/history',api.url);
      u.search=new URLSearchParams({resolution,tag_ids:'temperature',from:new Date(at).toISOString(),to:new Date(at+3600000).toISOString()});
      const response=await fetch(u,{headers:{authorization:'Bearer '+token}});
      assert.equal(response.status,200);
      const {items,backfill_pending}=await response.json();
      assert.equal(backfill_pending,false);
      assert.equal(items.length,resolution==='1s'?3:2);
      const bucket=items[0],second=resolution==='1s';
      assert.equal(bucket.count,second?10:11);assert.equal(bucket.good_count,second?8:9);
      assert.equal(bucket.bad_count,1);assert.equal(bucket.uncertain_count,1);
      assert.equal(bucket.min,0);assert.equal(bucket.max,second?9:10);
      assert.equal(bucket.mean,second?40/8:50/9);
      assert.equal(bucket.first.value.value,0);assert.equal(bucket.last.value.value,second?9:10);
      const late=items.find(b=>b.clock_epoch!==bucket.clock_epoch);
      assert.equal(late.count,1);assert.equal(late.mean,11);assert.equal(late.window.from,bucket.window.from);
    }
    await api.close();api=null;store.close();store=new AcquisitionStore(options);
    db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
    assert.equal(db.prepare('SELECT sum(sample_count) n FROM history_aggregates').get().n,36);
    assert.equal(db.prepare('SELECT count(*) n FROM history_raw').get().n,12);
  }finally{await api?.close();db?.close();store.close();await rm(directory,{recursive:true,force:true});}
});

test('v3已有历史分批补算，跨进程重启从已提交序号继续',async()=>{
  const directory=await mkdtemp(join(tmpdir(),'ltp-backfill-'));
  const options={directory,manifest:manifest()};
  let store=new AcquisitionStore(options);
  const file=join(directory,'runtime.sqlite');
  try{
    for(let i=0;i<35;i++)store.accept({tag_id:'heartbeat',device_id:'mock-only',group_id:'fast',
      value:{type:'boolean',value:i%2===0},quality:{level:'good',reason:'test'},scan_cycle:String(i),session_id:'s',
      monotonic_ns:String(i*100000000),observed_at_ms:i*100,event_at_ms:i*100,source_at_ms:null,
      server_at_ms:null,timestamp_basis:'observed'});
    store.close();
    const legacy=new DatabaseSync(file);
    legacy.exec('DROP INDEX history_bucket_lookup; DROP TABLE history_rollup_progress; DELETE FROM schema_migrations WHERE version=4; DELETE FROM history_aggregates;');
    legacy.close();
    for(const expected of [16,32,35]){
      store=new AcquisitionStore(options);store.checkpoint(store.now());store.close();
      const read=new DatabaseSync(file,{readOnly:true});
      try{
        const b=read.prepare('SELECT sum(sample_count) n,sum(numeric_good_count) numeric,max(numeric_max) maximum FROM history_aggregates WHERE resolution_ms=3600000').get();
        assert.equal(b.n,expected);assert.equal(b.numeric,0);assert.equal(b.maximum,null);
      }finally{read.close();}
    }
  }finally{store.close();await rm(directory,{recursive:true,force:true});}
});
