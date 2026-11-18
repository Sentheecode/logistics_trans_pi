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
import register from '../../agent/diagnostics.mjs';

test('五个只读工具经HTTP排查一次模拟超温，并区分无匹配和缺失窗口',async()=>{
  const directory=await mkdtemp(join(tmpdir(),'ltp-query-'));
  const store=new MockLoop({directory,manifest:manifest(),rules:rules()});
  const driver=new MockDriver(manifest()), samples=[];
  driver.on('sample',s=>{store.accept(s);samples.push(s);});
  let api;
  try{
    driver.connect();driver.advance(100);driver.setValue('temperature',12);driver.advance(100);
    const token=randomBytes(32).toString('hex');
    api=await startDiagnosticApi(directory,token);
    process.env.LTP_DIAGNOSTIC_URL=api.url;process.env.LTP_DIAGNOSTIC_TOKEN=token;
    const tools=new Map();register({registerTool:t=>tools.set(t.name,t)});
    const call=async(name,args)=>(await tools.get(name).execute('test',args)).details;
    assert.equal(tools.size,5);
    const tags=await call('query_tags',{tag_ids:['temperature','unknown']});
    assert.equal(tags.items[0].value.value,12);assert.deepEqual(tags.missing_tag_ids,['unknown']);
    const window={from:new Date(samples[0].observed_at_ms).toISOString(),to:new Date(samples.at(-1).observed_at_ms).toISOString()};
    const scope={tag_ids:['temperature'],window};
    assert.ok((await call('query_history',scope)).items.length);
    assert.equal((await call('list_alarms',{condition:'active'})).items[0].tag_id,'temperature');
    const logs=await call('search_logs',{window,text:'no-match'});
    assert.equal(logs.matched_count,0);assert.equal(logs.status,'complete');assert.ok(logs.time_window_actually_covered.length);
    const absent=await call('search_logs',{window:{from:'2000-01-01T00:00:00Z',to:'2000-01-01T01:00:00Z'}});
    assert.equal(absent.matched_count,0);assert.equal(absent.status,'none');assert.deepEqual(absent.time_window_actually_covered,[]);
    const result=await call('correlate_time_window',{...scope,window:{...window,to:new Date(samples.at(-1).observed_at_ms+1).toISOString()},device_ids:['mock-only']});
    assert.deepEqual(new Set(result.evidence.map(r=>r.kind)),new Set(['tag','alarm','log']));
    assert.match(result.conclusion_zh,/证据不足/);
    assert.equal((await fetch(api.url+'/api/v1/logs')).status,401);
    assert.equal((await fetch(api.url+'/api/v1/logs',{method:'POST',headers:{Authorization:'Bearer '+token}})).status,405);
    await api.close();api=null;driver.setValue('temperature',5);driver.advance(100);
    assert.equal(samples.filter(s=>s.tag_id==='temperature').at(-1).value.value,5);
  }finally{await api?.close();store.close();await rm(directory,{recursive:true,force:true});}
});
