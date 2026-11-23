# C++ 物流上位机首版（仅模拟）

这是独立实验程序，不是生产实现。只生成匿名合成数据，不读取仓库点表，不连接 PLC、AGV、LWMS 或其他服务，不包含任何写设备入口。旧 Node runtime、Pi 扩展和已发布契约保持原样。

交付三个独立进程：`collector` 定时模拟采集并单写 SQLite；`diagnose` 提供只读 CLI/REPL；`hmi` 用真实 Qt Widgets 异步查询、展示状态和最近 40 帧历史。HMI、诊断均不启动或停止采集进程。领域状态转换只依赖 C++ 标准库，设备/存储边界见 `src/adapters.hpp`；统一架构交接以仓库的 `ARCHITECTURE.md` 为准。

## 已实现的演示

一个合成运输场景携带三台小车，按第 1 台扫码、分拣、当前工位完成，再处理第 2、3 台。扫码不等于分拣完成；当前工位完成不等于整线或整单完成。每 16 次采样循环一次，默认周期 500ms：第 2 台演示阻塞异常，随后一次坏源质量；第 3 台演示两次断链采样和恢复。模拟进程重启后从新场景开始，不把上一轮状态当作真实业务恢复。

坏质量和断链帧的三车值均存为 SQL NULL，展示 `UNKNOWN`，从不补零。断链时源时间为空；服务器时间始终为空，因为没有真实设备服务器。SQLite 帧记录 UTC 接收毫秒、可用的合成源时间、启动标识、启动内单调纳秒、持久序号、质量原因、模拟链路、报警条件及事件。排序使用序号，不依赖墙钟。超过 3 秒没有新接收时间显示 stale；墙钟回跳显示 clock_uncertain。此新鲜度仅是本机接收证据，不证明设备健康。

报警只有合成条件 active / cleared / unknown；未实现确认、搁置、抑制或生产报警实例状态机。HMI 对坏质量、过期、时钟不确定和查询失败显示当前状态未知，历史仍按原始质量展示。诊断输出原始历史观测与 freshness，不能将历史 cleared 解读为当前没有异常。

## 数据边界

显式指定一个专用本地数据目录；请使用新的空目录。程序拒绝包含无关文件、符号链接或其他 schema 的目录，绝不复用旧 `runtime.sqlite`。目录中仅有 `simulation.sqlite`、其 WAL/SHM 和 `writer.lease`。SQLite `application_id` 与 schema v1 用于辨识。单写者由独立租约数据库的排他事务约束，进程死亡后由操作系统释放。历史每帧事务提交，WAL + synchronous=FULL；进程崩溃测试通过不等于物理断电验证。

CLI/HMI 用 SQLite READONLY 与 query_only，不提供 SQL、shell、脚本、写点或网络入口。操作系统账户权限仍需部署方配置；WAL 共享内存配套文件的访问条件须在目标系统验证。不要使用网络共享目录。磁盘错误使采集显式失败；没有自动拉起服务、容量保留/降采样、完整日志覆盖证明或生产看门狗。首次建库中途损坏、未知 schema 均拒绝自动修复，保留原文件人工处理。历史持续增长，本版只适合短时模拟验收。

## Windows 下载、构建、启动（非管理员）

在 **MSYS2 MINGW64** 终端操作，示例所有文件均放 D:。不是 UCRT64；不要求 Ninja。已有依赖时跳过安装。下列 pacman 使用当前配置仓库，不承诺自动得到指定版本；先检查 Qt、GCC 和 CMake 实际版本。若使用已有 Qt 6.11.1，必须确认它与当前 MinGW 编译器/架构匹配，设置其安装前缀和 bin；不能混用 MSVC、UCRT 或其他项目 DLL。

```bash
mkdir -p /d/dev
cd /d/dev
git clone --branch experiment/cpp-runtime --single-branch https://github.com/Sentheecode/logistics_trans_pi.git
cd logistics_trans_pi
git rev-parse HEAD

# 在用户可写的 D:/msys64 内安装开发包，不需要 Windows 管理员权限。
pacman -S --needed git mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make mingw-w64-x86_64-sqlite3 mingw-w64-x86_64-qt6-base mingw-w64-x86_64-python
export PATH=/mingw64/bin:$PATH
g++ --version
cmake --version
qmake6 -query QT_VERSION

cmake -S cpp-runtime -B /d/build/logistics-cpp -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/msys64/mingw64
cmake --build /d/build/logistics-cpp -j4
ctest --test-dir /d/build/logistics-cpp --output-on-failure
```

若 Qt 不可用，可加 `-DBUILD_HMI=OFF` 构建核心与 CLI；这不会生成伪 HMI。测试使用 Python，纯构建可用 `-DBUILD_TESTING=OFF`。程序不依赖 Qt Charts。

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
