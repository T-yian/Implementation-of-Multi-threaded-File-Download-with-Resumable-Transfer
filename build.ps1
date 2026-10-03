# 编译 build/downloader.exe（调试版：带符号、无优化）
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$Gpp = "E:\mingw64\bin\g++.exe"
$Out = "build\downloader.exe"

if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

# -g 调试符号  -O0 关闭优化（保证断点行为与源码一致）
# -finput-charset=UTF-8 源码是 UTF-8 中文注释
& $Gpp -std=c++17 -g -O0 -Wall -Wextra -finput-charset=UTF-8 -fexec-charset=UTF-8 `
    main.cpp `
    http_client.cpp `
    file_util.cpp `
    progress.cpp `
    progress_display.cpp `
    download_manager.cpp `
    -lwinhttp -lws2_32 `
    -o $Out

if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] Build failed." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] Build succeeded: $Out" -ForegroundColor Green
