@echo off
chcp 65001 >nul
cd /d %~dp0..

echo ============================================
echo  Test: BIG file (10 MB), real multi-thread
echo  Let it run for a while, then press Ctrl+C
echo  to interrupt. Run this bat AGAIN to see
echo  the resume (breakpoint) feature.
echo ============================================
echo.
build\downloader.exe probe "https://proof.ovh.net/files/10Mb.dat"
echo.
build\downloader.exe download "https://proof.ovh.net/files/10Mb.dat" "%~dp0downloads\10Mb.dat" 4
echo.
echo Saved to: %~dp0downloads\10Mb.dat
pause
