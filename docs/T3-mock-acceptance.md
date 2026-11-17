# T3 mock 验收记录

日期：2026-11-17。环境：Linux x86_64，Node v24.22.1，Node SQLite 3.53.4。

## 本次结果

已跑通 T3 的 mock 验收场景：内存点位变化 → 零延时阈值报警 → SQLite 历史 → 结构化 JSONL。没有连接 PLC、LLM 或外部业务接口。此结果不代表 T0 全部通过、T4 掉电通过或 T5 OPC UA 实机通过。

- 通用契约：`contracts/driver.ts`，link/session/subscription 独立，driver 不依赖数据库或协议库。
- Mock：`runtime/src/mock-driver.mjs`，频率分组、清单深拷贝、按类型死区、质量与源时间优先级。
- 闭环：`runtime/src/mock-loop.mjs`，复用 T2 DDL，历史/报警/log intent 同事务；JSONL fsync 后登记索引，64 位序号不经过 JS number。
- 单写者锁，配置版本不一致拒绝静默覆盖；存储导出失败后拒绝继续接收，保留已提交历史和 outbox。

## 实测

运行命令：

```bash
cd /workspace/ltp
npm test --prefix runtime
npm run demo --prefix runtime
```

完整 runtime 测试：**12 项通过，0 项失败**。包含分组频率、SourceTimestamp=0、服务器/接收时间回退、死区与质量变化、断线订阅重建、仅订阅丢失、清单隔离、规则触发/清除、坏质量不误清、正常重启、未知 tag 拒绝、第二写入者拒绝、未审核配置替换拒绝、超过 2^53 的序号，以及日志索引失败注入。

演示证据：`t3/evidence/mock-demo.json`。

| 项目 | 结果 |
|---|---|
| 历史样本 | 60 条 |
| JSONL | 60 条，与历史逐样本诊断记录对应 |
| 未导出 outbox | 0 |
| 人为模拟断线 | 10 次；每次先证明仅 Session 恢复不出数，再重建后恢复 |
| 独立订阅丢失 | link=up、session=active、subscription=stale；重建后 healthy |
| 最终订阅代数 | 12（首次 + 十次断线 + 单独订阅丢失） |
| SQLite integrity_check | ok |
| 演示 JSONL Schema | 60 条全部通过，启用显式 UTC 日期检查 |

T2 的 Python/Node SQLite 检查、日志 Schema 正反例检查也已通过。三份契约做了类型剥离后的模块语法检查，不等于 TypeScript 语义类型检查；该语法工具发出 Node experimental 提示。

## 测试中发现并修正

1. 订阅代数原先被用作 Session 标识，测试证明仅重建订阅会错误改变 Session。已分离 Session 代数和订阅代数。
2. 起初同一目录允许第二写入者、配置更换也能直接进入，负向测试暴露后加入排他锁和版本检查。
3. 日志索引提交失败后再次投递可能产生第二条历史和重复 JSONL，故障注入已证明；现将该实例标为故障、拒绝继续接收，close 不再重试追加，也不标 clean_shutdown。outbox 留待 T4 恢复。

首次测试在实现前因模块缺失失败；后续行为负向测试也实际失败过，上述结果不是只检查 mock 方法被调用。

## 不在本次验收范围

- 真实调度、自动重连退避、看门狗、订阅心跳超时检测；演示使用虚拟时间和显式调用。
- 异常退出后的锁清理、日志尾行恢复、覆盖检查点、轮转、降采样、性能优化、拔电和改系统时间。
- 完整报警延时、确认/抑制、quality_bad 规则与业务状态机；本次只用数值 high/low、滞回和零延时。
- HTTP/WS、agent 工具注册、HMI、受控写、OPC UA 证书与真实订阅。
- Node22 最低版本、Windows/Electron 部署、现场 24h 和十次实机断网。

每条 mock 样本单独 fsync/事务、每条变化一条日志仅用于小规模验收，不能当作生产 100ms 全量写入方案。数据目录在异常退出后需 T4 的恢复工具处理，当前不自动删除锁或截断日志。

## 待用户确认（不影响纯 mock）

1. 140 的触发/完成/结果绑定、触发值，以及双方复位时机。
2. 三台小车共享二维码地址时的互斥保证与当前车辆关联信息。

现场 code、点表来源、地址及方向保持原样；没有因为本次测试创建 140 的有效运行写配置。
