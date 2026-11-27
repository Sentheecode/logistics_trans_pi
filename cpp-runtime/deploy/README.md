# 本机 MCP 与可选内网入口

`logistics-mcp DATA_DIR` 默认使用 stdio。MCP 客户端应加载该命令，参数由用户在本机配置。使用专用 C++ 模拟目录，并让诊断进程及客户端使用低权限账户。路径仅由启动方指定，工具参数不提供路径。采集、HMI 和 MCP 各自独立启动。

## Linux 环回验收

需要 nlohmann/json 3.11 或更高版本。可选 HTTP 另需 libmicrohttpd（验收版本 1.0.1）与 OpenSSL 3。启用 HTTPS 测试需要本机的 nginx 和 openssl 可执行文件。常规构建不会生成 HTTP 适配器，启动 stdio 不监听端口。

```bash
cmake -S cpp-runtime -B build-mcp -DBUILD_HMI=OFF -DBUILD_MCP_HTTP=ON
cmake --build build-mcp -j4
ctest --test-dir build-mcp -R '^mcp_' --output-on-failure
```

若从隔离前缀安装库，配置时用 `CMAKE_PREFIX_PATH` 指向安装前缀。也可用 `-DNGINX_EXECUTABLE=/path/to/nginx` 指定 nginx。测试在两个环回端口启动实际 C++ 适配器和 nginx，各自用临时 PKI 与随机凭据，再通过独立客户端进程检查许可范围。

## 管理员配置单一 HTTPS 入口

示例 [nginx 配置模板](nginx-mcp.conf.in) 用于单工位的一机一入口。内网部署前须审批代理、证书、访问人员、网段和防火墙。工位 C++ 进程一直绑定 IPv4 环回；部署者只能将批准的 HTTPS 接口对管理子网开放。

1. 配置独立运行账户、专用模拟库与权限。诊断读取 SQLite/WAL，按部署的最小权限验证访问；网关无需数据库权限。
2. 为内网 DNS 名配置受信服务端证书。使用专用客户端 CA 签发证书，CA 私钥单独保管。模板信任一个直接签发客户端证书的 CA，以该 CA 的证书序列号白名单授权到本工位。多个工位用独立配置和凭据。撤销时还需更新证书白名单及 CA 管理策略。
3. 使用 `openssl rand -hex 32` 生成只在代理与本机后端之间使用的随机凭据。用其填充 `@BACKEND_TOKEN@`，同一值由受保护的进程环境提供给 `LTP_MCP_GATEWAY_TOKEN`。将 `@GATEWAY_BIND@`、`@GATEWAY_NAME@`、`@GATEWAY_PORT@`、证书路径、客户端序列号、后端端口及审计路径替换为受审批的配置。渲染产物含凭据，应只供必要的服务账户读取。若客户端发送 Origin，须在后端用 `LTP_MCP_ALLOWED_ORIGIN` 配置唯一确切来源。默认只接受缺省 Origin 的请求。本示例没有跨域浏览器 CORS 接入。
4. 本机启动 `./build-mcp/logistics-mcp-http --enable-loopback PORT DATA_DIR`。先用 `nginx -t -p PRIVATE_PREFIX -c PRIVATE_CONFIG` 校验，再显式启动 nginx。检查后端只监听 `127.0.0.1`，外部仅批准的 HTTPS 入口。路由为 `/mcp`；后端按请求验证认证，不使用来访源地址做身份判断。
5. 再用两个客户端执行 MCP 初始化、工具清单和合法/越权调用。无证书、同 CA 未授权证书、跨工位、错误 Host/Origin 和超限都须拒绝。还要验证客户端信任服务端证书、审计仅记录最小字段、停止网关后采集继续。紧急回退先停网关并撤销防火墙规则，再停 HTTP 适配器，保持采集和本机 stdio 独立。

此路径使用双向 TLS 身份验证，客户端必须支持配置客户端证书。无企业 SSO/OAuth 发现、SSH 隧道或跨主机网关后端实现。网关重写后端 Bearer 头，客户端从不持有后端凭据。厂区、Windows 和实际网络策略未验收。[MCP 传输规范](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)、[nginx TLS 文档](https://nginx.org/en/docs/http/ngx_http_ssl_module.html)

## 八台 Windows 工控机的内网规划

目标是每台工控机运行 C++ EXE，并经批准的传输组件提供一个独立 HTTPS MCP 入口。用户后续在开发机实现独立 MCP Gateway，统一读取八个入口。当前 HTTP 适配器只在 Linux 构建，示例 TLS 网关只在 Linux 环回验证。Windows 传输组件、受信 CA 部署和实际内网路径需要在试点单独验收。开发机 Gateway 不属于本仓库交付。

- 用可信配置绑定 **工控机 ID → HTTPS 端点 → 允许的工位 ID**。授权和结果归属都按工控机与工位二级键校验；不同工控机即使工位 ID 相同也保持独立。工控机 ID 来自 Gateway 的可信配置映射，不从工具提示或来访源 IP 推断。当前 MCP 工具仅接收已知模拟工位，未增加工控机 ID 参数。
- 每个端点使用独立服务端证书、授权记录及代理后端凭据。Gateway 按工控机申请最小只读权限；一台撤销证书或改授权时，其余端点仍可用。对各端点独立记录新鲜度、拒绝、超时、负载与连接状态。
- 首先采用有界只读轮询，按需建立连接或复用短时连接。每台配置独立超时、限流、熔断和带抖动的退避重连。单台失败返回该台证据不可用，避免拖住其他端点。无需常驻八条连接。长连接或订阅仅在两端明确支持，且已验证恢复、授权和资源预算后启用。

试点按以下顺序进行，每阶段保留本次证书、网络、权限和负载的验收记录。

1. **一台 Windows 工控机 + 另一台内网客户端**。以非管理员构建/运行 EXE，验证同时采集时的 SQLite 锁和恢复、独立进程重启、证书信任及撤销、防火墙准入、未认证和越权拒绝。
2. **双机混线和故障隔离**。使用相同工位标识的两个合成实例，验证工控机与工位两级不会串线。模拟单台断网、证书拒绝和重启，确认另一台的结果和采集不受影响。
3. **八台**。验证轮询周期、总并发和单台退避预算、证书轮换及单机回退。确认入口关闭后不留外部监听，采集与本机诊断继续运行。
