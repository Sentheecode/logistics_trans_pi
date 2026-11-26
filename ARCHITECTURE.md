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
| `cpp-runtime/src/catalog.*`、`point_audit.*` | 目录模型，以及从独立点表库读取物流点位 |
| `cpp-runtime/src/workbench.*`、`schematic.*` | Qt 单屏、拓扑、诊断弹窗和模拟命令 |
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

原生 Qt 主屏以 PLC 为浏览范围。顶部页签分别进入运行、采集、点表、日志、事件和接口。运行区用俯视图并列展示该 PLC 的全部工位，图纸名称和已确认的相邻、左右关系用于静态展示，其余工位只列资料顺序。点选工位打开详情，不改变浏览的 PLC 或后台采集所有权。采集、点表、日志和事件以详情所关联的工位隔离数据。日志页显示索引，正文可在本机 CLI 查阅。只有 140 提供实际合成帧，三辆车仅作为该工位的本轮逻辑序号出现在阶段表。运行页保留模拟模式和单步，断链操作位于采集页。

排序目录仍按四组、每组两个工位，210/220 属于排序区三。点表页区分只能逐行核对的 140 基线和其他站的资料说明。展示筛选保留 116 行及原始行号，默认查看 75 条命名行。运行时从程序同目录的 `point-table.sqlite` 读取点表。该文件只含物流点位，构建时由已核对的 140 CSV 生成，不进入采集目录。地址拆成 DB 块、起始、偏移和长度，读写分列。当前 116 条记录各自只填一侧。来源里的方向冲突和重复地址原样保留。文件缺失、打不开或结构不符时，工作台直接失败，程序内不再内嵌这 116 行。150 与 160 仍只登记、不导入。厂区正式协议及映射待独立确认。

Windows 便携包的 `Logistics.exe` 使用 Windows GUI 子系统，分别隐藏启动采集器和工作台，并重定向进程输出到包内独立日志目录。`point-table.sqlite` 与可执行文件放在同一目录。采集器与 HMI 无父进程退出绑定。采集库 `simulation.sqlite` 仍由 SQLite 单写者租约保护，与点表库分开。CLI、归档与 stdio MCP 可独立运行。窗口使用系统原生装饰。

模拟适配器把扫码、分拣完成反馈和质量异常分别作为事件发布。链路恢复重发当前阶段，模拟状态机在后续采样推进。

## MCP

默认 `logistics-mcp DATA_DIR` 通过 stdio 提供 `status`、`alarms`、`logs` 和 `history`。工位参数仅允许固定模拟范围，日志正文不读取，对外只返回有界白名单元数据。数据目录由启动方指定，MCP 工具无控制或修改能力。CLI 保持本机人工诊断能力。

管理员可在 Linux 构建并显式启用 `logistics-mcp-http`，后端只监听 IPv4 环回。通过具有独立凭据和证书工位授权的 HTTPS 网关访问。部署与回退见 [HTTPS 运维](cpp-runtime/deploy/README.md)，职责与验收见 [ADR 0002](docs/adr/0002-logistics-mcp-operations.md)。

## 后续接手

改变数据、接口、界面或部署时同时更新设计、验证与限制。当前检出使用 C++。旧 Node、旧 SQL、脚本及验证证据在 [迁移记录](docs/cpp-migration.md) 的固定历史版本。需要生产接口时先验收受认可的端点、证书、控制权、权限和业务协议，再单独实现适配模块。
