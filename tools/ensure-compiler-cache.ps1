$ErrorActionPreference = "Stop"

$SccacheVersion = "0.15.0"
$CcacheVersion = "4.13.6"
$ToolsDir = $PSScriptRoot

function Install-ArchiveTool {
    param(
        [string]$Name,
        [string]$Version,
        [string]$Url,
        [string]$ZipPath,
        [string]$ExtractDir,
        [string]$ExePath
    )

    if (Test-Path $ExePath) {
        Write-Host "$Name already installed at $ExePath"
        return $ExePath
    }

    Write-Host "Downloading $Name $Version..."
    New-Item -ItemType Directory -Force -Path $ExtractDir | Out-Null
    curl.exe -L -o $ZipPath $Url
    Expand-Archive -Force -Path $ZipPath -DestinationPath $ExtractDir

    if (-not (Test-Path $ExePath)) {
        throw "$Name.exe not found after extraction"
    }

    return $ExePath
}

$SccacheExe = Install-ArchiveTool `
    -Name "sccache" `
    -Version $SccacheVersion `
    -Url "https://github.com/mozilla/sccache/releases/download/v$SccacheVersion/sccache-v$SccacheVersion-x86_64-pc-windows-msvc.zip" `
    -ZipPath (Join-Path $ToolsDir "sccache.zip") `
    -ExtractDir (Join-Path $ToolsDir "sccache") `
    -ExePath (Join-Path $ToolsDir "sccache/sccache-v$SccacheVersion-x86_64-pc-windows-msvc/sccache.exe")

$CcacheExe = Install-ArchiveTool `
    -Name "ccache" `
    -Version $CcacheVersion `
    -Url "https://github.com/ccache/ccache/releases/download/v$CcacheVersion/ccache-$CcacheVersion-windows-x86_64.zip" `
    -ZipPath (Join-Path $ToolsDir "ccache.zip") `
    -ExtractDir (Join-Path $ToolsDir "ccache") `
    -ExePath (Join-Path $ToolsDir "ccache/ccache-$CcacheVersion-windows-x86_64/ccache.exe")

& $CcacheExe -M 10G | Out-Null
& $SccacheExe --start-server | Out-Null
Write-Host "Installed sccache to $SccacheExe"
Write-Host "Installed ccache to $CcacheExe"
