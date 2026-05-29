@echo off
setlocal

set "CCACHE_DIR=%~dp0..\.ccache"
set "CCACHE_BASEDIR=%~dp0.."
set "CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime"
if not exist "%CCACHE_DIR%" mkdir "%CCACHE_DIR%"

"%~dp0ccache\ccache-4.13.6-windows-x86_64\ccache.exe" %*
