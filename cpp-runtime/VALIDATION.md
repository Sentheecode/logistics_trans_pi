# 当前验证记录

## 2026-11-25 排序目录与点表展示更新

本轮复核 Cursor 的资料清单、静态目录及仓库 [140 点表](../data/baselines/station-140.csv)。旧 [topology.json](../data/baselines/topology.json) 保留需求归档时的快照，当前浏览目录由 `catalog.cpp` 构建。原始图纸、其他站工作簿及逐点实机验证不在此次检查范围。工位分组用于资料导航。

本轮在 Linux 使用下段组合构建和 CTest 命令，通过 15/15 项。独立导出内置点表后，按顺序比对 CSV 的来源行号、地址和名称，116 行全部一致。目录回归核对八个排序工位的组别和空模拟源，控件回归检查点表默认 75 行与完整 116 行、筛选组合和跨工位隔离。正常页 1280×800，告警及排序资料页 1100×650，均用全新模拟库真实渲染。

## 2026-11-25 工作台与资料核对

审阅 Cursor 的资料提交 `b640bda` 和界面提交 `297dd00`，从实验分支 `ed5cbc0` 选择性整合。输入为该分支提交的摘要与代码，未经独立验收的原始资料不进入工位配置。截图来自运行中的 Qt Widgets 和新建的本地模拟库。

```bash
cmake -S cpp-runtime -B /tmp/logistics-nov25-review -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_MCP_HTTP=ON -DBUILD_OPCUA_LOOPBACK_FIXTURE=ON
cmake --build /tmp/logistics-nov25-review -j3
ctest --test-dir /tmp/logistics-nov25-review --output-on-failure -j2
```

以上组合注册并通过 15/15 项 CTest，包括可选的环回 HTTPS 与加密 OPC UA 夹具。Linux Qt 6.8.2 offscreen 的正常视图为 1280×800，告警和排序区为 1100×650。联动、点选、手动待步、单步、断链、关闭诊断保留工位、同 PLC 的相邻工位隔离均由控件测试覆盖。新增检查覆盖采集器重启但尚未提交新帧、读取中断后的未知状态、排序资料字段与回执的区分。Windows CI 单独构建与解压验证，Windows 11 工控机、厂内网络和真实设备仍待验收。

## 迁移基线的验证记录

下列命令与结果对应此前的 C++ 迁移验收。测试使用独立目录、合成 SQLite 数据和临时环回证书。



Linux 实测 GCC 14.2、CMake 3.31.6、SQLite 3.46.1、Qt 6.8.2、Python 3.13.5、nlohmann/json 3.11.3、libmicrohttpd 1.0.1、OpenSSL 3 和 nginx 1.26.3。Qt 测试使用 offscreen。头文件和库来自独立 Debian 安装前缀，未纳入仓库。Qt 配置提示缺少 XKB 开发文件；隔离运行有两个位置的 OpenSSL 库，配置提示搜索顺序。实测使用的独立前缀及包版本见下文。

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

Windows EXE 构建、运行、文件锁和 SQLite 恢复，真实厂区、正式证书、单机到双机再到八台的内网验收均未执行。能力差异见 [迁移记录](../docs/cpp-migration.md)；远程接入与回退见 [部署说明](deploy/README.md)。

Windows 便携包的单独验收入口见 [Windows 预览构建](deploy/windows/README.md)。Windows 结果以该提交的独立构建与解压验证为准。
