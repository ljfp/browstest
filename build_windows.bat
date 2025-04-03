@echo off
echo Building SOCKS server for Windows...

REM Check if cl.exe is in the path
where cl >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Visual C++ compiler not found in PATH.
    echo Please run this from a Visual Studio Developer Command Prompt
    echo or run vcvarsall.bat to set up the environment.
    exit /b 1
)

REM Compile the SOCKS server
cl /W4 /MT /EHsc main.c /link ws2_32.lib /Fe:socks_server.exe

if %ERRORLEVEL% NEQ 0 (
    echo Build failed.
    exit /b 1
)

echo.
echo Build successful! The SOCKS server is available as socks_server.exe
echo.
echo To run the server: socks_server.exe
exit /b 0 