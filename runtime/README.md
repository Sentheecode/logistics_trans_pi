# runtime：T3 mock 闭环

独立于 pi，零 LLM 依赖，零 npm 运行依赖。当前是 mock 采集与持久化内核，不是已上线的守护服务。尚未启动 HTTP、WebSocket 或真实 PLC 连接。

从项目根目录执行：

```bash
npm test --prefix runtime
npm run demo --prefix runtime
```

不需要 npm install；当前 Node v24.22.1 实测，声明最低 Node22.19，尚未在该最低版本或 Windows 验证。Node22 的 node:sqlite 属实验能力，后续需在目标版本单独验收。

演示在临时目录生成 runtime.sqlite 和 events.jsonl，输出路径；测试使用临时库并清理。所有点位与阈值为 examples/mock-config.mjs 中的合成数据，不加载 140 基线，不发起 PLC 写入。driver 注入方法属于模拟接口，不开放给 agent。

## 当前能力

- driver 层不依赖 SQLite、pi 或 OPC UA 库；订阅清单按频率分组，深拷贝防止调用方改变已保存清单。
- link/session/subscription 三层状态；模拟断线、仅恢复会话、丢失订阅、按清单重建。订阅代数与会话代数分开。
- number 绝对/百分比死区；boolean/string/int64/uint64 禁用模拟量死区。质量变化与重建后首样本不被旧死区抑制。
- SourceTimestamp > ServerTimestamp > 接收时间；null 语义回退，零时间戳保留。
- 真 SQLite WAL/FULL；样本、零延时 high/low 报警转换、日志 intent 同一事务；JSONL fsync 后登记索引。
- 序号用 SQLite INTEGER / JS BigInt / JSON 十进制字符串衔接，不经过浮点数。
- 数据目录排他锁，日志尾部长度不一致则拒绝启动；日志导出失败后拒绝继续接收，避免重复追加并保留 outbox，不伪装为正常持久化。

## 明确限制

- advance() 是手动虚拟时间推进；没有真实周期调度、后台重连、看门狗或自动订阅超时检测。重建由测试/演示显式调用。
- 报警仅为 high/low、good 数值、滞回和零延时；坏质量不清除已有 active 实例。未实现 T2 的延时、确认、抑制、完整质量报警或实际业务状态机。
- 每个已投递 mock 样本写一条诊断记录，仅用于小规模闭环验收；生产必须使用 T2 的事件/汇总日志策略，不能直接照此处理全厂 100ms 数据。
- 没有降采样/清理、日志轮转、覆盖检查点、查询 API、审计受控写或异常退出后恢复。普通日志不能据此报告完整时间覆盖。
- 锁文件在异常退出后可能保留；不自动删除，因为此阶段尚无可靠僵尸写入者识别。不要对生产目录手工删锁；T4 必须实现恢复流程并做掉电验证。
- SQLite 与 JSONL 跨资源恢复尚未实现。发生尾部长度不一致时显式阻塞并保留文件，不自动截断/覆盖证据。
- 每个样本独立事务/fsync，未做批量性能优化或 10k 样本/秒验收。没有 Windows 电源故障/磁盘缓存测试，不承诺掉电零丢失。
- MockLoop 的同一设备配置版本变化会被拒绝；更换合成参数请使用新的临时目录。没有未经审核的在线配置热更新。

现场扫码仍等触发值、复位顺序和三车共享结果关联规则。真实 OPC UA NodeId、队列/publishing 参数协商、证书、自动健康监测与故障自愈由 T5 单独实现，不因 mock 成功而宣称通过。
