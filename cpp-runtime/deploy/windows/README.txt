物流上位机 Windows x64 便携预览版

1. 完整解压到可写目录，例如 D:\Logistics Preview。包内文件需保持原有相对位置。point-table.sqlite 必须留在 exe 同目录，工作台从该文件读取点表。
2. 双击 Logistics.exe。它会在后台启动模拟采集器并打开工作台，默认在包目录下保存 simulation-data。采集库与点表库是两个文件。点表缺失或无法读取时，工作台不会打开。
   进程输出和本次启动的 PID 记录在 preview-logs。工作台“日志”页显示日志索引，正文可用本机 diagnose 查看。
3. 关闭工作台后采集继续。需要结束后台采集时，在任务管理器“详细信息”按 processes.txt 的 PID 核对并结束 collector.exe。
   从命令行单独启动的采集器仍可用 Ctrl+C 停止。
4. 在 PowerShell 打开解压目录后可以单独运行以下命令。

   .\diagnose.exe .\simulation-data snapshot-json 5
   .\simulation-archive.exe backup .\simulation-data .\simulation-backup

MCP 客户端使用 stdio。命令填写 logistics-mcp.exe 的绝对路径，参数填写模拟数据目录的绝对路径。
四项工具使用模拟工位 stn.140。本预览只使用模拟数据，无 HTTP 监听、现场接口或 Windows 服务。

BUILD-INFO.txt 记录源码 SHA 与依赖版本。FILES.txt 列出包内容。
第三方库动态链接，可替换兼容版本。MSYS2 对应包及源码与第三方许可证见 BUILD-INFO.txt 和 licenses。
