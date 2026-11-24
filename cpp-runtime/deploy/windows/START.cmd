@echo off
setlocal DisableDelayedExpansion
if not defined LTP_PREVIEW_DATA_DIR set "LTP_PREVIEW_DATA_DIR=%~dp0simulation-data"
start "Logistics Collector" /D "%~dp0." "%~dp0collector.exe" "%LTP_PREVIEW_DATA_DIR%"
start "Logistics Workbench" /D "%~dp0." "%~dp0hmi.exe" "%LTP_PREVIEW_DATA_DIR%"
