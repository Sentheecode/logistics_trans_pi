const routes={query_tags:'tags',query_history:'history',list_alarms:'alarms',search_logs:'logs',correlate_time_window:'correlations'};
const string={type:'string'};
const ids={type:'array',items:string,maxItems:64};
const window={type:'object',properties:{from:string,to:string},required:['from','to'],additionalProperties:false};
const limit={type:'integer',minimum:1,maximum:500};
const specs={
  query_tags:[{tag_ids:ids},['tag_ids'],'查询每个点最后一次采集的值、时间和质量'],
  query_history:[{tag_ids:ids,window,resolution:{enum:['raw','1s','1m','1h'],type:'string'},limit},['tag_ids','window'],'查询原始历史或对齐UTC桶的聚合历史'],
  list_alarms:[{condition:{type:'string',enum:['active','cleared']},acknowledgement:{type:'string',enum:['acknowledged','unacknowledged']},limit},[],'列出报警及确认状态'],
  search_logs:[{window,device:string,tag:string,event_code:string,text:string,limit},['window'],'按字面检索日志，报告匹配总数、实际覆盖区间和缺口'],
  correlate_time_window:[{window,tag_ids:ids,device_ids:ids},['window','tag_ids','device_ids'],'按接收时间关联点位、报警和日志，仅报告可核实证据']
};
export default function register(pi){
  for(const [name,[properties,required,description]] of Object.entries(specs)){
    pi.registerTool({name,label:description,description,parameters:{type:'object',properties,required,additionalProperties:false},
      async execute(_id,input,signal){
        const base=new URL(process.env.LTP_DIAGNOSTIC_URL);
        if(base.protocol!=='http:'||base.hostname!=='127.0.0.1'||base.username||base.password)throw new Error('需要本机诊断API地址');
        const url=new URL('/api/v1/'+routes[name],base);
        for(const [key,value] of Object.entries(input)){
          if(key==='window'){url.searchParams.set('from',value.from);url.searchParams.set('to',value.to);}
          else for(const item of Array.isArray(value)?value:[value])url.searchParams.append(key,String(item));
        }
        const timeout=AbortSignal.timeout(10000);
        const response=await fetch(url,{headers:{authorization:'Bearer '+process.env.LTP_DIAGNOSTIC_TOKEN},
          signal:signal?AbortSignal.any([timeout,signal]):timeout,redirect:'error'});
        if(!response.ok)throw new Error('诊断查询失败，证据不足（HTTP '+response.status+'）');
        const details=await response.json();
        return {content:[{type:'text',text:JSON.stringify(details)}],details};
      }});
  }
}
