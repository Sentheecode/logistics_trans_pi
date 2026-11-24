# C++ 物流工作台运行手册

这是独立实验程序，不是生产实现。只生成匿名合成数据，不读取仓库点表，不连接 PLC、AGV、LWMS 或其他服务，不包含任何写设备入口。[快速开始](../README.md) 提供当前构建命令；[迁移表](../docs/cpp-migration.md) 记录旧工具的保留与退役。

工作台入口独立运行：`collector` 定时模拟采集并单写 SQLite；`diagnose` 提供只读 CLI/REPL；`hmi` 用真实 Qt Widgets 中文工作台异步查询。导航是工厂和 PLC。分拣、排序区只出现在 PLC 菜单的分组里。一台 HMI 同时列出该 PLC 的全部工位，没有工位选择器，也没有线体或业务模式下拉。页签是运行、日志、采集、点表、接口。当前模拟帧只挂在分拣外装的工位 140，切换 PLC 不改变采集归属。HMI、诊断均不启动或停止采集进程，也不连接 PLC、LWMS 或 AGV。领域状态转换只依赖 C++ 标准库，设备/存储边界见 `src/adapters.hpp`；统一架构交接以仓库的 `ARCHITECTURE.md` 为准。

## 已实现的演示

一个合成运输场景携带三台小车，按第 1 台扫码、分拣、当前工位完成，再处理第 2、3 台。扫码不等于分拣完成；当前工位完成不等于整线或整单完成。每 16 次采样循环一次，默认周期 500ms：第 2 台演示阻塞异常，随后一次坏源质量；第 3 台演示两次断链采样和恢复。模拟进程重启后保留已确认模式，从已提交合成阶段的下一步继续；这不是真实业务恢复。

坏质量和断链帧的三车值均存为 SQL NULL，展示 `UNKNOWN`，从不补零。断链时源时间为空；服务器时间始终为空，因为没有真实设备服务器。SQLite 帧记录 UTC 接收毫秒、可用的合成源时间、启动标识、启动内单调纳秒、持久序号、质量原因、模拟链路、报警条件及事件。排序使用序号，不依赖墙钟。界面把同一毫秒显示为北京时间（Asia/Shanghai，UTC+8），存储仍是 UTC 毫秒。超过 3 秒没有新接收时间显示 stale；墙钟回跳显示 clock_uncertain。此新鲜度仅是本机接收证据，不证明设备健康。

报警只有合成条件 active / cleared / unknown；未实现确认、搁置、抑制或生产报警实例状态机。HMI 对坏质量、采集进程心跳过期、时钟不确定和查询失败显示当前状态未知，历史仍按原始质量展示。诊断输出原始历史观测与 freshness，不能将历史 cleared 解读为当前没有异常。

## 数据边界

显式指定一个专用本地数据目录；请使用新的空目录。程序拒绝包含无关文件、符号链接或其他 schema 的目录，绝不复用旧 `runtime.sqlite`。目录中仅有 `simulation.sqlite`、其 WAL/SHM、`writer.lease` 和 `control.mailbox`。SQLite `application_id` 与 schema v1–v4 用于辨识；单写者租约内逐版迁移，保留原帧，旧客户端不能打开更高版本。v4 在同一事务内写日志、淘汰超过 300 条的旧记录并更新淘汰依据。单写者由独立租约数据库的排他事务约束，进程死亡后由操作系统释放。历史每帧事务提交，WAL + synchronous=FULL；进程崩溃测试通过不等于物理断电验证。

CLI/HMI 查询用 SQLite READONLY 与 query_only，不提供 SQL、shell、脚本、设备写点或网络入口。HMI 的模拟控制只写本地信箱，SQLite 仍由采集器单写。操作系统账户权限仍需部署方配置；WAL 共享内存配套文件的访问条件须在目标系统验证。不要使用网络共享目录。磁盘错误使采集显式失败。Linux 可选 `collector --supervise DATA_DIR [interval_ms]` 用独立父进程检测心跳并有限重启自有子进程。采样历史尚无容量保留、降采样或完整覆盖证明；未交付系统服务。首次建库中途损坏、未知 schema 均拒绝自动修复，保留原文件人工处理。历史持续增长，本版只适合短时模拟验收。

## 中文工作台与模拟手动/自动

新数据目录默认自动，按原场景节奏持续推进。点击“切换手动”，必须等采集端确认后才显示手动模式；随后事件保持不变，但进程每 200ms 发布心跳。点击“单步推进”只前进一步；“模拟断链”仅写入一帧合成断链证据，下一次单步继续原合成场景。自动模式禁止单步/断链按钮，并显示原因。点击“恢复自动…”必须在确认框选择“确认恢复自动”，默认选项是继续手动调试。

HMI 不启动/停止采集；关窗和重开都不会改变模式或推进任务。采集器重启保持已提交模式；手动模式重启仍待步。任务记录过期与采集进程心跳过期分开显示，手动停住不会仅因没点击就被当作断链。心跳超过 1500ms、时钟不确定或本地控制不可达时给出明确提示；结果未知不自动重试。

控制通道为同用户本地文件锁单槽信箱 `control.mailbox`，仅四种有限模拟命令。请求有启动身份、修订号、截止时间和长度上限；过期、重复修订号、并发忙均失败。不是通用控制面，无网络监听和设备通道。使用用户私有本地目录；Linux 0600 文件权限，Windows 继承目录 ACL，Windows 文件锁/权限仍待实测。CLI 和 MCP 仍只读。BUILD_TESTING 下的 `control_test_client` 是自动化测试夹具，不作为产品 CLI。

界面为中文，标准窗口 1280×800，也支持缩到 1100×650。程序优先读取 exe 旁边 `fonts` 目录里的 Noto Sans CJK SC（SIL OFL，见 [resources/fonts](resources/fonts/NOTICE.txt)），并同时接受微软雅黑。缺字回退保持打开，字体不打进 exe。运行、工位、事件、点表、接口、日志和采集排在工厂选择器上方；右上角不再用下拉打开这些页。按钮、输入框、下拉和卡片为圆角，下拉弹出层用深色文字配浅色底。没有 WebView 或 Qt Charts。分拣区仍是外装 140/150/160、仪表一 20/30/90、仪表二 40/10/70/80。排序区四个 PLC 各自同时显示自己的两个工位：排序区一 170/180、排序区二 190/200、排序区三 210/220、排序区四 230/240。工作台不提供工位选择。工位 140 的模拟只出现在 140 那一行，150 和 160 也没有模拟源。主图区分合成工位、固定设备与共用通道。上位机手动／自动和 PLC 设备自动是两回事；后者显示未读取。工位 140 的点表只在「点表」页作审计，不生成写地址，也不套用到其他工位。浏览目录和切换页签不改变采集。采集器在 schema v4 保留最近 300 条 `sim_logs`：操作审计、采集变化和三车阶段。日志显示北京时间，库里仍是 UTC 毫秒；不记录凭据或二维码原文。没有真实 S7 重连。

## Windows 下载、构建、启动（非管理员）

便携 ZIP 的生成与验收见[预览构建手册](deploy/windows/README.md)。

在 **MSYS2 MINGW64** 终端操作，示例所有文件均放 D:。不是 UCRT64；不要求 Ninja。已有依赖时跳过安装。下列 pacman 使用当前配置仓库，不承诺自动得到指定版本；先检查 Qt、GCC 和 CMake 实际版本。若使用已有 Qt 6.11.1，必须确认它与当前 MinGW 编译器/架构匹配，设置其安装前缀和 bin；不能混用 MSVC、UCRT 或其他项目 DLL。

```bash
mkdir -p /d/dev
cd /d/dev
git clone --branch experiment/cpp-runtime --single-branch https://github.com/Sentheecode/logistics_trans_pi.git
cd logistics_trans_pi
git rev-parse HEAD
# 已有上一版目录：确认没有未提交改动后用 git pull --ff-only origin experiment/cpp-runtime 更新。

# 在用户可写的 D:/msys64 内安装开发包，不需要 Windows 管理员权限。
pacman -S --needed git mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make mingw-w64-x86_64-sqlite mingw-w64-x86_64-nlohmann-json mingw-w64-x86_64-qt6-base mingw-w64-x86_64-python
export PATH=/mingw64/bin:$PATH
g++ --version
cmake --version
qmake6 -query QT_VERSION

cmake -S cpp-runtime -B /d/build/logistics-cpp -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/msys64/mingw64
cmake --build /d/build/logistics-cpp -j4
ctest --test-dir /d/build/logistics-cpp --output-on-failure
```

若 Qt 不可用，可加 `-DBUILD_HMI=OFF` 构建核心与 CLI；这不会生成伪 HMI。测试使用 Python，纯构建可用 `-DBUILD_TESTING=OFF`。程序不依赖 Qt Charts。合成拓扑使用 Qt Widgets 的 QPainter，不需要 Qt Svg。

打开三个独立 MINGW64 终端（每个设置 `export PATH=/mingw64/bin:$PATH`）：

```bash
# 终端 1：独立采集；Ctrl+C 仅停止此采集进程
/d/build/logistics-cpp/collector.exe /d/logistics-sim/cpp-v1

# 终端 2：只读 HMI，关窗不停止终端 1
/d/build/logistics-cpp/hmi.exe /d/logistics-sim/cpp-v1

# 终端 3：交互诊断；输入 help、status、recent 20、history 50、alarms、exit
/d/build/logistics-cpp/diagnose.exe /d/logistics-sim/cpp-v1
# 脚本调用，stdout 为带元信息和表头的 TSV，失败写 stderr 并返回非零
/d/build/logistics-cpp/diagnose.exe /d/logistics-sim/cpp-v1 status
/d/build/logistics-cpp/diagnose.exe /d/logistics-sim/cpp-v1 history 50
printf 'status\nalarms\nexit\n' | /d/build/logistics-cpp/diagnose.exe /d/logistics-sim/cpp-v1
```

`collector DIR [ticks [interval_ms]]` 可运行有限样本，ticks=1..1000000、interval_ms=20..1000。省略 ticks 则持续运行。不要在旧生产数据目录尝试。

本轮只在 Linux GCC 14.2 / Qt 6.8.2 / SQLite 3.46.1 验证。Windows 11 24H2、MSYS2 MINGW64 GCC 16.2 / CMake 4.4.4 / Qt 6.11.1 是后续目标，不是已通过验收环境。没有以 Qt 6.11.1 构建的产物在 Qt 6.11.0 上兼容的保证。

Windows 发布包以后使用与构建 Qt 一致的 `windeployqt` 收集 Qt 依赖，并单独核对 SQLite 和编译器运行库。不要复制其他项目发行 DLL。[Qt 官方部署说明](https://doc.qt.io/QT-6/windows-deployment.html)指出第三方库可能不由 windeployqt 收集。应在干净、非管理员目标机验收；本提交不包含发布二进制或 DLL。

## CLI 与旧接口兼容范围

命令有界：recent/history 默认 20，最大 200，超过范围报错；status/alarms 只返回最后一帧。`more` 表示还有更老的帧，不是精确匹配总数；交互命令只提供按序号倒序的尾部原始历史。单次执行可用 `snapshot-json [limit]` 或 `window-json FROM_UTC_MS TO_UTC_MS [limit]` 返回固定模拟范围的保留帧与日志，每类最多 40 条。`window-json` 按接收时间筛选且无游标。另有 `rollup-json FROM_UTC_MS TO_UTC_MS BUCKET_MS`，按源→服务端→接收时间回退统计保留模拟帧，刻度为 1000/60000/3600000 毫秒且 UTC 对齐，最多 24 小时、120 桶，空桶省略。输入每行最多 256 字节，未知命令和额外参数拒绝。coverage 固定 unknown；没有记录不能证明无异常，只能说证据不足。C++ CLI 不需要 LLM、Pi 或 Node，也不推断根因。

| 旧契约 | 当前 C++ 版 |
|---|---|
| query_tags / GET tags | 未实现 HTTP 兼容；status 仅展示合成三车帧 |
| query_history / GET history | CLI 提供尾部、接收时间窗与事件时间帧统计；仅固定模拟标签提供数值聚合，无旧 HTTP 契约或覆盖证明 |
| list_alarms / GET alarms | 未实现契约兼容；alarms 仅最新合成条件 |
| search_logs / GET logs | CLI 提供保留日志、时间窗及 v4 淘汰计数；无旧筛选字段及时间覆盖证明 |
| correlate_time_window / GET correlations | 未实现 |
| health / events WebSocket | 未实现 |
| 提案、确认和设备写入 | 未实现且无入口 |

人工诊断使用 C++ CLI，客户端集成使用 [MCP](deploy/README.md)。当前运行时全用 C++。旧格式、旧接口和可追溯版本见 [迁移验收表](../docs/cpp-migration.md)。

## MCP 工具与运行边界

启动 `logistics-mcp DATA_DIR`，由 MCP 客户端在 stdio 协议上列出和调用工具。每次调用指定 `station: "stn.140"`。日志只返回序号、时间、允许的级别和分类及关联帧序号。历史默认返回 10 条，最多 40 条；同时给出 `from_utc_ms`、`to_utc_ms` 可按接收时间筛选；再提供 `bucket_ms` 则统计事件时间桶；同时指定 `tag` 可聚合该模拟数值标签。报警是最近的模拟条件，并明确是否有当前有效证据。

Linux 可通过管理员批准的 [HTTPS 网关部署](deploy/README.md)。[验证记录](VALIDATION.md) 列出实际测试，Windows 和现场部署尚未执行。

## OPC UA 与恢复验证

可选 `-DBUILD_OPCUA_LOOPBACK_FIXTURE=ON` 需要 open62541 1.4.11 CMake 包。测试仅在 IPv4 环回执行合成批量 Read，覆盖 Int32、Boolean、Double、Good、Uncertain、Bad、缺失点及时间戳，也验证临时证书加密订阅、信任拒绝及断开后重新建立会话和订阅。默认采集器仍使用模拟适配器，真实设备及生产证书未接入。

## 模拟数值聚合与本机归档

单工位提供 `sim.completed_carts` 与 `sim.blocked_carts`，由合成帧的三车状态计算。使用源时间、服务端时间、接收时间的优先顺序划桶，沿用 1 秒、1 分钟、1 小时的对齐半开窗口，最多 24 小时和 120 桶。每桶返回帧数、可用数值数和最小值、最大值、平均值。坏质量、断链或不完整状态不会计作零，全无可用值时三项统计为 null。有效状态下的真实零仍参与聚合。示例在 Linux 用当前一分钟及前一分钟作为窗口：

```bash
minute=$(( $(date +%s) / 60 * 60000 ))
./build-logistics/diagnose DATA_DIR tag-rollup-json sim.completed_carts "$((minute - 60000))" "$((minute + 60000))" 60000
```

归档是单独的本机工具，MCP 没有此入口。输出目录必须由程序新建，SQLite 归档上限 64 MiB，操作预算 10 秒。可从采集运行中的库读取一致快照。恢复前做结构检查，快照和恢复库保留历史、日志、淘汰计数及已确认运行模式，清空采集心跳与启动标识。启动恢复目录中的采集器后，才会有新会话和当前证据。

```bash
./build-logistics/simulation-archive backup DATA_DIR NEW_ARCHIVE_DIR
./build-logistics/simulation-archive restore NEW_ARCHIVE_DIR NEW_RESTORE_DIR
./build-logistics/diagnose NEW_RESTORE_DIR snapshot-json 5
```

归档中只有 `simulation.sqlite`。需要恢复时只使用专用本地目录；如果进程被强制中断，可能留有半成品目录，复验后使用其他新目录运行。旧 JSONL 分段、旧 SQLite 格式和转换策略见 [迁移表](../docs/cpp-migration.md)。
