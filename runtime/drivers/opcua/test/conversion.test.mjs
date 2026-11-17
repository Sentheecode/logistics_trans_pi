import test from'node:test';import assert from'node:assert/strict';
import{DataValue,Variant,DataType,VariantArrayType,StatusCodes}from'node-opcua';
import{convertDataValue}from'../src/data-value.mjs';import{classifyOpcuaError}from'../src/diagnostics.mjs';
test('SourceTimestamp为零仍优先，质量和数值按普通对象输出',()=>{
  const value=new DataValue({value:new Variant({dataType:DataType.Double,value:1.25}),sourceTimestamp:new Date(0),serverTimestamp:new Date(500)});
  const r=convertDataValue({type:'number'},value,1000);
  assert.equal(r.event_at_ms,0);assert.equal(r.timestamp_basis,'source');assert.equal(r.quality.level,'good');assert.deepEqual(r.value,{type:'number',value:1.25});
});
test('坏质量无值保留为unavailable，不能伪装数值0',()=>{
  const r=convertDataValue({type:'number'},new DataValue({statusCode:StatusCodes.BadNoCommunication}),1000);
  assert.deepEqual(r.value,{type:'unavailable',value:null});assert.equal(r.quality.level,'bad');
  assert.ok(r.quality.native_code.includes('BadNoCommunication'));assert.equal(r.timestamp_basis,'observed');
});
test('UInt64/Int64边界精确保留为十进制字符串',()=>{
  let r=convertDataValue({type:'uint64'},new DataValue({value:new Variant({dataType:DataType.UInt64,arrayType:VariantArrayType.Scalar,value:[0xffffffff,0xffffffff]})}),1);
  assert.equal(r.value.value,'18446744073709551615');
  r=convertDataValue({type:'int64'},new DataValue({value:new Variant({dataType:DataType.Int64,arrayType:VariantArrayType.Scalar,value:[0x80000000,0]})}),1);
  assert.equal(r.value.value,'-9223372036854775808');
});
test('类型不符或非有限数值显式坏质量，不自动字符串转数值',()=>{
  const r=convertDataValue({type:'number'},new DataValue({value:new Variant({dataType:DataType.String,value:'42'})}),1);
  assert.equal(r.quality.reason,'decode_error');assert.equal(r.value.type,'unavailable');
});
test('证书时间错误先检查两端时钟；不把泛化错误直接认定证书过期',()=>{
  const r=classifyOpcuaError(new Error('Bad_CertificateTimeInvalid'));
  assert.equal(r.code,'OPCUA_CERTIFICATE_TIME_INVALID');assert.match(r.message_zh,/时钟/);
  const untrusted=classifyOpcuaError(new Error('BadCertificateUntrusted'));
  assert.match(untrusted.message_zh,/rejected/);assert.match(untrusted.message_zh,/trusted/);
  assert.equal(classifyOpcuaError(new Error('socket closed')).code,'OPCUA_CONNECTION_FAILED');
});

test('不确定质量与溢出显式保留，服务端时间用于缺失源时间的回退',()=>{
 const value=new DataValue({value:new Variant({dataType:DataType.Double,value:2}),statusCode:StatusCodes.Uncertain,serverTimestamp:new Date(100)});
 const r=convertDataValue({type:'number'},value,300);
 assert.equal(r.quality.level,'uncertain');assert.equal(r.event_at_ms,100);assert.equal(r.timestamp_basis,'server');
 const overflow=convertDataValue({type:'number'},{...value,statusCode:{value:0x80,toString:()=> 'GoodOverflow'}},300);
 assert.equal(overflow.quality.reason,'overflow');assert.equal(overflow.quality.level,'uncertain');
 const nan=convertDataValue({type:'number'},new DataValue({value:new Variant({dataType:DataType.Double,value:NaN})}),300);
 assert.equal(nan.quality.reason,'decode_error');
});
test('诊断不回传可能包含凭据的原始错误文本',()=>{
 const r=classifyOpcuaError(new Error('unexpected password=do-not-return-this'));
 assert.ok(!JSON.stringify(r).includes('do-not-return-this'));
});
