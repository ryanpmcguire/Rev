param(
    [string]$Compiler = 'clang++',
    [string]$OutputDirectory = (Join-Path $env:TEMP 'rev-directx-validation'),
    [switch]$ShaderParity
)
$ErrorActionPreference = 'Stop'
$backend = Split-Path $PSScriptRoot -Parent
$rev = [IO.Path]::GetFullPath((Join-Path $backend '../../..'))
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
# Clang may map pre-existing PCMs discovered through the prebuilt-module path.
# A fresh build directory avoids reading a previous module while overwriting it.
$output = Join-Path $outputRoot ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $output | Out-Null
$common = @('--target=x86_64-pc-windows-msvc', '-std=c++23', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH',
    '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-DGLEW_STATIC', '-DPROJECT_ROOT="."', '-Wno-unused-command-line-argument',
    "-I$rev/external", "-fprebuilt-module-path=$output")

function Invoke-Compiler([string[]]$Arguments) {
    & $Compiler @common @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Compiler failed: $Arguments" }
}

# Compile the real module dependencies, including the existing NativeWindow.
$modules = @{}
Get-ChildItem "$rev/source" -Recurse -Filter '*.ixx' | ForEach-Object {
    if ($_.Name -match '\.(OpenGL|Metal|Vulkan|mac|lnx)\.ixx$' -or $_.Name -eq 'Vulkan.ixx') { return }
    $text = [IO.File]::ReadAllText($_.FullName)
    if ($text -match '(?m)^export module ([\w.]+);') { $modules[$Matches[1]] = $_.FullName }
}
$built = @{}
function Build-Module([string]$Name) {
    if ($built.ContainsKey($Name)) { return }
    if (!$modules.ContainsKey($Name)) { throw "Missing module $Name" }
    $path = $modules[$Name]
    $text = [IO.File]::ReadAllText($path)
    foreach ($match in [regex]::Matches($text, '(?m)^(?:export )?import ([\w.]+);')) {
        Build-Module $match.Groups[1].Value
    }
    Invoke-Compiler @('-x', 'c++-module', '--precompile', $path, '-o', "$output/$Name.pcm")
    Invoke-Compiler @('-c', "$output/$Name.pcm", '-o', "$output/$Name.obj")
    $built[$Name] = $true
}
Get-ChildItem $backend -Filter '*.ixx' | ForEach-Object {
    $text = [IO.File]::ReadAllText($_.FullName)
    if ($text -match 'export module ([\w.]+);') { Build-Module $Matches[1] }
}
$objects = @($built.Keys | ForEach-Object { "$output/$_.obj" })
Invoke-Compiler (@("$PSScriptRoot/ModuleConsumer.cpp", '-o', "$output/ModuleConsumer.exe") + $objects +
    @("$rev/external/glew/bin/win/x64/glew32s.lib", '-ld3d12', '-ldxgi', '-ld3dcompiler', '-luser32', '-lgdi32', '-lopengl32', '-lshell32'))

$executable = Join-Path $output 'DirectXSmoke.exe'
Invoke-Compiler @('-Wall', '-Wextra', '-Werror', '-Wno-missing-designated-field-initializers',
    "$PSScriptRoot/Smoke.cpp", '-o', $executable, '-ld3d12', '-ldxgi', '-ld3dcompiler', '-luser32')
& $executable
if ($LASTEXITCODE -ne 0) { throw 'DirectX smoke test failed' }
& python "$rev/scripts/PortDirectXShaders.py" --check
if ($LASTEXITCODE -ne 0) { throw 'Primitive HLSL is stale' }
Invoke-Compiler @("$PSScriptRoot/CompileShaders.cpp", '-o', "$output/CompileShaders.exe", '-ld3d12', '-ldxgi', '-ld3dcompiler')
& python "$rev/scripts/ValidateDirectXShaders.py" "$output/CompileShaders.exe"
if ($LASTEXITCODE -ne 0) { throw 'Primitive shader compilation/layout validation failed' }
if ($ShaderParity) {
    Invoke-Compiler @("$PSScriptRoot/ShaderParity.cpp", '-o', "$output/ShaderParity.exe",
        "$rev/external/glew/bin/win/x64/glew32s.lib", '-ld3d12', '-ldxgi', '-ld3dcompiler', '-luser32', '-lgdi32', '-lopengl32')
    & "$output/ShaderParity.exe" "$rev/source/Graphics/Primitives"
    if ($LASTEXITCODE -ne 0) { throw 'GLSL/HLSL rendered parity failed' }
}
Write-Output 'All DirectX modules and module consumer compiled; WARP/debug-layer smoke passed.'
Write-Output "Validation artifacts: $output"
