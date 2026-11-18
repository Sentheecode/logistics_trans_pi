import {createServer} from 'node:http';
import {DatabaseSync} from 'node:sqlite';
import {randomUUID,timingSafeEqual,createHash} from 'node:crypto';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
import {utcUs} from './log-coverage.mjs';
import {readLogWindow} from './log-store.mjs';

const routes = {
  tags:['tag_ids'], history:['tag_ids','from','to','resolution','limit'],
  alarms:['condition','acknowledgement','limit'], logs:['from','to','device','tag','event_code','text','limit'],
  correlations:['from','to','tag_ids','device_ids']
};
const utc = value => value == null ? null : utcUs(Number(value));
const hash = value => createHash('sha256').update(value).digest();
function requireInput(condition,message){if(!condition)throw new TypeError(message);}
function parameters(route,params){
  const args = {limit:200,tag_ids:[],device_ids:[]};
  for(const [key,value] of params){
    requireInput(routes[route].includes(key) && value.length>0 && value.length<=1024,'不支持的查询参数');
    if(key.endsWith('_ids')){
      requireInput(args[key].length<64,'一次最多查询64个标识');args[key].push(value);
    }else{
      requireInput(params.getAll(key).length===1,'查询参数重复');args[key]=value;
    }
  }
  args.limit=Number(args.limit);
  requireInput(Number.isInteger(args.limit)&&args.limit>=1&&args.limit<=500,'limit范围为1至500');
  if(['tags','history'].includes(route))requireInput(args.tag_ids.length,'必须提供tag_ids');
  if(route==='history')requireInput(!args.resolution||args.resolution==='raw','当前仅支持raw历史');
  if(args.condition)requireInput(['active','cleared'].includes(args.condition),'无效报警状态');
  if(args.acknowledgement)requireInput(['acknowledged','unacknowledged'].includes(args.acknowledgement),'无效确认状态');
  if(routes[route].includes('from')){
    for(const key of ['from','to']){
      requireInput(/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,3})?Z$/.test(args[key]),'时间须为UTC，最多毫秒精度');
      args[key+'_ms']=Date.parse(args[key]);
    }
    requireInput(args.to_ms>args.from_ms && args.to_ms-args.from_ms<=86400000,'时间窗须大于零且不超过24小时');
  }
  return args;
}
function stamp(row){return {sequence:String(row.sequence),boot_id:row.boot_id,monotonic_ns:String(row.monotonic_ns),observed_at:utc(row.observed_at_us)};}
function sample(row){return {tag_id:row.tag_id,device_id:row.device_id,scan_group_id:row.group_id,value:JSON.parse(row.value_json),
  quality:{level:row.quality,reason:row.quality_reason,native_code:row.native_code??undefined},source_timestamp:utc(row.source_at_us),
  server_timestamp:utc(row.server_at_us),event_timestamp:utc(row.event_at_us),timestamp_basis:row.timestamp_basis,clock_epoch:Number(row.clock_epoch),stamp:stamp(row)};}
function execute(db,directory,route,args){
  function rows(sql,...values){const q=db.prepare(sql);q.setReadBigInts(true);return q.all(...values);}
  const identity=rows("SELECT runtime_id,high_water FROM runtime_identity,sequence_counters WHERE name='record'")[0];
  const meta={request_id:randomUUID(),runtime_id:identity.runtime_id,as_of_sequence:String(identity.high_water),generated_at:new Date().toISOString()};
  const source=' FROM history_raw h JOIN tags t USING(tag_id) ';
  if(route==='tags'){
    const items=[],missing_tag_ids=[];
    for(const id of new Set(args.tag_ids)){
      const [r]=rows('SELECT h.*,t.device_id,t.group_id'+source+'WHERE tag_id=? ORDER BY sequence DESC LIMIT 1',id);
      if(r)items.push(sample(r));else missing_tag_ids.push(id);
    }
    return {items,missing_tag_ids,meta};
  }
  function page(items){return {items:items.slice(0,args.limit),truncated:items.length>args.limit,meta};}
  if(route==='alarms'){
    const result=rows(`SELECT o.*,r.tag_id,r.severity,s.suppression,s.suppression_until_us,a.sequence,a.boot_id,a.monotonic_ns,a.observed_at_us
      FROM alarm_occurrences o JOIN alarm_rules r USING(rule_id) JOIN alarm_rule_state s USING(rule_id)
      JOIN alarm_transitions a ON a.sequence=coalesce(o.cleared_sequence,o.opened_sequence)
      WHERE (? IS NULL OR o.condition=?) AND (? IS NULL OR o.acknowledgement=?) ORDER BY a.sequence DESC LIMIT ?`,
      args.condition??null,args.condition??null,args.acknowledgement??null,args.acknowledgement??null,args.limit+1);
    return page(result.map(r=>({occurrence_id:r.occurrence_id,rule_id:r.rule_id,tag_id:r.tag_id,revision:String(r.revision),
      severity:Number(r.severity),condition:r.condition,acknowledgement:r.acknowledgement,suppression:r.suppression,
      suppression_until:utc(r.suppression_until_us),stamp:stamp(r),message_zh:'报警规则 '+r.rule_id})));
  }
  const requested={from:args.from,to:args.to};
  const unknown={requested,status:'unknown',time_window_actually_covered:[],gaps:[{window:requested,reason:'unknown'}],retention_policy_revision:'unknown'};
  const bounds=[args.from_ms*1000,args.to_ms*1000];
  const clauses=[],values=[];
  for(const [column,ids] of [['t.tag_id',args.tag_ids],['t.device_id',args.device_ids]]){
    if(ids.length){clauses.push(column+' IN ('+ids.map(()=>'?').join(',')+')');values.push(...ids);}
  }
  const filter=clauses.length?' AND '+clauses.join(' AND '):'';
  function history(column){return rows('SELECT h.*,t.device_id,t.group_id'+source+'WHERE '+column+'>=? AND '+column+'<?'+filter+
    ' ORDER BY h.sequence LIMIT ?',...bounds,...values,args.limit+1).map(sample);}
  if(route==='history')return {...page(history('event_at_us')),coverage:unknown,resolution:'raw'};
  const [policy]=rows("SELECT revision,policy_json FROM retention_policies JOIN log_segments ON revision=policy_revision WHERE state='active'");
  const logs=readLogWindow(db,directory,policy.revision,JSON.parse(policy.policy_json).max_query_records,args);
  if(route==='logs')return {...logs,meta,evidence_note:logs.matched_count && logs.status==='complete'?'有匹配记录':'证据不足以判断根因'};
  const h=page(history('observed_at_us'));
  const alarms=page(rows(`SELECT a.* FROM alarm_transitions a JOIN alarm_rules r USING(rule_id) JOIN tags t USING(tag_id)
    WHERE a.observed_at_us>=? AND a.observed_at_us<?`+filter+' ORDER BY a.sequence LIMIT ?',...bounds,...values,args.limit+1));
  const evidence=[...h.items.map(r=>({kind:'tag',id:r.tag_id,stamp:r.stamp,summary_zh:JSON.stringify({value:r.value,quality:r.quality})})),
    ...alarms.items.map(r=>({kind:'alarm',id:r.rule_id,stamp:stamp(r),summary_zh:r.transition})),
    ...logs.items.map(r=>({kind:'log',id:r.event_code,stamp:r.stamp,summary_zh:r.message_zh}))];
  evidence.sort((a,b)=>BigInt(a.stamp.sequence)<BigInt(b.stamp.sequence)?-1:1);
  const {items,truncated,matched_count,count_is_exact,...coverage}=logs;
  return {meta,evidence,coverage_by_source:{logs:coverage,history:unknown,alarms:unknown},truncated:truncated||h.truncated||alarms.truncated,
    evidence_sufficiency:'insufficient',conclusion_zh:'证据不足以判断根因'};
}
export async function startDiagnosticApi(directory,token,port=0){
  requireInput(typeof token==='string'&&token.length>=32,'需要至少32字符的诊断令牌');
  const db=new DatabaseSync(join(directory,'runtime.sqlite'),{readOnly:true});
  const server=createServer((req,res)=>{
    function send(status,body){res.writeHead(status,{'content-type':'application/json; charset=utf-8','cache-control':'no-store'});res.end(JSON.stringify(body));}
    if(!timingSafeEqual(hash(req.headers.authorization??''),hash('Bearer '+token)))return send(401,{error:'需要诊断身份'});
    if(req.method!=='GET')return send(405,{error:'仅支持只读查询'});
    try{
      const url=new URL(req.url,'http://localhost'),route=url.pathname.split('/').pop();
      if(!Object.hasOwn(routes,route)||url.pathname!=='/api/v1/'+route)return send(404,{error:'未知端点'});
      const args=parameters(route,url.searchParams);
      db.exec('BEGIN');
      try {send(200,execute(db,directory,route,args));} finally {db.exec('ROLLBACK');}
    }catch(e){send(e instanceof TypeError?400:503,{error:e instanceof TypeError?e.message:'查询未完成，证据不足',code:e.code??'QUERY_FAILED'});}
  });
  try {await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(port,'127.0.0.1',resolve);});}
  catch(e){db.close();throw e;}
  return {url:'http://127.0.0.1:'+server.address().port,close:()=>new Promise(resolve=>server.close(()=>{db.close();resolve();}))};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href){
  const api=await startDiagnosticApi(process.argv[2],process.env.LTP_DIAGNOSTIC_TOKEN,Number(process.argv[3]??0));
  console.log(api.url);
  for(const signal of ['SIGINT','SIGTERM'])process.once(signal,()=>api.close());
}
