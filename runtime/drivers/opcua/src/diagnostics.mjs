/** 输出稳定诊断码和建议，不回传可能含凭据的原始错误文本。 */
export function classifyOpcuaError(error) {
  const text=String(error?.message??error).toLowerCase().replace(/[^a-z0-9]/g,'');
  if(text.includes('badcertificatetimeinvalid'))return{code:'OPCUA_CERTIFICATE_TIME_INVALID',message_zh:'先核对客户端与服务端的当前时钟和偏差，再核对证书有效期；不能直接认定证书已过期。'};
  if(text.includes('badcertificateuntrusted'))return{code:'OPCUA_CERTIFICATE_UNTRUSTED',message_zh:'核对双方证书指纹与信任方向；若客户端证书位于服务端 pki/rejected，由授权人员审核后移入 pki/trusted/certs。客户端也需明确信任服务端证书，驱动不会自动批准。'};
  if(text.includes('badcertificatehostNameinvalid'.toLowerCase()))return{code:'OPCUA_CERTIFICATE_HOSTNAME_INVALID',message_zh:'核对Endpoint主机名与服务端证书SAN，不关闭证书校验绕过。'};
  if(text.includes('badidentity')||text.includes('baduseraccessdenied'))return{code:'OPCUA_IDENTITY_REJECTED',message_zh:'服务器拒绝认证身份或点位读取权限，请核对已授权的身份配置。'};
  return{code:'OPCUA_CONNECTION_FAILED',message_zh:'OPC UA连接或会话操作失败；需结合三层状态、证书与网络证据排查，当前不足以断言根因。'};
}
