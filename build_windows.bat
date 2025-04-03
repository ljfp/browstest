@echo off
echo Building SOCKS server for Windows...

REM Check if cl.exe is in the path
where cl >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Visual C++ compiler not found in PATH.
    echo Please run this from a Visual Studio Developer Command Prompt
    echo or run vcvarsall.bat to set up the environment.
    exit /b 1
)

echo.
echo Compiling main.c...
echo.

REM Compile the SOCKS server with _CRT_SECURE_NO_WARNINGS to suppress sprintf warnings
cl /W4 /MT /EHsc /D_CRT_SECURE_NO_WARNINGS /Fe:socks_server.exe main.c /link ws2_32.lib mswsock.lib

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Build failed. 
    echo Please check the error messages above.
    exit /b 1
)

echo.
echo Build successful! The SOCKS server is available as socks_server.exe
echo.
echo To run the server: socks_server.exe
exit /b 0 