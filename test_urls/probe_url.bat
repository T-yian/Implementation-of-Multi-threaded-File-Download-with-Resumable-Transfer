@echo off
chcp 65001 >nul
cd /d %~dp0..

if "%~1"=="" (
    echo Usage: probe_url.bat ^<URL^>
    echo Example: probe_url.bat "https://example.com/file.zip"
    pause
    exit /b 1
)

echo Probing: %~1
echo.
build\downloader.exe probe "%~1"
pause
