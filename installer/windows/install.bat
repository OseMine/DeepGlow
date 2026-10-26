@echo off
REM DeepGlow 2 - Windows Installer (OFX Bundle)
REM Installs to: C:\Program Files\Common Files\OFX\Plugins\

setlocal

NET SESSION >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo This installer requires Administrator privileges.
    echo Right-click and select "Run as Administrator".
    pause
    exit /b 1
)

set "SOURCE=%~dp0..\..\bundle\DeepGlow2.ofx.bundle"
if not exist "%SOURCE%" (
    set "SOURCE=%~dp0DeepGlow2.ofx.bundle"
)

set "DEST=C:\Program Files\Common Files\OFX\Plugins\DeepGlow2.ofx.bundle"

if not exist "%SOURCE%" (
    echo Error: DeepGlow2.ofx.bundle not found.
    echo Expected at: %SOURCE%
    pause
    exit /b 1
)

echo Installing DeepGlow 2 to: %DEST%
if exist "%DEST%" (
    echo Removing existing installation...
    rmdir /s /q "%DEST%"
)

xcopy /E /I /Y "%SOURCE%" "%DEST%" >nul
if %ERRORLEVEL% NEQ 0 (
    echo Installation failed.
    pause
    exit /b 1
)

echo Installation complete!
echo.
echo You may need to restart DaVinci Resolve for the plugin to appear.
pause
exit /b 0
