# Windows x64 便携预览构建

适用范围为本机模拟。包含采集器、Qt 工作台、诊断、归档恢复和只读 MCP stdio。运行时不依赖 MSYS2 安装，随包附带实际需要的 Qt、SQLite 与 MinGW DLL。`point-table.sqlite` 与可执行文件放在同一目录，工作台从这里读取 140 点表。默认不监听网络，包内没有 HTTP 适配器、设备驱动、证书或模拟数据目录。

## 构建与验证

`windows-preview.yml` 只在实验分支的相关提交上运行，无发布权限。第一台 Windows runner 使用 MSYS2 MINGW64 和 CMake MinGW Makefiles 构建，按 PE 导入收集依赖及 Qt Windows 插件，记录源码 SHA 和包版本，生成 ZIP 与 SHA-256。第二台独立 Windows runner 核对哈希并解压到带空格的路径，再用系统 PATH 验证原生窗口、实时刷新、只读 CLI、MCP 工位拒绝和归档恢复。只有两阶段通过才产生 `windows-preview-verified` 工件，供维护者核对后手动发布为预发布版。

本机复验可在非管理员 MSYS2 MINGW64 终端执行。暂存目录必须是新目录。

```bash
pacman -S --needed git mingw-w64-x86_64-{binutils,gcc,cmake,make,qt6-base,sqlite,nlohmann-json}
cmake -S cpp-runtime -B build-preview -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_MCP_HTTP=OFF -DBUILD_OPCUA_LOOPBACK_FIXTURE=OFF
cmake --build build-preview --parallel 4
bash cpp-runtime/deploy/windows/stage.sh build-preview preview-stage
```

在 PowerShell 中压缩并复跑。测试 Python 仅用于验收，无需随包交付。

```powershell
Compress-Archive -Path preview-stage/* -DestinationPath Logistics-Preview-Windows-x64.zip
Get-FileHash .\Logistics-Preview-Windows-x64.zip -Algorithm SHA256
Expand-Archive .\Logistics-Preview-Windows-x64.zip -DestinationPath '.\Logistics clean preview'
python cpp-runtime/deploy/windows/smoke.py '.\Logistics clean preview'
```

只在通过验收的 ZIP 上发布，并附同名 `.sha256` 文件。发布前核对 `BUILD-INFO.txt`、`FILES.txt`、验证构建的源码 SHA、依赖归属和第三方许可证。解压后双击 `Logistics.exe`，按随包 `README.txt` 查看后台日志及采集进程 PID。`START.cmd` 作为脚本兼容入口。Windows CI 也从启动器验证隐藏采集、写入本地输出和关闭 HMI 后继续采样。仅 ASCII 路径中的空格有自动化覆盖。厂区、服务安装和受保护 HTTPS 部署仍需单独验收。
