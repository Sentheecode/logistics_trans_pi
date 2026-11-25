@echo off
setlocal DisableDelayedExpansion
if not defined LTP_PREVIEW_DATA_DIR set "LTP_PREVIEW_DATA_DIR=%~dp0simulation-data"
start "Logistics Collector" /D "%~dp0." "%~dp0Logistics.exe" collector "%LTP_PREVIEW_DATA_DIR%"
start "Logistics Workbench" /D "%~dp0." "%~dp0Logistics.exe" "%LTP_PREVIEW_DATA_DIR%"
