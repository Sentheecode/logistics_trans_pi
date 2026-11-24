物流上位机 Windows x64 便携预览版

包内只有一个程序：Logistics.exe。Qt、MinGW 和 SQLite 仍以 DLL 放在同一目录。中文字体是 fonts 目录里的 Noto Sans CJK SC 文件，不在 exe 内部。Windows 上还会使用微软雅黑，并允许缺字回退。

1. 完整解压到可写目录，例如 D:\Logistics Preview。包内文件需保持原有相对位置，fonts 目录不要拆走。
2. 双击 START.cmd。它会用同一个 exe 分别启动模拟采集器与工作台，并在包目录下保存 simulation-data。
3. 关闭工作台后，采集仍继续。要结束采集，在采集器控制台按 Ctrl+C。
4. 在 PowerShell 打开解压目录后可以单独运行以下命令。

   .\Logistics.exe diagnose .\simulation-data snapshot-json 5
   .\Logistics.exe archive backup .\simulation-data .\simulation-backup

MCP 客户端使用 stdio。命令填写 Logistics.exe 的绝对路径，参数先写 mcp，再写模拟数据目录的绝对路径。
四项工具使用模拟工位 stn.140。本预览只使用模拟数据，无 HTTP 监听、现场接口或 Windows 服务。

BUILD-INFO.txt 记录源码 SHA 与依赖版本。FILES.txt 列出包内容。
第三方库动态链接，可替换兼容版本。MSYS2 对应包及源码与第三方许可证见 BUILD-INFO.txt 和 licenses。
字体许可证见 licenses/noto。
