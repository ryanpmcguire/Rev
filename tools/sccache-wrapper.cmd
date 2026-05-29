@echo off
setlocal

set "SCCACHE_DIR=%~dp0..\.sccache"
if not exist "%SCCACHE_DIR%" mkdir "%SCCACHE_DIR%"

"%~dp0sccache\sccache-v0.15.0-x86_64-pc-windows-msvc\sccache.exe" %*
