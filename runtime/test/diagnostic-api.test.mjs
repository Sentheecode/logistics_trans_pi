import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {randomBytes} from 'node:crypto';
import {MockLoop} from '../src/mock-loop.mjs';
import {MockDriver} from '../src/mock-driver.mjs';
import {manifest,rules} from '../examples/mock-config.mjs';
import {startDiagnosticApi} from '../src/diagnostic-api.mjs';

test('五个只读 HTTP 路由排查一次模拟超温，并区分无匹配和缺失窗口',async()=>{
  const directory=await mkdtemp(join(tmpdir(),'ltp-query-'));
  const store=new MockLoop({directory,manifest:manifest(),rules:rules()});
  const driver=new MockDriver(manifest()), samples=[];
  driver.on('sample',s=>{store.accept(s);samples.push(s);});
  let api;
  try{
    driver.connect();driver.advance(100);driver.setValue('temperature',12);driver.advance(100);
    const token=randomBytes(32).toString('hex');
    api=await startDiagnosticApi(directory,token);
    const call=async(route,{window,...filters})=>{
      const url=new URL('/api/v1/'+route,api.url);
      if(window){url.searchParams.set('from',window.from);url.searchParams.set('to',window.to);}
      for(const [key,value] of Object.entries(filters))
        for(const item of Array.isArray(value)?value:[value])url.searchParams.append(key,String(item));
      const response=await fetch(url,{headers:{Authorization:'Bearer '+token},signal:AbortSignal.timeout(5000)});
      assert.equal(response.status,200);
      return response.json();
    };
    const tags=await call('tags',{tag_ids:['temperature','unknown']});
    assert.equal(tags.items[0].value.value,12);assert.deepEqual(tags.missing_tag_ids,['unknown']);
    const window={from:new Date(samples[0].observed_at_ms).toISOString(),to:new Date(samples.at(-1).observed_at_ms).toISOString()};
    const scope={tag_ids:['temperature'],window};
    assert.ok((await call('history',scope)).items.length);
    assert.equal((await call('alarms',{condition:'active'})).items[0].tag_id,'temperature');
    const logs=await call('logs',{window,text:'no-match'});
    assert.equal(logs.matched_count,0);assert.equal(logs.status,'complete');assert.ok(logs.time_window_actually_covered.length);
    const absent=await call('logs',{window:{from:'2000-01-01T00:00:00Z',to:'2000-01-01T01:00:00Z'}});
    assert.equal(absent.matched_count,0);assert.equal(absent.status,'none');assert.deepEqual(absent.time_window_actually_covered,[]);
    const result=await call('correlations',{...scope,window:{...window,to:new Date(samples.at(-1).observed_at_ms+1).toISOString()},device_ids:['mock-only']});
    assert.deepEqual(new Set(result.evidence.map(r=>r.kind)),new Set(['tag','alarm','log']));
    assert.match(result.conclusion_zh,/证据不足/);
    assert.equal((await fetch(api.url+'/api/v1/logs')).status,401);
    assert.equal((await fetch(api.url+'/api/v1/logs',{method:'POST',headers:{Authorization:'Bearer '+token}})).status,405);
    await api.close();api=null;driver.setValue('temperature',5);driver.advance(100);
    assert.equal(samples.filter(s=>s.tag_id==='temperature').at(-1).value.value,5);
  }finally{await api?.close();store.close();await rm(directory,{recursive:true,force:true});}
});
