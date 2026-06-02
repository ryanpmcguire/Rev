@echo off
setlocal EnableExtensions

rem C++ modules (.pcm) and sccache on Windows often race or lock files after
rem interrupted builds. Bypass the cache for those compile lines.
set "SKIP_CACHE=0"
for %%A in (%*) do (
    echo %%~xA | findstr /I /B /R "\.pcm$" >nul 2>nul && set "SKIP_CACHE=1"
    echo %%A | findstr /I /C:"-fmodule-name" /C:"--precompile" /C:"-fprebuilt-module-path" >nul 2>nul && set "SKIP_CACHE=1"
)

if "%SKIP_CACHE%"=="1" goto :run_compiler

set "SCCACHE_DIR=%~dp0..\.sccache"
if not exist "%SCCACHE_DIR%" mkdir "%SCCACHE_DIR%"

"%~dp0sccache\sccache-v0.15.0-x86_64-pc-windows-msvc\sccache.exe" %*
exit /b %ERRORLEVEL%

:run_compiler
set "COMPILER=%~1"
shift
call "%COMPILER%" %*
exit /b %ERRORLEVEL%
