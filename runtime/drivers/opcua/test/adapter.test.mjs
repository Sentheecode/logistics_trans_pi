import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {OPCUAServer,OPCUACertificateManager,DataType,Variant,RegisterServerMethod} from 'node-opcua';
import {OpcuaDriver} from '../src/adapter.mjs';
async function until(check,ms=15000){const start=Date.now();while(!check()){if(Date.now()-start>ms)throw new Error('等待条件超时');await new Promise(r=>setTimeout(r,30));}}
const manifest={device_id:'local-test',groups:[{id:'fast',interval_ms:100,tags:[{id:'heart',type:'boolean',deadband:{mode:'none',value:0}},{id:'count',type:'number',deadband:{mode:'absolute',value:0.5}}]},{id:'slow',interval_ms:150,tags:[{id:'text',type:'string',deadband:{mode:'none',value:0}}]}]};
const bindings={heart:{namespace_uri:'urn:ltp:test',identifier:'heart'},count:{namespace_uri:'urn:ltp:test',identifier:'count'},text:{namespace_uri:'urn:ltp:test',identifier:'text'}};
test('生产环境拒绝Security None以及缺失心跳配置',()=>{
 assert.throws(()=>new OpcuaDriver({endpoint:'opc.tcp://127.0.0.1:12345',security_mode:'None',manifest,bindings,heartbeat_tag_id:'heart'}),/回环测试/);
 assert.throws(()=>new OpcuaDriver({endpoint:'opc.tcp://127.0.0.1:12345',allow_insecure_loopback:true,security_mode:'None',pki_directory:'/tmp/test-pki',manifest,bindings}),/心跳/);
});
test('真实本地UA订阅、Session假活重建、服务器重启恢复、关闭无重连',{timeout:60000},async(t)=>{
 const dir=await mkdtemp(join(tmpdir(),'ltp-ua-'));let server,driver,timer;let count=0,heart=false,restarts=0,freezeHeart=false;
 async function start(port){
  server=new OPCUAServer({port,hostname:'127.0.0.1',alternateHostname:['localhost'],registerServerMethod:RegisterServerMethod.HIDDEN,
   securityModes:[1],securityPolicies:['http://opcfoundation.org/UA/SecurityPolicy#None'],allowAnonymous:true,
   serverCertificateManager:new OPCUACertificateManager({rootFolder:join(dir,'server-pki'),automaticallyAcceptUnknownCertificate:false})});
  await server.initialize();const space=server.engine.addressSpace;if(restarts++>0)space.registerNamespace('urn:ltp:unrelated');const ns=space.registerNamespace('urn:ltp:test');
  for(const [id,type,get]of [['heart',DataType.Boolean,()=>heart],['count',DataType.Double,()=>count],['text',DataType.String,()=>String(count)]])ns.addVariable({organizedBy:space.rootFolder.objects,nodeId:`s=${id}`,browseName:id,dataType:type,minimumSamplingInterval:50,value:{get:()=>new Variant({dataType:type,value:get()})}});
  await server.start();return server.endpoints[0].endpointDescriptions()[0].endpointUrl;
 }
 try{
  const endpoint=await start(0);const port=server.endpoints[0].port;
  timer=setInterval(()=>{if(!freezeHeart)heart=!heart;count++;},80);
  driver=new OpcuaDriver({endpoint,security_mode:'None',allow_insecure_loopback:true,pki_directory:join(dir,'client-pki'),manifest,bindings,heartbeat_tag_id:'heart',heartbeat_timeout_ms:800,reconnect_delay_ms:100});
  const samples=[],diagnostics=[];driver.on('sample',s=>samples.push(s));driver.on('diagnostic',d=>diagnostics.push(d));
  await driver.connect();await until(()=>driver.snapshot().connection.subscription==='healthy'&&samples.some(s=>s.tag_id==='count'));
  assert.equal(driver.snapshot().connection.session,'active');assert.equal(samples.find(s=>s.tag_id==='count').value.type,'number');
  const session=driver.snapshot().session_id,gen=driver.snapshot().generation;
  // 仅停止不含原始心跳点的慢组，快组仍持续发布。
  server.engine.findSubscription(driver.snapshot().subscriptions.find(g=>g.group_id==='slow').subscription_id).setPublishingMode(false);
  await until(()=>driver.snapshot().generation>gen&&driver.snapshot().connection.subscription==='healthy');
  assert.equal(driver.snapshot().session_id,session);assert.ok(diagnostics.some(d=>d.code==='OPCUA_HEARTBEAT_STALE'));assert.ok(samples.some(s=>s.quality.reason==='stale'&&s.value.type==='unavailable'));
  assert.ok(driver.snapshot().subscriptions.every(g=>g.items.length===2));
  assert.equal(driver.snapshot().subscriptions.find(g=>g.group_id==='slow').items.find(i=>i.tag_id==='heart').auxiliary_heartbeat,true);
  // PLC心跳冻结但其他数据仍变化：重建不能用缓存首值冒充恢复。
  freezeHeart=true;const frozenGeneration=driver.snapshot().generation;
  await until(()=>driver.snapshot().generation>=frozenGeneration+2);
  await new Promise(r=>setTimeout(r,250));assert.notEqual(driver.snapshot().connection.subscription,'healthy');
  freezeHeart=false;await until(()=>driver.snapshot().connection.subscription==='healthy');
  for(let cycle=0;cycle<10;cycle++){
   const previousSession=driver.snapshot().session_id;
   await server.shutdown(0);server=null;await until(()=>driver.snapshot().connection.link!=='up');
   await start(port);await until(()=>driver.snapshot().connection.subscription==='healthy'&&driver.snapshot().session_id!==previousSession);
  }
  await driver.disconnect();const n=samples.length;await new Promise(r=>setTimeout(r,1100));assert.equal(samples.length,n);assert.equal(driver.snapshot().connection.link,'down');
  assert.equal(typeof driver.write,'undefined');assert.doesNotThrow(()=>JSON.stringify(driver.snapshot()));
  t.diagnostic(JSON.stringify({server_restart_cycles:10,namespace_index_changed:true,partial_group_stall_recovered:true,frozen_heartbeat_not_healthy:true,sample_count:samples.length,last_generation:driver.snapshot().generation}));
 }finally{clearInterval(timer);await driver?.disconnect();await server?.shutdown(0);await rm(dir,{recursive:true,force:true});}
});
