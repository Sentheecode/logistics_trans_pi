import {EventEmitter} from 'node:events';
import {randomUUID} from 'node:crypto';
import {statSync} from 'node:fs';
import {isAbsolute} from 'node:path';
import {OPCUAClient,OPCUACertificateManager,MessageSecurityMode,SecurityPolicy,UserTokenType,
 AttributeIds,TimestampsToReturn,makeNodeId,DataChangeFilter,DataChangeTrigger,DeadbandType} from 'node-opcua';
import {convertDataValue} from './data-value.mjs';
import {classifyOpcuaError} from './diagnostics.mjs';

function validate(config){
 const u=new URL(config.endpoint);
 if(u.username||u.password)throw new Error('Endpoint不得嵌入凭据');
 if(u.protocol!=='opc.tcp:')throw new Error('仅支持opc.tcp Endpoint');
 const mode=config.security_mode??'SignAndEncrypt';
 if(!['None','SignAndEncrypt'].includes(mode))throw new Error('未知安全模式');
 if(mode==='None'&&!(config.allow_insecure_loopback===true&&['127.0.0.1','localhost','[::1]'].includes(u.hostname)))throw new Error('Security None仅允许显式回环测试');
 if(!config.pki_directory||!isAbsolute(config.pki_directory))throw new Error('PKI目录必须是绝对路径');
 if(mode!=='None'){
  if(!config.application_uri||!config.certificate_file||!config.private_key_file)throw new Error('安全连接必须配置ApplicationUri、客户端证书和私钥');
  for(const path of [config.certificate_file,config.private_key_file])if(!isAbsolute(path)||!statSync(path).isFile())throw new Error('证书和私钥必须是已有的绝对文件路径');
 }
 const tags=config.manifest?.groups?.flatMap(g=>g.tags)??[];
 if(!config.heartbeat_tag_id||!tags.some(t=>t.id===config.heartbeat_tag_id))throw new Error('必须配置清单内的心跳点位');
 const ids=new Set(),groups=new Set();
 for(const g of config.manifest.groups){
  if(groups.has(g.id)||!Number.isFinite(g.interval_ms)||g.interval_ms<20)throw new Error('无效或重复的采集组');groups.add(g.id);
  for(const t of g.tags){
   if(ids.has(t.id))throw new Error('重复点位');ids.add(t.id);
   if(!['number','boolean','string','int64','uint64'].includes(t.type))throw new Error('未知点位类型');
   const b=config.bindings?.[t.id];
   if(!b?.namespace_uri||!(typeof b.identifier==='string'||(Number.isSafeInteger(b.identifier)&&b.identifier>=0)))throw new Error('点位必须配置NamespaceUri和标识符');
   const d=t.deadband;
   if(!d||!['none','absolute','percent'].includes(d.mode)||!Number.isFinite(d.value)||d.value<0||(d.mode==='none'&&d.value!==0)||(d.mode==='percent'&&d.value>100))throw new Error('无效死区');
   if(t.type!=='number'&&d.mode!=='none')throw new Error('布尔/文本/64位精确值必须使用无死区');
   if(t.id===config.heartbeat_tag_id&&d.mode!=='none')throw new Error('心跳不能配置死区');
  }
 }
 const timeout=config.heartbeat_timeout_ms??10000;
 if(!Number.isFinite(timeout)||timeout<200)throw new Error('无效心跳超时');
 if(!Number.isFinite(config.reconnect_delay_ms??1000)||(config.reconnect_delay_ms??1000)<50)throw new Error('无效重连间隔');
}

/** 只读适配层。协议对象与凭据从不进入snapshot或sample。 */
export class OpcuaDriver extends EventEmitter {
 #config;#state={link:'down',session:'absent',subscription:'absent'};#client;#session;#manager;
 #subscriptions=[];#details=[];#generation=0;#sessionId=null;#chain=Promise.resolve();#timer;#retry;
 #closing=true;#recovering=false;#hearts=new Map();#ready=false;#generationStart=0n;#scan=0n;#invalidated=false;
 constructor(config){super();validate(config);const {get_credentials,...plain}=config;this.#config=structuredClone(plain);this.#config.get_credentials=get_credentials;}
 #set(patch){const changed=Object.entries(patch).some(([key,value])=>this.#state[key]!==value);Object.assign(this.#state,patch);if(changed)this.emit('state',{...this.#state});}
 #diagnostic(d){this.emit('diagnostic',{...d,device_id:this.#config.manifest.device_id,observed_at_ms:Date.now()});}
 #serialize(fn){const p=this.#chain.then(fn);this.#chain=p.catch(()=>{});return p;}
 snapshot(){return structuredClone({connection:this.#state,manifest:this.#config.manifest,generation:this.#generation,session_id:this.#sessionId,subscriptions:this.#details});}
 async connect(){
  if(!this.#closing)return;this.#closing=false;
  try{await this.#serialize(()=>this.#open());}catch(error){this.#diagnostic(classifyOpcuaError(error));await this.disconnect();throw new Error('OPC UA连接失败，详见结构化诊断');}
  if(!this.#closing)this.#timer=setInterval(()=>this.#check(),Math.min(200,this.#timeout()/4));
 }
 #timeout(){return this.#config.heartbeat_timeout_ms??10000;}
 async #open(){
  if(this.#closing)return;
  const c=this.#config;
  if(!this.#manager){this.#manager=new OPCUACertificateManager({rootFolder:c.pki_directory,automaticallyAcceptUnknownCertificate:false});await this.#manager.initialize();}
  const client=OPCUAClient.create({applicationName:'LTP read-only acquisition',...(c.application_uri?{applicationUri:c.application_uri}:{}),clientCertificateManager:this.#manager,
   securityMode:c.security_mode==='None'?MessageSecurityMode.None:MessageSecurityMode.SignAndEncrypt,
   securityPolicy:c.security_mode==='None'?SecurityPolicy.None:SecurityPolicy.Basic256Sha256,
   ...(c.certificate_file?{certificateFile:c.certificate_file,privateKeyFile:c.private_key_file}:{}),
   connectionStrategy:{maxRetry:0,initialDelay:100,maxDelay:100},keepSessionAlive:true,
   transportTimeout:5000,defaultTransactionTimeout:5000,endpointMustExist:true});
  this.#client=client;this.#set({link:'connecting',session:'absent',subscription:'absent'});
  client.on('connection_lost',()=>{if(this.#client===client)this.#lost();});
  client.on('close',()=>{if(this.#client===client&&!this.#closing)this.#lost();});
  await client.connect(c.endpoint);if(this.#closing)return;this.#set({link:'up',session:'creating'});
  const secret=await c.get_credentials?.();
  const session=await client.createSession(secret?{type:UserTokenType.UserName,userName:secret.username,password:secret.password}:{type:UserTokenType.Anonymous});
  this.#session=session;this.#sessionId=randomUUID();this.#set({session:'active'});
  session.on('session_closed',()=>{if(this.#session===session&&!this.#closing)this.#lost();});
  await this.#build();
 }
 #invalidate(reason){
  if(this.#invalidated)return;this.#invalidated=true;
  const at=Date.now();
  for(const group of this.#config.manifest.groups)for(const tag of group.tags)this.emit('sample',{
   tag_id:tag.id,device_id:this.#config.manifest.device_id,group_id:group.id,value:{type:'unavailable',value:null},
   quality:{level:'bad',reason},source_at_ms:null,server_at_ms:null,observed_at_ms:at,event_at_ms:at,timestamp_basis:'observed',
   monotonic_ns:String(process.hrtime.bigint()),scan_cycle:String(++this.#scan),session_id:this.#sessionId});
 }
 #lost(){
  if(!this.#closing)this.#invalidate('communication_lost');
  this.#set({link:'down',session:'absent',subscription:'stale'});
  if(this.#closing||this.#retry||this.#recovering)return;
  this.#retry=setTimeout(()=>{this.#retry=null;this.#recovering=true;
   this.#serialize(async()=>{await this.#cleanup();if(!this.#closing)await this.#open();})
    .catch(e=>this.#diagnostic(classifyOpcuaError(e))).finally(()=>{this.#recovering=false;if(!this.#closing&&this.#state.session!=='active')this.#lost();});
  },this.#config.reconnect_delay_ms??1000);
 }
 async #build(){
  this.#generation++;const generation=this.#generation;this.#generationStart=process.hrtime.bigint();this.#ready=false;
  this.#set({subscription:'rebuilding'});
  const old=this.#subscriptions;this.#subscriptions=[];this.#details=[];
  for(const s of old)await s.terminate().catch(()=>{});
  if(this.#closing)return;
  const namespaces=await this.#session.readNamespaceArray();
  for(const group of this.#config.manifest.groups){
   const sub=await this.#session.createSubscription2({requestedPublishingInterval:group.interval_ms,requestedLifetimeCount:60,requestedMaxKeepAliveCount:5,maxNotificationsPerPublish:1000,publishingEnabled:true,priority:0});
   this.#subscriptions.push(sub);
   for(const event of ['internal_error','error'])sub.on(event,()=>{if(generation===this.#generation&&!this.#closing)this.#set({subscription:'stale'});});
   const detail={group_id:group.id,subscription_id:sub.subscriptionId,requested_interval_ms:group.interval_ms,revised_interval_ms:sub.publishingInterval,items:[]};
   this.#details.push(detail);
   const heartbeat=this.#config.manifest.groups.flatMap(g=>g.tags).find(t=>t.id===this.#config.heartbeat_tag_id);
   const tags=group.tags.some(t=>t.id===heartbeat.id)?group.tags:[...group.tags,heartbeat];
   for(const tag of tags){
    const auxiliary=!group.tags.some(t=>t.id===tag.id);
    const binding=this.#config.bindings[tag.id],index=namespaces.indexOf(binding.namespace_uri);
    if(index<0)throw new Error('NamespaceUri未由服务端提供');
    const d=tag.deadband;
    const filter=new DataChangeFilter({trigger:DataChangeTrigger.StatusValue,deadbandType:d.mode==='absolute'?DeadbandType.Absolute:d.mode==='percent'?DeadbandType.Percent:DeadbandType.None,deadbandValue:d.value});
    const item=await sub.monitor({nodeId:makeNodeId(binding.identifier,index),attributeId:AttributeIds.Value},{samplingInterval:group.interval_ms,queueSize:10,discardOldest:true,filter},TimestampsToReturn.Both);
    if(!item.statusCode.isGood())throw new Error('MonitoredItem创建失败');
    detail.items.push({tag_id:tag.id,auxiliary_heartbeat:auxiliary,requested_sampling_ms:group.interval_ms,revised_sampling_ms:item.monitoringParameters.samplingInterval,queue_size:item.monitoringParameters.queueSize});
    item.on('err',()=>{if(generation===this.#generation&&!this.#closing)this.#set({subscription:'stale'});});
    item.on('changed',value=>{
     if(this.#closing||generation!==this.#generation||this.#state.session!=='active')return;
     const sample={...convertDataValue(tag,value,Date.now()),tag_id:tag.id,device_id:this.#config.manifest.device_id,group_id:group.id,
      monotonic_ns:String(process.hrtime.bigint()),scan_cycle:String(++this.#scan),session_id:this.#sessionId};
     if(tag.id===this.#config.heartbeat_tag_id&&sample.quality.level==='good'){
      const fingerprint=JSON.stringify(sample.value),previous=this.#hearts.get(group.id);
      if(fingerprint!==previous?.value)this.#hearts.set(group.id,{value:fingerprint,progress:process.hrtime.bigint()});
     }
     if(!auxiliary)this.emit('sample',sample);
    });
   }
  }
  this.#ready=true;this.#invalidated=false;
 }
 #check(){
  if(this.#closing||this.#recovering||!this.#ready||this.#state.session!=='active')return;
  const now=process.hrtime.bigint(),progress=this.#config.manifest.groups.map(g=>this.#hearts.get(g.id)?.progress??0n);
  const timedOut=progress.some(p=>Number(now-(p>this.#generationStart?p:this.#generationStart))/1e6>this.#timeout());
  if(timedOut||this.#state.subscription==='stale'){
   this.#invalidate('stale');
   this.#set({subscription:'stale'});
   this.#diagnostic({code:'OPCUA_HEARTBEAT_STALE',message_zh:'心跳未取得进展或订阅报告异常；证据不足以区分PLC停止与订阅失活，主动按清单重建',generation:this.#generation});
   this.rebuildSubscriptions().catch(()=>{});
  }else if(progress.every(p=>p>this.#generationStart))this.#set({subscription:'healthy'});
 }
 async rebuildSubscriptions(){
  if(this.#closing||this.#recovering)return;this.#recovering=true;
  try{await this.#serialize(()=>this.#build());}
  catch(error){this.#diagnostic(classifyOpcuaError(error));this.#set({session:'absent',subscription:'stale'});}
  finally{this.#recovering=false;if(!this.#closing&&this.#state.session!=='active')this.#lost();}
 }
 async #cleanup(){
  this.#generation++;const subs=this.#subscriptions;this.#subscriptions=[];this.#details=[];
  const session=this.#session,client=this.#client;this.#session=null;this.#client=null;
  for(const s of subs)await s.terminate().catch(()=>{});
  await session?.close().catch(()=>{});await client?.disconnect().catch(()=>{});
  this.#set({link:'down',session:'absent',subscription:'absent'});
 }
 async disconnect(){
  this.#closing=true;clearInterval(this.#timer);clearTimeout(this.#retry);this.#timer=null;this.#retry=null;
  await this.#serialize(async()=>{await this.#cleanup();await this.#manager?.dispose();this.#manager=null;});
 }
}
