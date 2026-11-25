# Windows x64 便携预览构建

适用范围为本机模拟。包含采集器、Qt 工作台、诊断、归档恢复和只读 MCP stdio。运行时不依赖 MSYS2 安装，随包附带实际需要的 Qt、SQLite 与 MinGW DLL。默认不监听网络，包内没有 HTTP 适配器、设备驱动、证书或数据目录。

## 构建与验证

`windows-preview.yml` 在 `experiment/cpp-runtime` 和 UI 预览分支的相关提交上运行。第一台 Windows runner 使用 MSYS2 MINGW64 和 CMake MinGW Makefiles 构建，按 PE 导入收集依赖及 Qt Windows 插件，记录源码 SHA 和包版本，生成 ZIP 与 SHA-256。第二台独立 Windows runner 核对哈希并解压到带空格的路径，再用系统 PATH 验证原生窗口、实时刷新、只读 CLI、MCP 工位拒绝、归档恢复，以及打包后的 `Logistics.exe` 报告 `cjk_glyph=yes`。包内只有这一个 exe。中文字体在旁边的 `fonts` 目录，许可证在 `licenses/noto`。只有两阶段通过才产生 `windows-preview-verified` 工件。`experiment/cpp-runtime` 的推送只上传该工件。`cursor/windows-ui-preview-063d` 在冒烟通过后把这一个 ZIP 发布为 GitHub 预发布版。

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

只在通过验收的 ZIP 上发布这一个文件。包内只有 `Logistics.exe`，Qt 依赖仍是 DLL，中文字体是旁边的 `fonts` 文件。发布前核对 `BUILD-INFO.txt`、`FILES.txt`、验证构建的源码 SHA、依赖归属和第三方许可证。解压后双击 `START.cmd`，或按随包 `README.txt` 启动。仅 ASCII 路径中的空格有自动化覆盖。厂区、服务安装、Windows 字体与 DPI 的实机观感和受保护 HTTPS 部署仍需单独验收。
