# OPC UA只读采集驱动

独立依赖树：`node-opcua@2.187.29`，锁文件包含完整传递依赖。核心runtime与pi extension不引用此包。驱动不提供写PLC的方法。

本目录已通过本地真实OPC UA服务端测试，尚未接入现场或替换现有mock服务。T5现场验收未完成。

## 安装与验证

从仓库根目录执行，安装会写本目录node_modules，测试仅操作临时目录与测试自己创建的本地服务端。

```bash
npm ci --prefix runtime/drivers/opcua --registry=https://registry.npmmirror.com --ignore-scripts --no-audit --no-fund --fetch-retries=0 --fetch-timeout=30000
npm test --prefix runtime/drivers/opcua
npm test --prefix runtime
python scripts/verify-opcua-offline.py
```

最后一条建立本机依赖快照、逐文件还原核对，再直接运行还原后的驱动测试。归档路径与SHA256见`t5/evidence/offline-restore.json`；二进制归档被Git忽略，不随GitHub代码上传。生产Windows离线包须在目标平台重新建立并验证。

## 配置边界

`new OpcuaDriver(config)`：

| 字段 | 语义 |
|---|---|
| endpoint | 用户提供的实际opc.tcp地址，不得嵌入凭据 |
| security_mode | 默认SignAndEncrypt，固定Basic256Sha256；None只允许显式回环测试 |
| application_uri | 安全连接必填，须匹配客户端证书的ApplicationUri |
| pki_directory | 客户端独立PKI绝对目录，默认拒绝未知证书 |
| certificate_file / private_key_file | 安全连接必填，必须是已有绝对文件路径；不在仓库提交私钥 |
| get_credentials | 可选异步函数返回`{username,password}`；缺省Anonymous。凭据不进入snapshot/diagnostic |
| manifest | `contracts/driver.ts`的设备、采集组、tag类型与各点死区 |
| bindings | 每个tag对应`{namespace_uri, identifier}`。字符串表示String NodeId，非负整数表示Numeric NodeId；暂不支持GUID/ByteString |
| heartbeat_tag_id | 必须位于清单，且不能有死区；心跳应持续变化 |
| heartbeat_timeout_ms | 默认10000，最小200；须结合PLC心跳周期、服务器实际采样/发布周期和网络抖动整定 |
| reconnect_delay_ms | 固定重试间隔，默认1000，最小50；初次连接失败向调用方报错，不后台掩盖 |

接口：异步`connect/disconnect/rebuildSubscriptions`，`snapshot`普通对象，`sample/state/diagnostic`事件。请先注册监听再连接。调用方管理进程和持久化；诊断事件尚未接入runtime JSONL。

DB1900/1901和字节位偏移是原始点位信息，**不是OPC UA NodeId**，驱动不会推导或拼造现场绑定。

## 恢复和质量

- 分离链路、Session、Subscription状态。关闭库隐式重连，由驱动串行管理连接、会话及整份订阅清单。
- 每组一个Subscription；每组自动附加同一心跳MonitoredItem，以免某组停止发布时被其他组心跳掩盖。辅助心跳不重复作为业务样本发出。
- 心跳必须取得值变化，重建后收到同一缓存值不能算恢复；冻结心跳即使普通数据变化也不会显示healthy。
- 一组超时就使整体订阅stale，对清单发出坏质量unavailable样本，整单重建。断链也使缓存失效。恢复后重新核对NamespaceUri，避免NamespaceIndex变化串点。
- 重建代数隔离旧回调；断开后停止定时与重试，释放Session、Subscription、客户端和证书管理器。
- SourceTimestamp优先，其次ServerTimestamp，再次接收墙钟。时间0有效。单调时间和扫描序号独立记录。
- INT64/UINT64用十进制字符串，避免JS精度损失。类型不匹配、无值不伪造0；质量和原始状态码保留。
- 数值支持绝对/百分比死区；百分比由服务端标准EURange解释，服务端不支持过滤会使建点失败，不能静默退化。布尔、字符串和64位值目前只允许无死区。
- snapshot记录请求与服务端修订后的采样/发布间隔及队列大小。过滤器、队列溢出与状态异常不应被解读为完整历史。

心跳只能证明相应Subscription有进展，不能证明所有业务点内容正确或PLC业务正常。驱动报“证据不足”区分心跳冻结与订阅故障，不自动断言根因。

## 证书处理

双向分别建立信任：核对指纹后，由授权人员将服务端`pki/rejected`中的客户端证书移入服务端`pki/trusted/certs`；客户端也需信任服务端证书。驱动不自动批准。厂商PKI布局以现场产品为准。

`Bad_CertificateTimeInvalid`先核对双方当前时钟偏差，再检查证书有效期。泛化`BadSecurityChecksFailed`不能单独证明证书过期或不受信任。不得关闭校验绕过。证书管理器文件监视生效与否、重启要求须在目标PLC上实测。

## 尚未验收

现场Endpoint、安全策略、身份与NamespaceUri/NodeId待提供；连续24h、真实断网10次、Windows/Node22最低版本测试均未做。本地测试包含10次服务端重启，不等价于现场断网。

现有`MockLoop`仍为mock闭环，包含对mock manifest的校验；不能直接把此驱动插入后宣称生产历史落盘。下一步需在真实配置确定后接通协议无关的采集持久化入口，并验证坏质量unavailable、重连及日志落盘。HMI、LWMS/AGV请求与PLC写动作均未启用。
