# logistics_trans_pi

本仓库仅用于工业物流上位机与中间层。C++ 实验分支提供 Qt 单屏模拟工作台，采集、界面和诊断独立运行。

当前已有接口/数据模型草案、业务点表基线、T3 mock闭环、T4软件加固与T5独立OPC UA只读驱动。C++ 模拟界面和旧版 Node 运行路径均有本地验证。现场接入与生产部署待独立验收。

- C++ 入口：[架构](ARCHITECTURE.md)、[运行说明](cpp-runtime/README.md)、[验证记录](cpp-runtime/VALIDATION.md)、[协作规则](AGENTS.md)。
- [C++ 迁移阶段与兼容验收](docs/cpp-migration.md)；[本机诊断入口](agent/README.md)。
- [工作台架构决策](docs/adr/0001-logistics-workbench.md)；[MCP 运维提案（未实现）](docs/adr/0002-logistics-mcp-operations-proposal.md)。
- T0：pi 1.5.2 已锁定和实测；LLM 连通性/工具调用延迟经用户暂缓，Windows/完整离线部署尚未验收。
- [T1 接口契约](docs/T1-process-and-api.md)
- [T2 数据模型与容量](docs/T2-data-model.md)
- [业务基线](docs/business/requirements-v0.1.md)、[读写同一行的点表结构](docs/business/point-table-v0.1.md)
- [三台小车顺序与跨上位机共享澄清](docs/business/cart-sequence-and-sharing-v0.2.md)
- [T3 mock 运行与限制](runtime/README.md)
- [T4 第一部分：时钟不连续处理与验证](docs/T4-clock-acceptance.md)；T4 总体验收未完成
- [T4 软件验收：日志恢复/轮转/覆盖、实际调度、看门狗与自动重启](docs/T4-runtime-acceptance.md)；物理掉电及Windows验收待完成

- [T5 OPC UA本地验证](docs/T5-opcua-local-acceptance.md)、[驱动配置与运行](runtime/drivers/opcua/README.md)；已完成本地 OPC UA → SQLite 历史 → 阈值报警 → JSONL 验证，现场24h与实际断网待验收

本地验证（不连接 PLC、不调用 LLM）：

```bash
npm test --prefix runtime
npm run demo --prefix runtime
```

[T6 本机诊断工具与运行命令](agent/README.md) 已覆盖五个只读工具。模拟超温、日志缺口和 pi 工具白名单已在本地验证，模型通道待测。
