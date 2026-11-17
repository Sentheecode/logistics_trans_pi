# T5 OPC UA只读驱动：本地验证记录

日期：2026-11-17。结论：独立驱动本地验证通过，**T5现场验收未通过，尚未开展**。本次实现没有连接PLC、没有执行PLC写动作。

## 实现与边界

驱动位于`runtime/drivers/opcua`，协议库对象封装在内部；对外为普通样本、三层状态、订阅清单摘要及诊断事件。依赖与pi扩展、核心runtime分离，未fork pi。

- `node-opcua@2.187.29`，精确版本和147个传递包锁入lockfile v3；所有resolved地址为npmmirror。
- npm包gitHead：`ebdbec8c9287bf0691753099a3a12843beb01008`。完整来源和完整性值见`t5/evidence/package-provenance.json`。
- 实测平台Linux x86_64、Node v24.22.1。声明最低Node22.19不等于最低版本测试已完成。
- 默认SignAndEncrypt/Basic256Sha256，证书双向人工建立信任；不自动批准未知证书。None仅显式回环测试。
- 库自动重连关闭，驱动串行管理重连、Session和整份Subscription/MonitoredItem清单。
- 按组附加心跳监测，单组假活不能被其他组掩盖。旧订阅回调按代数隔离；冻结心跳的缓存首值不能伪造恢复。
- 质量、原始状态码、时间戳优先级、64位整数精度保留。坏质量无值用unavailable/null，通用契约相应扩展；mock初始值在契约中标记为可选但mock自身仍校验。

使用、配置字段、证书操作和限制见`runtime/drivers/opcua/README.md`。

## 实测结果

| 项目 | 证据与结果 |
|---|---|
| 驱动测试 | `t5/evidence/driver-tests.txt`：10/10通过，0失败/跳过 |
| 实际协议采集 | 本地创建OPCUAServer，经真实TCP/Session/Subscription读取BOOL、数值、字符串 |
| 部分组假活 | 仅停止慢组发布，快组持续更新；保持Session，检测超时并重建全部组 |
| 心跳冻结 | 普通值仍变化，心跳停止；经过多轮订阅重建仍不判healthy，心跳恢复后才恢复 |
| 连接恢复 | 测试服务端重启10次，每次新Session、完整订阅及心跳恢复；不是现场断网测试 |
| NamespaceIndex变化 | 第一次重启插入无关Namespace，使业务NamespaceIndex改变；按NamespaceUri重解析后继续采集 |
| 证书信任 | SignAndEncrypt实际连接：先因客户端不信任服务端拒绝；再因服务端不信任客户端拒绝并进入rejected；测试夹具显式建立双方信任后成功订阅 |
| 质量/时间/精度 | 坏值不转0，Uncertain/Overflow可见，SourceTimestamp=0有效、ServerTimestamp回退、INT64/UINT64边界不失精度 |
| 资源关闭 | disconnect后无新样本、不再重连，测试进程正常退出 |
| 原runtime回归 | `t5/evidence/runtime-regression.txt`：46/46通过，含SIGKILL恢复、调度、看门狗和监督重启 |

证书时间无效的判断顺序由纯诊断测试验证；本次没有更改系统时钟，也没有把模拟错误分类称作现场证书时间故障实测。

## 离线还原

`scripts/verify-opcua-offline.py`创建`node_modules`离线归档，还原到全新临时目录，核对8615个文件/链接的哈希、目标和权限，直接运行驱动测试：10/10通过。

恢复过程未调用npm或下载/安装包；测试只连接自己建立的本地服务端。未实施网络隔离容器验证，亦不证明Windows目标包可运行。归档约8.84MB，精确字节、SHA256和环境见`t5/evidence/offline-restore.json`，归档被Git忽略、未上传GitHub。完整还原测试输出为`t5/evidence/offline-driver-tests.txt`。

## 未完成与后续条件

1. 现场Endpoint、安全模式/策略、身份类型、ApplicationUri及证书、心跳与业务点NamespaceUri/NodeId尚未取得；原DB地址不能直接充当NodeId。
2. 现有mock闭环尚未改为协议无关的采集持久化服务；独立UA驱动尚未接入SQLite/报警/JSONL。当前不宣称OPC UA端到端生产闭环已完成。
3. 未进行测试工位24h运行、实际断网10次、物理掉电、现场改时钟、Windows及最低Node版本验证。
4. 心跳可证明所在Subscription有进展，不能证明每个业务点正确；不编造PLC故障根因。超时配置需现场整定。
5. LLM仍按用户要求暂缓；HMI、LWMS/AGV接口、跨上位机共享与业务PLC写动作未启用，业务协议不凭空补齐。

140原始点表未改动，SHA256仍为`fd4885f50bb7fbda73e77ecdf40520015782eda34f56c98ccb7845004ad2cc44`。“小车1/2/3放料完成”仍分别表示对应小车在当前工位的本轮全部分拣完成；不能据此推导扫码应答/复位顺序。
