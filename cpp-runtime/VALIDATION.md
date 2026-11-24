# 当前验证记录

本轮只修日志列表的半行，并复跑晨间候选的交互回归。运行页和采集页的已定样式没有重做。日志视口高度按 28px 行对齐，滚到头、中、尾以及筛选后都只显示完整行，正文字号仍是 14px。手自动、单步和断链仍只在采集页。下面「本轮实测」是这次实际跑过的命令。其后「此前记录」来自更早提交，本轮没有重跑 HTTP、nginx 或 OPC UA。公开预发布包本轮不更新，现有 Release `preview-windows-20261124-ui` 仍指向 `5a92900`。

## 本轮实测

Linux 上使用 GCC 13.3.0、CMake 3.28.3、SQLite 3.45.1、Qt 6.4.2、Python 3.12.3、nlohmann/json 3.11.3。Qt 配置提示缺少 XKB 开发文件。`ctest` 使用 offscreen。日志截图和关窗再开使用 xvfb 的 xcb，字体由 exe 旁的 Noto Sans CJK SC 加载，两次 HMI 都报告 `ui_font=Noto Sans CJK SC`、`cjk_glyph=yes`。没有用 `diagnose` 代替界面验收。

```bash
cmake -S cpp-runtime -B /tmp/logistics-qt -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build /tmp/logistics-qt -j$(nproc)
ctest --test-dir /tmp/logistics-qt --output-on-failure
```

结果：**11/11** 通过。`widgets_controls` 在真实控件上走完外装自动、手动、单步、模拟断链、恢复和回到自动；手动和断链期间各切到排序区三再切回；展开 150/160 再收回 140。采集进程的 boot、模式和修订号保持不变，三页不出现 140 的帧。同一测试在 xvfb/xcb 下再跑一遍，1100×650 与 1280×800 的日志视口高度分别是 308 和 448，都能被 28 整除。滚动到头、中、尾时首行贴齐视口顶，底边 3px 没有文字像素。筛选后的可见行同样完整。

另起采集进程，用 sqlite 直接读 `runtime_state`：HMI 关闭后序号从 4 增到 7，boot 仍是 `e6427dfe6fca38cf73c01cf0fe39dc4f`，模式仍是自动，进程还在。再次打开 HMI，boot 不变，序号继续到 10。

这不是 Windows 实机，也不是 DPI 验收。Windows 便携包仍由 `windows-preview.yml` 构建，包内文件名是 `Logistics-Preview-Windows-x64.zip`，内部产物名是 `windows-preview-verified`。本段写下时，这次提交的 Windows 构建尚未在本机执行。

## 此前记录

基线 [89fae7b](https://github.com/Sentheecode/logistics_trans_pi/tree/89fae7b32595dc734f6bf173a393670cc2b1b086)。下列环境与 11/11、8/8、13/13 是该次记录，不是本轮命令的结果。

Linux 记录为 GCC 14.2、CMake 3.31.6、SQLite 3.46.1、Qt 6.8.2、Python 3.13.5、nlohmann/json 3.11.3、libmicrohttpd 1.0.1、OpenSSL 3 和 nginx 1.26.3。Qt 测试使用 offscreen。头文件和库来自独立 Debian 安装前缀，未纳入仓库。Qt 配置提示缺少 XKB 开发文件；隔离运行有两个位置的 OpenSSL 库，配置提示搜索顺序。

```bash
cmake -S cpp-runtime -B /tmp/logistics-qt -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/logistics-qt -j5
ctest --test-dir /tmp/logistics-qt -j4 --output-on-failure

cmake -S cpp-runtime -B /tmp/logistics-min -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_HMI=OFF -DBUILD_MCP=OFF
cmake --build /tmp/logistics-min -j4
ctest --test-dir /tmp/logistics-min -j4 --output-on-failure

cmake -S cpp-runtime -B /tmp/logistics-mcp -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_HMI=OFF -DBUILD_MCP_HTTP=ON -DBUILD_OPCUA_LOOPBACK_FIXTURE=ON
cmake --build /tmp/logistics-mcp -j5
ctest --test-dir /tmp/logistics-mcp -j4 --output-on-failure
git diff --check
```

启用 nginx 网关测试须安装 nginx、openssl 命令，或指定 `NGINX_EXECUTABLE`。OPC UA 使用 open62541 1.4.11 的 mbedTLS 构建（Debian runtime/dev/tools 包版本 1.4.11.1-1）。可用 `open62541_DIR` 指向其安装位置。若依赖安装在独立前缀，设置 `CMAKE_PREFIX_PATH`，程序运行环境需加载相同前缀的共享库。

三个 Release 构建均从空目录配置。最终 Qt + stdio **11/11**，最小核心 **8/8**，带 HTTP、nginx、OPC UA 的无 Qt 组合 **13/13**。Markdown 相对链接及锚点、`git diff --check` 均通过。

旧版对照从固定基准解包到临时目录执行，Node 24.22.3 的 49/49 通过。当前检出的验证命令不依赖 Node。

## 已覆盖行为

- `mcp_stdio` 使用独立进程读写 MCP JSON-RPC，覆盖初始化、工具清单、状态、报警、历史分桶、数值标签与日志元数据，拒绝越权、无效参数、未知工具、重复 JSON 键和过大输入。验证旧 schema、坏质量置空、过期/缺失证据、查询预算和关闭诊断时采集仍持续。
- `mcp_http` 使用实际 HTTP 后端和两个独立客户端，验证标准请求、协议版本、单路径、Bearer、Host/Origin、请求大小和跨工位拒绝。网关用 `mcp_https_gateway` 启动真实 nginx，使用临时 CA；有权限证书完成调用，同 CA 未授权证书和无证书客户端被拒绝。审计不包含日志正文、请求参数或令牌。
- `opcua_loopback` 验证 Int32、Boolean、Double 批量 Read，区分 Good、Uncertain、Bad 与缺失点，核对源和服务端时间戳。`opcua_secure_loopback` 使用双边应用证书、Basic256Sha256/SignAndEncrypt，验证证书拒绝、服务离线失败及服务重启后新会话重建订阅。测试服务器限制在 IPv4 环回，默认采集仍走模拟适配器。
- `logs` 验证日志容量、事务故障后的恢复。独立 `archive` 进程测试从运行中的采集备份，恢复到新目录，核对日志与淘汰计数、清除旧心跳、重启续写及拒绝覆盖、旧格式和超限。旧库、旧 JSONL 和生产数据均未改动。
- 核心回归覆盖模拟采集、手自动、历史迁移、范围、日志、监督回收与有限重启。Qt 验证真实控件、大小、隔离和刷新。新增数值分桶验证半开边界、时间戳回退、有效零、坏质量 null 与 CLI/MCP 输出一致。

Windows 文件锁和 SQLite 恢复、真实厂区、正式证书、单机到双机再到八台的内网验收均未在本轮执行。能力差异见 [迁移记录](../docs/cpp-migration.md)；远程接入与回退见 [部署说明](deploy/README.md)。

Windows 便携包的入口见 [Windows 预览构建](deploy/windows/README.md)。打包后的字形检查以该次 Actions 冒烟输出为准，不能代替目标机上的 DPI 或现场验收。
