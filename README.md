# 物流上位机

C++17 / Qt Widgets 实验分支。使用单工位合成物流数据演示采集、工作台、历史、报警证据和 MCP 只读诊断。

## 快速开始

在仓库根目录执行。需要 C++17 编译器、CMake、SQLite3、nlohmann/json 3.11+、Qt 6.4+ Widgets 和用于测试的 Python 3。无 Qt 时加 `-DBUILD_HMI=OFF`；只构建采集和 CLI 时还可加 `-DBUILD_MCP=OFF`。

```bash
cmake -S cpp-runtime -B build-logistics -DCMAKE_BUILD_TYPE=Release
cmake --build build-logistics -j4
ctest --test-dir build-logistics --output-on-failure
```

打开独立终端。采集器启动后将实际临时目录复制给界面和诊断。

```bash
# 终端 1
data=$(mktemp -d)
printf '%s\n' "$data"
./build-logistics/collector "$data"

# 终端 2（替换 DATA_DIR）
./build-logistics/hmi DATA_DIR
# 终端 3（替换 DATA_DIR）
./build-logistics/diagnose DATA_DIR snapshot-json 5
```

在 MCP 客户端设置 stdio 命令为 `/path/to/build-logistics/logistics-mcp`，参数为 `["DATA_DIR"]`。四项工具都要求 `{"station":"stn.140"}`。例如调用 `status` 可区分采集心跳、最近的帧及其当前可用性。MCP 客户端退出后采集继续运行。

[Windows 构建与工作台细节](cpp-runtime/README.md) · [可选的内网 HTTPS MCP](cpp-runtime/deploy/README.md) · [架构](ARCHITECTURE.md) · [验证记录](cpp-runtime/VALIDATION.md) · [迁移边界与旧版基准](docs/cpp-migration.md)
