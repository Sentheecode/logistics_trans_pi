# 本机只读诊断

## C++ 模拟数据（默认）

首选 C++ CLI，运行时只需要 C++ 程序和模拟数据，不需要 Node、Pi 或 HTTP 服务。在仓库根目录执行：

```bash
cmake -S cpp-runtime -B /tmp/ltp-cpp -DBUILD_HMI=OFF -DBUILD_TESTING=OFF
cmake --build /tmp/ltp-cpp -j
workdir=$(mktemp -d)
/tmp/ltp-cpp/collector "$workdir" 16 20
/tmp/ltp-cpp/diagnose "$workdir" snapshot-json 5
```

`snapshot-json [1..40]` 返回保留帧与本机日志尾部；`window-json FROM_UTC_MS TO_UTC_MS [1..40]` 按接收时间查保留记录，区间左闭右开且最长 24 小时。后者仅在 C++ CLI 开放。两种查询均按序号倒序，分开给出心跳、帧新鲜度、质量、是否还有保留结果，以及固定模拟范围 `stn.140`。覆盖始终为 `unknown`。查询超出 SQLite 指令预算会报错，空结果不能证明时间窗完整。日志尾部包括该单实例的模拟操作审计。旧 schema 无日志表时显式报告不可用。

如仍使用已安装的 Pi，可用可选的默认桥。Pi 宿主仍需 Node 和原模型配置。桥只启用 `read_simulation_snapshot`，固定读取部署时指定的本地专用模拟目录，不需要旧 Node HTTP。它运行固定参数的 C++ 子进程，超时五秒、输出最多 256 KiB。

```bash
export LTP_CPP_DIAGNOSE=/tmp/ltp-cpp/diagnose
export LTP_CPP_DATA_DIR="$workdir"
bash agent/diagnose.sh "读 stn.140 的模拟快照，区分最近观察、实时有效性和未知覆盖"
```

旧五工具没有自动映射到 C++。详见[迁移验收表](../docs/cpp-migration.md)。C++ 与旧 Node 使用独立数据目录。

## 显式 legacy（旧 Node HTTP）

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

调用方从本机部署配置获得相同令牌并设置 LTP_DIAGNOSTIC_URL 为上述 URL。调用 `bash agent/diagnose.sh --legacy "问题"` 使用锁定的 Pi 1.5.2，仅启用原五个诊断工具。实际调用模型需要另行配置模型通道。目前自动化验证直接执行工具，经 HTTP 读取数据。要在已有上述环境变量的 Bash 中查证据：

```bash
curl --fail-with-body --silent --show-error --get \
  --header "Authorization: Bearer $LTP_DIAGNOSTIC_TOKEN" \
  --data-urlencode 'condition=active' "$LTP_DIAGNOSTIC_URL/api/v1/alarms"
```

接口路径为 /api/v1/tags、history、alarms、logs、correlations（共用前缀）。时间参数 from、to 使用左闭右开 UTC 窗口，最多毫秒精度，查询跨度最多一天。多个标识使用重复的 tag_ids 或 device_ids 参数。单次最多 64 个标识、每类最多返回 500 条，截断通过 truncated 明示，日志另报精确 matched_count。历史支持 raw、1s、1m、1h。聚合查询起止须对齐相应 UTC 桶边界，backfill_pending 表示还有旧数据待补算。分页游标尚待实现。若日志损坏、超出精确计数预算或查询失败，调用方收到错误。

query_tags 反映最后一次采样。history 按事件时间筛选，correlations 按接收时间筛选。日志覆盖来自记录区间、时钟分段及轮转信息。历史和报警缺少完整性证明时返回 unknown，关联结果只列观测证据。运行账户应只有所需文件读取权限。此 HTTP 服务供本机部署，Windows 服务注册另行验证。
