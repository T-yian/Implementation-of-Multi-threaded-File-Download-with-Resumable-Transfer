@echo off
chcp 65001 >nul
cd /d %~dp0

echo ============================================
echo   Multi-threaded Downloader Demo
echo ============================================
echo.

echo [1/2] Probe remote file (HEAD request, no download)
echo.
build\downloader.exe probe "https://www.w3.org/WAI/ER/tests/xhtml/testfiles/resources/pdf/dummy.pdf"

echo.
echo [2/2] Download with 4 threads (press Ctrl+C to interrupt)
echo.
build\downloader.exe download "https://www.w3.org/WAI/ER/tests/xhtml/testfiles/resources/pdf/dummy.pdf" "build\demo.pdf" 4

echo.
echo Done. To test resume: run the same download command again.
pause
