import test from 'node:test';
import assert from 'node:assert/strict';
import {X509Certificate} from 'node:crypto';
import {mkdtemp,rm,readdir,readFile} from 'node:fs/promises';
import {tmpdir,hostname} from 'node:os';
import {join} from 'node:path';
import {OPCUAServer,OPCUACertificateManager,DataType,Variant,RegisterServerMethod} from 'node-opcua';
import {OpcuaDriver} from '../src/adapter.mjs';

test('SignAndEncrypt不自动批准证书，双方显式信任后订阅成功',{timeout:60000},async()=>{
 const dir=await mkdtemp(join(tmpdir(),'ltp-secure-'));
 const clientPki=join(dir,'client-pki'),certificate=join(dir,'client.pem'),uri='urn:ltp:test:secure-client';
 let cm,server,driver,timer;let n=0;
 const diagnostics=[];
 try{
  cm=new OPCUACertificateManager({rootFolder:clientPki,automaticallyAcceptUnknownCertificate:false});await cm.initialize();
  await cm.createSelfSignedCertificate({applicationUri:uri,dns:[hostname()],outputFile:certificate,subject:'/CN=LTP Test',startDate:new Date(Date.now()-60000),validity:365});
  const privateKey=cm.privateKey;
  server=new OPCUAServer({port:0,hostname:'127.0.0.1',alternateHostname:['127.0.0.1','localhost'],registerServerMethod:RegisterServerMethod.HIDDEN,
   securityModes:[3],securityPolicies:['http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256'],allowAnonymous:true,
   serverCertificateManager:new OPCUACertificateManager({rootFolder:join(dir,'server-pki'),automaticallyAcceptUnknownCertificate:false})});
  await server.initialize();const space=server.engine.addressSpace,ns=space.registerNamespace('urn:ltp:secure');
  ns.addVariable({organizedBy:space.rootFolder.objects,nodeId:'s=heart',browseName:'Heart',dataType:DataType.UInt32,minimumSamplingInterval:50,value:{get:()=>new Variant({dataType:DataType.UInt32,value:n})}});
  await server.start();timer=setInterval(()=>n++,80);
  const config={endpoint:server.endpoints[0].endpointDescriptions().find(e=>e.securityMode===3).endpointUrl,pki_directory:clientPki,
   application_uri:uri,certificate_file:certificate,private_key_file:privateKey,heartbeat_tag_id:'heart',heartbeat_timeout_ms:1000,
   manifest:{device_id:'secure-test',groups:[{id:'fast',interval_ms:100,tags:[{id:'heart',type:'number',deadband:{mode:'none',value:0}}]}]},
   bindings:{heart:{namespace_uri:'urn:ltp:secure',identifier:'heart'}}};
  await cm.dispose();cm=null;
  const make=()=>{driver=new OpcuaDriver(config);driver.on('diagnostic',d=>diagnostics.push(d));return driver;};
  await assert.rejects(make().connect(),/连接失败/);assert.ok(diagnostics.some(d=>d.code==='OPCUA_CERTIFICATE_UNTRUSTED'));
  assert.ok((await readdir(join(clientPki,'rejected'))).length>0);
  cm=new OPCUACertificateManager({rootFolder:clientPki,automaticallyAcceptUnknownCertificate:false});await cm.initialize();
  await cm.trustCertificate(server.getCertificate());await cm.dispose();cm=null;
  await assert.rejects(make().connect(),/连接失败/);
  assert.ok((await readdir(join(dir,'server-pki','rejected'))).length>0);
  // 仅测试夹具显式授权双方证书，不属于生产驱动行为。
  await server.serverCertificateManager.trustCertificate(new X509Certificate(await readFile(certificate)).raw);
  await make().connect();const start=Date.now();while(driver.snapshot().connection.subscription!=='healthy'){
   if(Date.now()-start>10000)throw new Error('安全订阅超时');await new Promise(r=>setTimeout(r,30));
  }
  assert.equal(driver.snapshot().connection.session,'active');
 }finally{clearInterval(timer);await driver?.disconnect();await cm?.dispose();await server?.shutdown(0);await rm(dir,{recursive:true,force:true});}
});
