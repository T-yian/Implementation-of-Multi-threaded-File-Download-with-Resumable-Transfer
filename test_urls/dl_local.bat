@echo off
chcp 65001 >nul
cd /d %~dp0..

echo ============================================
echo  Test 4: LOCAL server (works offline)
echo  Step 1: open another terminal, cd to a folder
echo          with some files, run:
echo            python -m http.server 8000
echo  Step 2: press any key to continue here
echo ============================================
echo.
pause
echo.
echo NOTE: python's built-in server does NOT support
echo       Range, so this takes the single-thread path.
echo.
build\downloader.exe probe "http://127.0.0.1:8000/test.pdf"
echo.
build\downloader.exe download "http://127.0.0.1:8000/test.pdf" "%~dp0downloads\local_test.pdf" 4
echo.
echo Saved to: %~dp0downloads\local_test.pdf
pause
