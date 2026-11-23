# C++ 物流上位机首版（仅模拟）

这是独立实验程序，不是生产实现。只生成匿名合成数据，不读取仓库点表，不连接 PLC、AGV、LWMS 或其他服务，不包含任何写设备入口。旧 Node runtime、Pi 扩展和已发布契约保持原样。

交付三个独立进程：`collector` 定时模拟采集并单写 SQLite；`diagnose` 提供只读 CLI/REPL；`hmi` 用真实 Qt Widgets 中文工作台异步查询。导航是钱塘二厂 → 分拣区或排序区 → PLC 配置 → 已给定工位。一个 PLC 只挂一处采集；当前模拟帧只挂在工位 140，不复制到 150/160。HMI、诊断均不启动或停止采集进程，也不连接 PLC、LWMS 或 AGV。领域状态转换只依赖 C++ 标准库，设备/存储边界见 `src/adapters.hpp`；统一架构交接以仓库的 `ARCHITECTURE.md` 为准。

## 已实现的演示

一个合成运输场景携带三台小车，按第 1 台扫码、分拣、当前工位完成，再处理第 2、3 台。扫码不等于分拣完成；当前工位完成不等于整线或整单完成。每 16 次采样循环一次，默认周期 500ms：第 2 台演示阻塞异常，随后一次坏源质量；第 3 台演示两次断链采样和恢复。模拟进程重启后保留已确认模式，从已提交合成阶段的下一步继续；这不是真实业务恢复。

坏质量和断链帧的三车值均存为 SQL NULL，展示 `UNKNOWN`，从不补零。断链时源时间为空；服务器时间始终为空，因为没有真实设备服务器。SQLite 帧记录 UTC 接收毫秒、可用的合成源时间、启动标识、启动内单调纳秒、持久序号、质量原因、模拟链路、报警条件及事件。排序使用序号，不依赖墙钟。界面把同一毫秒显示为北京时间（Asia/Shanghai，UTC+8），存储仍是 UTC 毫秒。超过 3 秒没有新接收时间显示 stale；墙钟回跳显示 clock_uncertain。此新鲜度仅是本机接收证据，不证明设备健康。

报警只有合成条件 active / cleared / unknown；未实现确认、搁置、抑制或生产报警实例状态机。HMI 对坏质量、采集进程心跳过期、时钟不确定和查询失败显示当前状态未知，历史仍按原始质量展示。诊断输出原始历史观测与 freshness，不能将历史 cleared 解读为当前没有异常。

## 数据边界

显式指定一个专用本地数据目录；请使用新的空目录。程序拒绝包含无关文件、符号链接或其他 schema 的目录，绝不复用旧 `runtime.sqlite`。目录中仅有 `simulation.sqlite`、其 WAL/SHM、`writer.lease` 和 `control.mailbox`。SQLite `application_id` 与 schema v1/v2 用于辨识；新版在单写者租约内迁移 v1→v2，新增模式/心跳表，保留原帧。旧版程序不能打开 v2，不支持降级。单写者由独立租约数据库的排他事务约束，进程死亡后由操作系统释放。历史每帧事务提交，WAL + synchronous=FULL；进程崩溃测试通过不等于物理断电验证。

CLI/HMI 查询用 SQLite READONLY 与 query_only，不提供 SQL、shell、脚本、设备写点或网络入口。HMI 的模拟控制只写本地信箱，SQLite 仍由采集器单写。操作系统账户权限仍需部署方配置；WAL 共享内存配套文件的访问条件须在目标系统验证。不要使用网络共享目录。磁盘错误使采集显式失败；没有自动拉起服务、容量保留/降采样、完整日志覆盖证明或生产看门狗。首次建库中途损坏、未知 schema 均拒绝自动修复，保留原文件人工处理。历史持续增长，本版只适合短时模拟验收。

## 中文工作台与模拟手动/自动

新数据目录默认自动，按原场景节奏持续推进。点击“切换手动”，必须等采集端确认后才显示手动模式；随后事件保持不变，但进程每 200ms 发布心跳。点击“单步推进”只前进一步；“模拟断链”仅写入一帧合成断链证据，下一次单步继续原合成场景。自动模式禁止单步/断链按钮，并显示原因。点击“恢复自动…”必须在确认框选择“确认恢复自动”，默认选项是继续手动调试。

HMI 不启动/停止采集；关窗和重开都不会改变模式或推进任务。采集器重启保持已提交模式；手动模式重启仍待步。任务记录过期与采集进程心跳过期分开显示，手动停住不会仅因没点击就被当作断链。心跳超过 1500ms、时钟不确定或本地控制不可达时给出明确提示；结果未知不自动重试。

控制通道为同用户本地文件锁单槽信箱 `control.mailbox`，仅四种有限模拟命令。请求有启动身份、修订号、截止时间和长度上限；过期、重复修订号、并发忙均失败。不是通用控制面，无网络监听和设备通道。使用用户私有本地目录；Linux 0600 文件权限，Windows 继承目录 ACL，Windows 文件锁/权限仍待实测。CLI/Pi 仍只读。BUILD_TESTING 下的 `control_test_client` 是自动化测试夹具，不作为产品 CLI。

界面为中文，标准窗口 1280×800，也支持缩到 1100×650。Linux 需要中文字体（例如 Debian/Ubuntu 的 `fonts-noto-cjk` 包提供 Noto Sans CJK SC）；Windows 优先已有微软雅黑字体。没有 WebView 或 Qt Charts。主图是钱塘二厂的工位示意，不是测量图，也不是设备照片。上位机手动／自动和 PLC 设备自动是两回事；后者显示未读取。工位 140 的点表只在「点表」页作审计，不生成写地址，也不套用到其他工位。浏览目录不改变采集。

## Windows 下载、构建、启动（非管理员）

在 **MSYS2 MINGW64** 终端操作，示例所有文件均放 D:。不是 UCRT64；不要求 Ninja。已有依赖时跳过安装。下列 pacman 使用当前配置仓库，不承诺自动得到指定版本；先检查 Qt、GCC 和 CMake 实际版本。若使用已有 Qt 6.11.1，必须确认它与当前 MinGW 编译器/架构匹配，设置其安装前缀和 bin；不能混用 MSVC、UCRT 或其他项目 DLL。

```bash
mkdir -p /d/dev
cd /d/dev
git clone --branch experiment/cpp-runtime --single-branch https://github.com/Sentheecode/logistics_trans_pi.git
cd logistics_trans_pi
git rev-parse HEAD
# 已有上一版目录：确认没有未提交改动后用 git pull --ff-only origin experiment/cpp-runtime 更新。

# 在用户可写的 D:/msys64 内安装开发包，不需要 Windows 管理员权限。
pacman -S --needed git mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make mingw-w64-x86_64-sqlite3 mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-svg mingw-w64-x86_64-python
export PATH=/mingw64/bin:$PATH
g++ --version
cmake --version
qmake6 -query QT_VERSION

cmake -S cpp-runtime -B /d/build/logistics-cpp -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/msys64/mingw64
cmake --build /d/build/logistics-cpp -j4
ctest --test-dir /d/build/logistics-cpp --output-on-failure
```

若 Qt 不可用，可加 `-DBUILD_HMI=OFF` 构建核心与 CLI；这不会生成伪 HMI。测试使用 Python，纯构建可用 `-DBUILD_TESTING=OFF`。程序不依赖 Qt Charts。HMI 需要 Qt Svg 来绘制合成示意图；没有该组件时不要改用网页代替。

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

命令有界：recent/history 默认 20，最大 200，超过范围报错；status/alarms 只返回最后一帧。`more` 表示还有更老的帧，不是精确匹配总数；查询仅是按序号倒序的尾部原始历史，没有时间窗口、游标或聚合。输入每行最多 256 字节，未知命令和额外参数拒绝。coverage 固定 unknown；没有记录不能证明无异常，只能说证据不足。没有 LLM、根因推断和 Pi 自动调用。

| 旧契约 | 此 C++ 首版 |
|---|---|
| query_tags / GET tags | 未实现 HTTP 兼容；status 仅展示合成三车帧 |
| query_history / GET history | 未实现契约兼容；history 仅有界尾部帧 |
| list_alarms / GET alarms | 未实现契约兼容；alarms 仅最新合成条件 |
| search_logs / GET logs | 未实现，recent 不能当日志覆盖证明 |
| correlate_time_window / GET correlations | 未实现 |
| health / events WebSocket | 未实现 |
| 提案、确认和设备写入 | 未实现且无入口 |

旧 Pi 继续使用原 Node 只读 HTTP 服务和 token；不要把 C++ SQLite 交给旧服务。本版没有监听端口，故没有新的网络 token 服务。旧五工具测试仍独立通过，但不代表 C++ 已兼容。

## Linux 验证与遗留

常规已有依赖的 Linux 环境：

```bash
cmake -S cpp-runtime -B /tmp/logistics-cpp-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/logistics-cpp-build -j4
ctest --test-dir /tmp/logistics-cpp-build --output-on-failure
npm test --prefix runtime
```

[VALIDATION.md](VALIDATION.md) 记录本次实际命令、版本及测试结果。Qt 测试用 offscreen 平台运行真实 Widgets 和事件循环，读取真实模拟 SQLite；不是窗口管理器、显卡或 Windows 部署验收。

后续适配器入口为 `SampleSource`、`HistorySink`；只读诊断边界为 `sim::read`，领域纯函数为 `domain::advance`。真实 PLC NodeId、生产握手触发/复位、两家 AGV 签版 API、外部任务协议均未就绪，不定义假端点。没有新增 open62541 适配器或实测；已有 Node 本地协议测试不构成新 C++ OPC UA 测试。[open62541 1.5.8 加密后端说明](https://open62541.org/doc/v1.5.8/security/backends.html)已参考，后续若增加适配器，须单独锁定版本、UA_ENABLE_ENCRYPTION 后端与依赖，并只先连接隔离本地模拟服务器。
