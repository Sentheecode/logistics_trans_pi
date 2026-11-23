# 本次验证记录

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
