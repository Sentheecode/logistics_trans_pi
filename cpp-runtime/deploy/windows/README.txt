物流上位机 Windows x64 便携预览版

1. 完整解压到可写目录，例如 D:\Logistics Preview。包内文件需保持原有相对位置。
2. 双击 START.cmd。它会独立启动模拟采集器与工作台，并在包目录下保存 simulation-data。
3. 关闭工作台后，采集仍继续。要结束采集，在采集器控制台按 Ctrl+C。
4. 在 PowerShell 打开解压目录后可以单独运行以下命令。

   .\diagnose.exe .\simulation-data snapshot-json 5
   .\simulation-archive.exe backup .\simulation-data .\simulation-backup

MCP 客户端使用 stdio。命令填写 logistics-mcp.exe 的绝对路径，参数填写模拟数据目录的绝对路径。
四项工具使用模拟工位 stn.140。本预览只使用模拟数据，无 HTTP 监听、现场接口或 Windows 服务。

BUILD-INFO.txt 记录源码 SHA 与依赖版本。FILES.txt 列出包内容。
第三方库动态链接，可替换兼容版本。MSYS2 对应包及源码与第三方许可证见 BUILD-INFO.txt 和 licenses。
中文界面字体嵌在 hmi.exe 内（Noto Sans CJK SC 子集，许可证见 licenses/noto）。更换工作台时替换 hmi.exe，并保持它与 Qt、MinGW、SQLite DLL 及 platforms 插件在同一目录。
