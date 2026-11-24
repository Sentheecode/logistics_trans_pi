# 当前验证记录

基线 [0d1a429](https://github.com/Sentheecode/logistics_trans_pi/tree/0d1a429968240060a5e05a427a5816f0977fbb5d)。测试使用新的空目录、合成 SQLite 数据和临时环回证书。当前 Qt 布局未改。旧版截图和阶段验证留在上述历史提交，不作为本轮验收证据。

## 复验

Linux 实测 GCC 14.2、CMake 3.31.6、SQLite 3.46.1、Qt 6.8.2、Python 3.13.5、Node 24.22.3、nlohmann/json 3.11.3、libmicrohttpd 1.0.1、OpenSSL 3 和 nginx 1.26.3。Qt 测试使用 offscreen。头文件和库来自独立 Debian 安装前缀，未纳入仓库。Qt 配置提示缺少 XKB 开发文件；隔离运行有两个位置的 OpenSSL 库，配置提示搜索顺序。实测使用的独立前缀及包版本见下文。

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
npm test --prefix runtime
git diff --check
```

启用 nginx 网关测试须安装 nginx、openssl 命令，或指定 `NGINX_EXECUTABLE`。OPC UA 使用 open62541 1.4.11 的 mbedTLS 构建（Debian runtime/dev/tools 包版本 1.4.11.1-1）。可用 `open62541_DIR` 指向其安装位置。若依赖安装在独立前缀，设置 `CMAKE_PREFIX_PATH`，程序运行环境需加载相同前缀的共享库。

Release 从空构建目录配置并编译。Qt + stdio **10/10**，关闭 Qt/MCP 的最小核心 **7/7**，开启 HTTP、nginx 和 OPC UA 的无 Qt 构建 **12/12**。旧 Node **49/49**，Markdown 相对文件链接与 `git diff --check` 通过。

## 已覆盖行为

- `mcp_stdio` 使用独立进程读写 MCP JSON-RPC，覆盖初始化、工具清单、状态、报警、历史分桶和日志元数据，拒绝越权、无效参数、未知工具、重复 JSON 键和过大输入。验证旧 schema、坏质量置空、过期/缺失证据、查询预算和关闭诊断时采集仍持续。
- `mcp_http` 使用实际 HTTP 后端和两个独立客户端，验证标准请求、协议版本、单路径、Bearer、Host/Origin、请求大小和跨工位拒绝。网关用 `mcp_https_gateway` 启动真实 nginx，使用临时 CA；有权限证书完成调用，同 CA 未授权证书和无证书客户端被拒绝。审计不包含日志正文、请求参数或令牌。
- `opcua_loopback` 验证实际 Read、质量和两种时间戳，以及缺失节点无值。`opcua_secure_loopback` 使用双边应用证书、Basic256Sha256/SignAndEncrypt，验证证书拒绝、服务离线失败及服务重启后新会话重建订阅。测试服务器限制在 IPv4 环回，默认采集仍走模拟适配器。
- `logs` 验证日志容量、事务故障后的恢复，并通过 SQLite 备份恢复至空目录，核验完整性、日志和计数，采集重启继续原有帧序号。旧库、旧 JSONL 和生产数据均未改动。
- 核心回归覆盖模拟采集、手自动、历史迁移、范围、日志、监督回收与有限重启。Qt 验证真实控件、大小、隔离和刷新。旧 Node 的五项 HTTP 契约现在由测试直接调用，独立于已删除的 Pi 包装器。

Windows、真实厂区、生产证书和跨主机网络策略未验收。能力差异见 [迁移记录](../docs/cpp-migration.md)；远程接入与回退见 [部署说明](deploy/README.md)。
