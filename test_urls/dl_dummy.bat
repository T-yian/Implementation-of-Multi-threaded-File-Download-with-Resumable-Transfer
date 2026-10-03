@echo off
chcp 65001 >nul
cd /d %~dp0..
if not exist "%~dp0downloads" mkdir "%~dp0downloads"

echo ============================================
echo  Test 1: small file with Range support
echo  (multi-thread download demo)
echo ============================================
echo.
build\downloader.exe probe "https://www.w3.org/WAI/ER/tests/xhtml/testfiles/resources/pdf/dummy.pdf"
echo.
build\downloader.exe download "https://www.w3.org/WAI/ER/tests/xhtml/testfiles/resources/pdf/dummy.pdf" "%~dp0downloads\dummy.pdf" 4
echo.
echo Saved to: %~dp0downloads\dummy.pdf
pause
