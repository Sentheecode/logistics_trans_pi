# 本机只读诊断

以项目根目录为当前目录。下面的测试注入模拟超温，验证五个工具，并检查日志完整窗口与空缺窗口的区别。

```bash
node --test runtime/test/diagnostic-api.test.mjs
```

查询服务属于 runtime 的只读部分，可作为单独进程启动。使用已有 runtime.sqlite 所在的数据目录。SQLite 以只读方式打开，HTTP 仅监听 127.0.0.1。端口传 0 时由系统分配，启动时打印实际 URL。终端内先设置数据目录及诊断令牌，例如在 Bash 中运行：

```bash
read -r -p 'runtime 数据目录: ' LTP_DATA_DIR
export LTP_DIAGNOSTIC_TOKEN="$(node -p 'require("node:crypto").randomBytes(32).toString("hex")')"
node runtime/src/diagnostic-api.mjs "$LTP_DATA_DIR" 0
```

调用方从本机部署配置获得相同令牌并设置 LTP_DIAGNOSTIC_URL 为上述 URL。仓库中的 agent/diagnose.sh 使用锁定的 pi 1.5.2，仅启用五个诊断工具。实际调用模型需要另行配置模型通道。目前自动化验证直接执行工具，经 HTTP 读取数据。要在已有上述环境变量的 Bash 中查证据：

```bash
curl --fail-with-body --silent --show-error --get \
  --header "Authorization: Bearer $LTP_DIAGNOSTIC_TOKEN" \
  --data-urlencode 'condition=active' "$LTP_DIAGNOSTIC_URL/api/v1/alarms"
```

接口路径为 /api/v1/tags、history、alarms、logs、correlations（共用前缀）。时间参数 from、to 使用左闭右开 UTC 窗口，最多毫秒精度，查询跨度最多一天。多个标识使用重复的 tag_ids 或 device_ids 参数。单次最多 64 个标识、每类最多返回 500 条，截断通过 truncated 明示，日志另报精确 matched_count。当前版本提供 raw 历史；聚合分辨率和翻页游标尚待实现。若日志损坏、超出精确计数预算或查询失败，调用方收到错误。

query_tags 反映最后一次采样。history 按事件时间筛选，correlations 按接收时间筛选。日志覆盖来自记录区间、时钟分段及轮转信息。历史和报警缺少完整性证明时返回 unknown，关联结果只列观测证据。运行账户应只有所需文件读取权限。此 HTTP 服务供本机部署，Windows 服务注册另行验证。
