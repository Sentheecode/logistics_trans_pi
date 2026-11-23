# 日志去重与跨 PLC 隔离（收尾）

日期：2026-11-23。本地分支 `cursor/qiantang-workbench-d21d`，没有推送，没有动 `main`（`82a8232202f39bd423d43c69cd19f7061cbc2336`）。

相位 10 和 11 都是模拟链路断开。原先只要事件名是 `SIM_LINK_DOWN` 就再写一行，所以 1100 日志里连续两行是同一句话。现在只在链路或质量相对上一帧变化时写采集日志，重启时用最后一帧记住上次状态。列表用短句（模拟链路断开、模拟链路恢复、数据质量不可用），「不是现场离线、没有 S7 重连」只留在日志页说明。非 140 的运行、日志、事件、点表和采集页不展示 140 的帧或日志；顶栏心跳仍是采集进程。150/160 示意图不使用 140 的三车颜色。运行页直接铺满当前视口，近期记录在表内滚动。质量正常浅绿，未知和未连接为灰，模拟断开为琥珀。

审查原则来自 Damir Sokolovsky，heydamir/unslop，CC BY 4.0。技能未装入仓库。

```bash
cmake -S cpp-runtime -B /tmp/logistics-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build /tmp/logistics-release -j$(nproc)
ctest --test-dir /tmp/logistics-release --output-on-failure
cmake -S cpp-runtime -B /tmp/logistics-core -DBUILD_HMI=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build /tmp/logistics-core -j$(nproc)
ctest --test-dir /tmp/logistics-core --output-on-failure
npm test --prefix runtime
```

同一最终树上：

- Release CTest **8/8** 通过，0 失败：`domain`、`catalog`、`point_audit`、`modes`、`acceptance`、`logs`、`widgets_controls`、`hmi_refresh`。
- `BUILD_HMI=OFF` **6/6** 通过，0 失败：`domain`、`catalog`、`point_audit`、`modes`、`acceptance`、`logs`。
- Node runtime **49/49** 通过，0 失败，0 跳过。`runtime/drivers/opcua` 未跑。
- `logs` 覆盖 v1→v3 与 v2→v3 迁移、`sim_logs` 上限 300、采集日志少于帧数、相邻采集日志不重复、断开帧数多于断开日志、日志不含凭据字样、第二采集进程拿不到 `writer.lease`。

点表详情放在可滚动区域里：文字短于视口时一次看完，更长时滚动条能到达最后一行「审计码」，1280 和 1100 都检查标签高度和滚动范围，不只检查字符串是否存在。没有工位的 PLC（排序二、三、四）运行页只在顶部显示「尚未配置工位」「当前 PLC 未绑定模拟源」「暂无法执行模拟操作」，不再铺开带工位 140 说明的运行卡。有工位但未绑定模拟源的 PLC 仍显示本 PLC 的示意图，不显示 140 的帧和日志。

尚未做：Windows DPI 与字体、真实 S7/LWMS/库卡/华睿报文、工位真实形体。接口页右侧留白仍在，本轮不改。截图由最终提交的 `hmi` 在新数据目录当场抓取。

---

# 左侧导航工作台（本轮）

日期：2026-11-23。仍在实验分支上本地修改，没有推送，没有动 `main`（`82a8232202f39bd423d43c69cd19f7061cbc2336`）。只模拟，不连接现场设备。

界面审查用了 Damir Sokolovsky / heydamir `unslop`（CC BY 4.0）里可迁移的原则，没有安装该 skill，也没有跑它的浏览器或生图脚本。对照的是 Xvfb 上 `hmi` 的真实截图，不是样式表里有没有 token。

- 1280×800 与 1100×650 的运行、采集、日志、事件、工位、点表、接口页，内容都从顶栏下约 y=106–133 开始，工作区铺到状态栏上沿（底边空隙 1px）。接口页列表和详情都落到 y=789，不再把标题撑到中部。
- 标题「物流工作台」「工位 140」与 Qt 画出的 Noto Sans CJK SC Bold 逐像素一致（均方误差 0）；同位置的 Noto Serif CJK SC 均方误差约 8500–11300。时间列更接近 Noto Sans Mono CJK SC。控件测试检查的是 `QFontInfo` 解析结果，要求 family 为 `Noto Sans CJK SC` 且 style 为 Regular 或 Bold。
- 运行页主操作是「切换手动」。采集页给出所有者、模拟源、年龄、帧序号、质量、链路、模式，并写明设备未连接。日志对象显示「外装 · 工位 140」，时区写在页说明和顶栏心跳，行内时间不再逐条加「北京」。

```bash
cmake -S cpp-runtime -B /tmp/logistics-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build /tmp/logistics-release -j$(nproc)
ctest --test-dir /tmp/logistics-release --output-on-failure
cmake -S cpp-runtime -B /tmp/logistics-core -DBUILD_HMI=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build /tmp/logistics-core -j$(nproc)
ctest --test-dir /tmp/logistics-core --output-on-failure
npm test --prefix runtime
```

- 带界面的 Release CTest **8/8**：`domain`、`catalog`、`point_audit`、`modes`、`acceptance`、`logs`、`widgets_controls`、`hmi_refresh`。
- `BUILD_HMI=OFF` **6/6**：上述除 `widgets_controls` 与 `hmi_refresh` 外的六项，含 `logs`。
- Node runtime **49/49**。
- `logs` 覆盖 v1/v2 迁到 schema v3、日志不超过 300 行、采集类日志少于帧数、第二个采集进程拿不到写入租约。

---

# 钱塘二厂模拟示意版交付

日期：2026-11-23。在 `518cf6bd690d2a2779ffb8856e602dd3b6ec5dfb` 之上完成钱塘目录、单 PLC 示意和北京时间显示，并推回 `experiment/cpp-runtime`。`main` 保持 `82a8232202f39bd423d43c69cd19f7061cbc2336`，没有检出或推送。这是模拟示意，不是生产上线，也不是现场布局验收。

环境：Linux，GCC 13.3.0，CMake 3.28.3，SQLite 3.45.1，Qt 6.4.2，Python 3.12.3，Node v22.23.4，Noto Sans CJK SC，Xvfb。Qt 配置打印 `Could NOT find XKB`；`xcb` 仍完成真实窗口抓图。未在 Windows、MINGW64、真实 DPI 或现场协议上运行。

```bash
cmake -S cpp-runtime -B /tmp/logistics-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build /tmp/logistics-release -j$(nproc)
ctest --test-dir /tmp/logistics-release --output-on-failure
cmake -S cpp-runtime -B /tmp/logistics-core -DBUILD_HMI=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build /tmp/logistics-core -j$(nproc)
ctest --test-dir /tmp/logistics-core --output-on-failure
npm test --prefix runtime
```

- Qt Release CTest **7/7**：`domain`、`catalog`、`point_audit`、`modes`、`acceptance`、`widgets_controls`、`hmi_refresh`。
- `BUILD_HMI=OFF` 核心 CTest **5/5**：`domain`、`catalog`、`point_audit`、`modes`、`acceptance`。
- Node runtime **49/49**，0 fail / 0 skip。`runtime/drivers/opcua` 的独立测试未跑。
- 控件测试实际点击了图上工位和编号按钮。仪表二只含 40/10/70/80，排序二、三、四保持空态。140 的历史、质量和点表不出现在其他工位。点表每行 `executable_write` 为 false。
- 手动连点只推进一帧；模拟断链后单步仍在同一采集器上继续；取消恢复自动不改变模式；确认后回到自动。来回切换 PLC 不改变模式、修订号和启动身份。
- 右上心跳和历史接收时间显示北京时间（UTC+8）。SQLite 里的 `received_ms` / `heartbeat_ms` 仍是 UTC 毫秒。
- 140 图内三辆料车与下方状态卡共用阶段色：等待灰、扫码或分拣蓝、完成深色、阻塞或未知琥珀。其他工位只画一辆无阶段料车。

截图由 Xvfb + `QT_QPA_PLATFORM=xcb` 的 `hmi` 抓取，不进 Git。外装、仪表一、排序一、排序二各有 1280×800 和 1100×650；仪表二另有同两档。图题写明通用示意，层数和动线未提供。1100 宽下工位按钮在图下，状态文字未被裁切。

尚未确认：现场平面和各工位真实形体、层数、动线、LWMS／华睿／库卡报文、140 点表冲突地址的现场结论、Windows 字体与 DPI、生产权限和安全联锁。

---

# 钱塘二厂导航纠正（中间记录，随后已纳入上一节交付）

日期：2026-11-23。在 `518cf6bd690d2a2779ffb8856e602dd3b6ec5dfb` 之上改目录和界面，工作分支留在本地，没有推送 `experiment/cpp-runtime`，也没有动 `main`（`82a8232202f39bd423d43c69cd19f7061cbc2336`）。

```bash
cmake -S cpp-runtime -B /tmp/logistics-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build /tmp/logistics-release -j4
ctest --test-dir /tmp/logistics-release --output-on-failure
npm test --prefix runtime
```

Release CTest **7/7** 通过：`domain`、`catalog`、`point_audit`、`modes`、`acceptance`、`widgets_controls`、`hmi_refresh`。Node **49/49**。测试覆盖钱塘二厂唯一工厂、排序二未配置、140 与 150 不重复模拟帧、排序区不改手动／自动、140 点表无可执行写、SVG 与下拉同步，以及 1100×650 / 1280×800 标签。抓图在 Xvfb 的 `xcb` 上由 `hmi` 生成，供视觉审查，不代表视觉已经定稿。

---

# 目录、控制器一对多与 SVG 联动验证

日期：2026-11-23。起点：`ab0d660d5a2cdf6f270b2a795e597fda33f6b5a9`。`main` 在验证开始时为 `82a8232202f39bd423d43c69cd19f7061cbc2336`，本轮没有检出或提交 `main`。只使用合成目录和合成采集，没有连接 PLC 或其他工业设备。

环境：Linux，GCC 13.3.0，CMake 3.28.3，SQLite 3.45.1，Qt 6.4.2，Python 3.12.3，Node v22.23.4，Noto Sans CJK SC，Xvfb。Qt 配置打印 `Could NOT find XKB`；`xcb` 平台仍完成真实窗口抓图。

```bash
cmake -S cpp-runtime -B /tmp/logistics-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build /tmp/logistics-release -j4
ctest --test-dir /tmp/logistics-release --output-on-failure
cmake -S cpp-runtime -B /tmp/logistics-core -DBUILD_HMI=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build /tmp/logistics-core -j4
ctest --test-dir /tmp/logistics-core --output-on-failure
npm test --prefix runtime
```

- Qt Release CTest **6/6** 通过：`domain`、`catalog`、`modes`、`acceptance`、`widgets_controls`、`hmi_refresh`。
- 无 HMI 的核心 CTest **4/4** 通过：`domain`、`catalog`、`modes`、`acceptance`。
- Node runtime **49/49** 通过，0 fail / 0 skip。`runtime/drivers/opcua` 的独立测试未跑。
- 手动连点仍只推进一帧；取消恢复自动不改变模式；确认后自动继续。关闭 HMI 不停止采集。
- 手动时切换工厂、业务模式、空线和 SVG 点选，模式、修订号、序号、启动身份不变。
- 自动时选中排序区，序号继续增加，修订号不变，历史表保持为空。
- 非实时工位的控制按钮禁用；详情中的设备连接为“未探测”，库存为“未知”。
- 南湾工位 C1 与北岸工位 A1 同名，控制器关系仍按 ID 分开；C1 与 D1 跨业务模式同属一个控制器。

截图是 Xvfb 上 `QT_QPA_PLATFORM=xcb` 的 `hmi` 抓图，外加 `import -window root` 的桌面图，不进 Git。主要文件：`cpp-sps-auto.png`、`cpp-sps-manual.png`、`cpp-dropdown.png` 与 `cpp-dropdown-popup.png`、`cpp-plc-peers.png`、`cpp-sort.png`、`cpp-south-factory.png`、`cpp-unconfigured.png`、`cpp-sps-compact.png`（1100×650）、`cpp-sps-alarm.png`。语义和未跑项见仓库 `ARCHITECTURE.md` 的实施记录。

---

# 中文工作台与模拟模式迭代验证

日期：2026-11-23。迭代基线：`bd92ab0f2b16aca25b5ae6aa799931af25b959f5`；仍仅修改实验分支。环境沿用下方记录：Linux GCC 14.2 / CMake 3.31.6 / SQLite 3.46.1 / Qt 6.8.2。Qt 中文字体使用 Noto Sans CJK SC。

## 本轮实际命令与结果

从空目录构建 Qt Release 和无 Qt 核心，最终源码增量重建后再次运行同一套测试：

```bash
export LD_LIBRARY_PATH=/workspace/deps/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=/workspace/deps/root/usr/lib/x86_64-linux-gnu/qt6/plugins
export XDG_CACHE_HOME=/workspace/deps/cache
/workspace/deps/root/usr/bin/cmake -S cpp-runtime -B /workspace/cpp-v2-release \
  -DCMAKE_PREFIX_PATH=/workspace/deps/root/usr -DCMAKE_BUILD_TYPE=Release
/workspace/deps/root/usr/bin/cmake --build /workspace/cpp-v2-release -j4
/workspace/deps/root/usr/bin/ctest --test-dir /workspace/cpp-v2-release --output-on-failure
/workspace/deps/root/usr/bin/cmake -S cpp-runtime -B /workspace/cpp-v2-core \
  -DBUILD_HMI=OFF -DCMAKE_PREFIX_PATH=/workspace/deps/root/usr -DCMAKE_BUILD_TYPE=Release
/workspace/deps/root/usr/bin/cmake --build /workspace/cpp-v2-core -j4
/workspace/deps/root/usr/bin/ctest --test-dir /workspace/cpp-v2-core --output-on-failure
npm test --prefix runtime
```

- Qt CTest **5/5 通过**：domain、modes、acceptance、widgets_controls、hmi_refresh。
- 核心 CTest **3/3 通过**：domain、modes、acceptance。
- 原 Node runtime **49/49 通过**，0 fail / skip。
- 自动持续推进；手动等待超过 3 秒，事件不变、心跳仍更新；每次成功单步只增加一帧。
- 实际 Qt 按钮快速连点只提交一次；恢复自动的取消分支不推进、确认分支恢复推进；自动模式下单步禁用。
- 并发客户端、旧修订号、过期请求、旧启动身份拒绝；超长/损坏信箱输入不导致任务推进；无采集进程时请求有界失败，结果未知不重试。
- HMI 在自动/手动两种模式退出都不停止采集；手动模式重开 HMI 不切模式、不推进；采集器重启保留手动模式和历史。
- 缺失端点及心跳过期时 Qt 控件禁用；CLI 新增观察到的 mode/process 元信息，仍拒绝 step/manual 等写命令。
- 合成 v1 数据库升级到 v2，原始帧保留；原正常/异常/坏质量/断链恢复/崩溃与单写者测试仍通过。

## 真实截图与尺寸检查

最终 Qt 程序使用 offscreen 平台、实际 SQLite 和实际模式确认渲染；没有设计稿或虚构状态。两张交付 PNG：`cpp-workbench-auto.png`、`cpp-workbench-manual.png`，均为 1280×800；另生成 1100×650 的 compact 版本供检查。四张均已人工查看，中文没有方块，顶栏、任务、卡片、按钮和底栏无裁切。Qt 控件测试也检查两种尺寸的可见标签宽度与中文字形。

复现方式：独立启动 `collector DATA_DIR 100 800`，使用 HMI 的“切换手动”按钮进入采集端确认的手动模式。自动化截图流程中的测试夹具调用与同一有限信箱等价；产品诊断 CLI 不提供该操作。以下命令只读现有模式并截图，不会为截图而切模式：

```bash
export QT_QPA_PLATFORM=offscreen
/workspace/cpp-v2-release/hmi DATA_DIR 800 /workspace/cpp-workbench-auto.png normal
# 先在交互界面切换手动，等待采集端确认，再运行：
/workspace/cpp-v2-release/hmi DATA_DIR 800 /workspace/cpp-workbench-manual.png manual
# 在同类命令末尾增加 compact 得到 1100×650。
```

截图独立保存在 Library，不提交 Git。仍未在 Windows/MINGW64、Qt 6.11.x、真实桌面 DPI 或实际设备上验证。Windows LockFileEx 分支已提供但未运行；本地文件信箱只适用于受信任同用户的私有本地目录，不是生产权限/联锁系统。不连接任何现场端点，不部署。

---

# 首版验证记录

日期：2026-11-23。基线：`82a8232202f39bd423d43c69cd19f7061cbc2336`。只使用匿名合成数据；未连接任何真实设备或外部业务服务。

## 环境与依赖

Linux / GCC 14.2.0 / CMake 3.31.6 / SQLite 3.46.1 / Qt 6.8.2 / Python 3.13.5 / Node 24.22.3。系统没有可用 sudo，系统 apt 安装因权限失败。随后成功以普通用户把 Debian 已配置 snapshot 源的开发包下载并解压到 `/workspace/deps/root`，未修改系统或项目依赖目录。

实际依赖准备命令（仅云端 Linux 普通用户路径）：

```bash
mkdir -p /workspace/deps/lists/partial /workspace/deps/cache/archives/partial /workspace/deps/root
touch /workspace/deps/status
apt-get -o Dir::State::lists=/workspace/deps/lists -o Dir::Cache=/workspace/deps/cache update
apt-get -o Dir::State::lists=/workspace/deps/lists -o Dir::Cache=/workspace/deps/cache \
  -o Dir::State::status=/workspace/deps/status --download-only -y install cmake libsqlite3-dev qt6-base-dev
for f in /workspace/deps/cache/archives/*.deb; do dpkg-deb -x "$f" /workspace/deps/root; done
```

使用 Debian trixie snapshot `20261117T190000Z` 配置源；下载的是开发环境依赖，不进入 Git。下列构建从原本不存在的两个构建目录执行，之后因源码修改做过增量重建和测试重跑。

```bash
export LD_LIBRARY_PATH=/workspace/deps/root/usr/lib/x86_64-linux-gnu
/workspace/deps/root/usr/bin/cmake -S cpp-runtime -B /workspace/build-cpp \
  -DCMAKE_PREFIX_PATH=/workspace/deps/root/usr -DCMAKE_BUILD_TYPE=Debug
/workspace/deps/root/usr/bin/cmake --build /workspace/build-cpp -j4
QT_PLUGIN_PATH=/workspace/deps/root/usr/lib/x86_64-linux-gnu/qt6/plugins \
  /workspace/deps/root/usr/bin/ctest --test-dir /workspace/build-cpp --output-on-failure

/workspace/deps/root/usr/bin/cmake -S cpp-runtime -B /workspace/build-core-only \
  -DBUILD_HMI=OFF -DCMAKE_PREFIX_PATH=/workspace/deps/root/usr -DCMAKE_BUILD_TYPE=Release
/workspace/deps/root/usr/bin/cmake --build /workspace/build-core-only -j4
/workspace/deps/root/usr/bin/ctest --test-dir /workspace/build-core-only --output-on-failure
npm test --prefix runtime
```

最终源码另从空目录 `/workspace/cpp-final-build` 完整配置、Release 构建并通过 3/3 CTest：

```bash
/workspace/deps/root/usr/bin/cmake -S cpp-runtime -B /workspace/cpp-final-build \
  -DCMAKE_PREFIX_PATH=/workspace/deps/root/usr -DCMAKE_BUILD_TYPE=Release
/workspace/deps/root/usr/bin/cmake --build /workspace/cpp-final-build -j4
QT_PLUGIN_PATH=/workspace/deps/root/usr/lib/x86_64-linux-gnu/qt6/plugins \
  /workspace/deps/root/usr/bin/ctest --test-dir /workspace/cpp-final-build --output-on-failure
```

## 结果

| 验证 | 结果 |
|---|---|
| Qt 构建的 CTest | 3/3 通过：domain、acceptance、hmi_refresh |
| 无 Qt 核心构建的 CTest | 2/2 通过：domain、acceptance |
| 旧 Node runtime | 49/49 通过，0 fail / skip |
| 纯领域 | 顺序推进、阻塞时禁止完成、重复完成拒绝 |
| 采集闭环 | 三个合成实体正常状态、活动异常及恢复、坏质量 NULL、断链恢复 |
| SQLite / 生命周期 | 已提交历史重启保留；杀死测试自己启动的采集子进程后保留历史和释放租约；第二写者拒绝；诊断退出后继续采集 |
| 只读边界 | 输入限额、非法/未知命令、未知目录/schema 拒绝；旧目录哨兵内容未改；过期证据标记 |
| Qt 实际运行 | 真实 Widgets + 事件循环 + SQLite 异步查询；观察活动异常、坏质量、断链恢复；HMI 退出后采集仍运行 |

Qt offscreen 运行有字体缓存不可写和 propagateSizeHints 提示；截图实际渲染可读。配置时提示未找到可选 XKB；Widgets/offscreen 构建和运行成功，不据此声称 X11 桌面已验收。

## 真实 Qt 截图复现

截图是运行中 QWidget::grab() 输出的 PNG，不是设计稿。截图不入 Git。两个产物为 `cpp-hmi-normal.png` 与 `cpp-hmi-alarm.png`，均 1100×650。程序可选第三/第四参数用于限时验证与截图；截图条件未出现或未成功保存时限时模式返回非零。

已使用下列等价命令在同一个独立模拟采集进程存活期间顺序运行两个 HMI（实际调用由 Python subprocess 管理并在 finally 中停止它自己创建的采集进程）：

```bash
export LD_LIBRARY_PATH=/workspace/deps/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=/workspace/deps/root/usr/lib/x86_64-linux-gnu/qt6/plugins
export QT_QPA_PLATFORM=offscreen
# 终端 1，新专用目录
/workspace/build-cpp/collector /tmp/cpp-screenshot-data 60 500
# 终端 2
/workspace/build-cpp/hmi /tmp/cpp-screenshot-data 9500 /workspace/cpp-hmi-normal.png normal
/workspace/build-cpp/hmi /tmp/cpp-screenshot-data 9500 /workspace/cpp-hmi-alarm.png alarm
```

normal 选择新鲜、good、cleared 的真实观测；alarm 选择新鲜的 active 观测。另支持 `unavailable` 捕获坏质量/过期状态。截图时间和序号随运行变化，内容均为本程序生成的合成数据。

## 未验收

未在 Windows、目标 Qt 6.11.x、MinGW GCC 16.2 或目标 CMake 4.4.4 运行；未验收实际桌面驱动、部署依赖收集、非管理员干净 Windows 安装、物理掉电、长时容量、真实 OPC UA 或其他外部适配器。没有新增 open62541；旧驱动的集成/协议测试未在本轮重跑。没有对旧 HTTP 五工具宣称 C++ 兼容。
