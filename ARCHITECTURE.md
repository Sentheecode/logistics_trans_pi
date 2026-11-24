# 物流 C++ 工作台架构

当前工作台、采集和诊断使用独立 C++17 进程与合成数据。应用面向物流。真实设备、现场点位、外部任务接口及厂区安装有独立验收边界。[快速开始](README.md) · [验证记录](cpp-runtime/VALIDATION.md) · [协作规则](AGENTS.md)

## 代码与依赖

| 模块 | 职责 |
|---|---|
| `cpp-runtime/src/domain.*` | 物流模拟状态转换，依赖 C++ 标准库 |
| `cpp-runtime/src/adapters.*` | `SampleSource`、`HistorySink` 和模拟数据源 |
| `cpp-runtime/src/core.*` | SQLite 单写者、只读视图、接收时间窗、事件时间统计与固定模拟数值标签 |
| `cpp-runtime/src/control.*` | 有界本机文件信箱，只供模拟器命令 |
| `cpp-runtime/src/collector.cpp`、`supervisor.*` | 独立采集，Linux 可选父进程监督 |
| `cpp-runtime/src/catalog.*`、`point_audit.*` | 目录模型和静态配置审计 |
| `cpp-runtime/src/workbench.*`、`schematic.*` | Qt 单屏、拓扑、工厂选择器上方的诊断页签和模拟命令 |
| `cpp-runtime/src/diagnose.cpp`、`snapshot_json.*` | 本机只读 CLI 和原生 JSON 诊断 |
| `cpp-runtime/src/archive.cpp` | 独立本机 SQLite 归档恢复，输出只能落到新目录 |
| `cpp-runtime/src/mcp.*`、`mcp_stdio.cpp`、`mcp_http.cpp` | 四项只读工具与两种独立传输入口 |

上层通过边界模块访问领域及存储。领域状态机不依赖 Qt、SQLite、网络及设备 SDK。Qt 和 CLI/MCP 的生命周期与采集隔离。

## 采集、存储与故障

- 每个实例用专用本机目录和 SQLite 单写者租约，与旧库分开。v1–v4 逐版迁移，日志淘汰和落库在同一事务完成。v4 日志保留最新 300 条，记录 v4 之后的淘汰数和最高淘汰序号。采样历史会持续增长，覆盖保持未知。
- 坏质量的业务值用空值表达。时序明细按持久序号排序；墙钟和心跳各自提供时间证据。事件桶按源、服务端、接收时间优先级选取事件时间，在限额内统计质量、模拟帧和固定模拟标签的 min/max/avg，诊断结果明确区别最近观察和当前可用性。读者用 READONLY 与 query_only；大窗口查询有指令预算。
- Linux `collector --supervise DATA_DIR [interval_ms]` 以父进程和非阻塞管道监视事务提交后的心跳。使用单调时钟，回收自有采集子进程，再在 60 秒最多 3 次的预算内重启。独立采集可直接启动。
- Qt 异步读取 SQLite，异步发送模拟信箱。信箱只有 `manual`、`automatic_confirmed`、`step`、`disconnect` 四种有限操作，有本机锁、修订号、会话和截止时间。自动连续推进，手动只按明确单步推进，恢复自动需确认。手动等待期间继续发布采集心跳。
- 本机归档用 SQLite Backup API 固定一致读事务，将单文件快照复制到新目录，快照和恢复库清空旧心跳和启动标识。归档功能和 MCP 分离。
- 可选 open62541 fixture 只启动环回模拟服务器，验证多类型与多质量的批量 Read，以及临时证书下的新会话和重新订阅。默认运行时依赖不包含 open62541，现场适配器尚待验收。

## Qt 工作台与业务范围

工厂、业务模式、线体及工位级联联动，PLC 是设备关联。一台 PLC 的画面只列出这台 PLC 的工位。工位拓扑与选择双向同步。采集绑定保持独立，目前只有工位 140 提供实际模拟帧、日志和控制。排序区四个 PLC 已登记工位号和名称，没有模拟源，也不借用 140 的帧、日志或颜色。未登记工位的 PLC 仍显示未配置。三台可移动料车共享通道，固定工位与机械臂独立绘制。模拟分拣信息来自同一最新帧，数据质量和新鲜度影响当前可用性。

主屏包含工位、状态、任务进度、告警、近期日志和常驻操作。运行、工位、事件、点表、接口、日志和采集是工厂选择器上方的页签；切换页签不改变工位、阶段或采集。界面内嵌 Noto Sans CJK SC 子集，下拉弹出层使用深色文字和浅色底，按钮、输入、下拉和卡片使用圆角。调整界面或范围时回归 1100×650 与 1280×800、两种运行模式、工位隔离、过期证据和诊断的进程独立性。

## MCP

默认 `logistics-mcp DATA_DIR` 通过 stdio 提供 `status`、`alarms`、`logs` 和 `history`。工位参数仅允许固定模拟范围，日志正文不读取，对外只返回有界白名单元数据。数据目录由启动方指定，MCP 工具无控制或修改能力。CLI 保持本机人工诊断能力。

管理员可在 Linux 构建并显式启用 `logistics-mcp-http`，后端只监听 IPv4 环回。通过具有独立凭据和证书工位授权的 HTTPS 网关访问。部署与回退见 [HTTPS 运维](cpp-runtime/deploy/README.md)，职责与验收见 [ADR 0002](docs/adr/0002-logistics-mcp-operations.md)。

## 后续接手

改变数据、接口、界面或部署时同时更新设计、验证与限制。当前检出使用 C++。旧 Node、旧 SQL、脚本及验证证据在 [迁移记录](docs/cpp-migration.md) 的固定历史版本。需要生产接口时先验收受认可的端点、证书、控制权、权限和业务协议，再单独实现适配模块。
