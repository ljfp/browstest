@echo off
echo Testing SOCKS server...

REM Check if the SOCKS server executable exists
if not exist socks_server.exe (
    echo Error: socks_server.exe not found.
    echo Please build the SOCKS server first using build_windows.bat
    exit /b 1
)

REM Test the SOCKS connection using curl if available
where curl >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo curl.exe not found in PATH.
    echo Manual testing instructions:
    echo 1. Start socks_server.exe
    echo 2. Configure your browser to use SOCKS5 proxy at 127.0.0.1:1080
    echo 3. Try to browse to a website
    exit /b 0
)

echo.
echo Starting SOCKS server...
start /b socks_server.exe

REM Wait a bit for the SOCKS server to start
timeout /t 3 /nobreak > nul

echo.
echo Attempting to connect through the SOCKS proxy...
curl --socks5 127.0.0.1:1080 -o nul -s -w "Connection test: %%{http_code}\n" http://example.com

echo.
echo If you received "Connection test: 200", the SOCKS server is working!
echo Otherwise, check the SOCKS server console for error messages.
echo.
echo Press any key to stop the SOCKS server...
pause > nul

REM Find and terminate the SOCKS server process
taskkill /f /im socks_server.exe >nul 2>&1

exit /b 0 