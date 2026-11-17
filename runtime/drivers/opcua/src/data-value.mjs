import{DataType,VariantArrayType}from'node-opcua';
const numeric=new Set([DataType.SByte,DataType.Byte,DataType.Int16,DataType.UInt16,DataType.Int32,DataType.UInt32,DataType.Float,DataType.Double]);
function typedValue(tag,variant) {
  if(!variant||variant.arrayType!==VariantArrayType.Scalar)throw new TypeError('只支持标量点位');
  const value=variant.value,type=variant.dataType;
  if(tag.type==='number'&&numeric.has(type)&&typeof value==='number'&&Number.isFinite(value))return{type:'number',value};
  if(tag.type==='boolean'&&type===DataType.Boolean&&typeof value==='boolean')return{type:'boolean',value};
  if(tag.type==='string'&&type===DataType.String&&typeof value==='string')return{type:'string',value};
  if(['int64','uint64'].includes(tag.type)&&type===(tag.type==='int64'?DataType.Int64:DataType.UInt64)&&Array.isArray(value)&&value.length===2) {
    const combined=(BigInt(value[0]>>>0)<<32n)|BigInt(value[1]>>>0);
    return{type:tag.type,value:String(tag.type==='int64'?BigInt.asIntN(64,combined):combined)};
  }
  throw new TypeError('点位类型与OPC UA值不匹配');
}
export function convertDataValue(tag,dataValue,observed_at_ms) {
  const status=dataValue.statusCode,valueCode=status.value>>>0,severity=valueCode>>>30;
  const quality={level:severity>=2?'bad':severity===1?'uncertain':'good',reason:severity>=2?'source_bad':severity===1?'source_uncertain':'source_good',native_code:status.toString()};
  if(valueCode&0x80){quality.level=severity>=2?'bad':'uncertain';quality.reason='overflow';}
  let value;
  try{value=typedValue(tag,dataValue.value);}catch{value={type:'unavailable',value:null};if(quality.level!=='bad'){quality.level='bad';quality.reason='decode_error';}}
  const timestamp=date=>{
    if(date===null||date===undefined)return null;
    const n=date instanceof Date?date.getTime():NaN;
    if(Number.isFinite(n))return n;
    if(quality.level==='good'){quality.level='uncertain';quality.reason='invalid_timestamp';}return null;
  };
  const source=timestamp(dataValue.sourceTimestamp),server=timestamp(dataValue.serverTimestamp);
  return{value,quality,source_at_ms:source,server_at_ms:server,observed_at_ms,event_at_ms:source??server??observed_at_ms,
    timestamp_basis:source!==null?'source':server!==null?'server':'observed'};
}
