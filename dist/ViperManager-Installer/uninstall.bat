@echo off
setlocal EnableDelayedExpansion

title Viper Biometric Key - Uninstaller

:: Check for Administrative privileges
>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"
if '%errorlevel%' NEQ '0' (
    echo [Setup] Requesting Administrator privileges to remove Viper Key...
    goto UACPrompt
) else ( goto gotAdmin )

:UACPrompt
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin_uninstaller.vbs"
    echo UAC.ShellExecute "%~s0", "", "", "runas", 1 >> "%temp%\getadmin_uninstaller.vbs"
    "%temp%\getadmin_uninstaller.vbs"
    del "%temp%\getadmin_uninstaller.vbs"
    exit /B

:gotAdmin
    echo =========================================================
    echo        VIPER BIOMETRIC KEY - UNINSTALLER
    echo =========================================================
    echo.

    set "INSTALL_DIR=%ProgramFiles%\ViperKey"

    echo [1/3] Stopping and removing VipersKeyService...
    net stop VipersKeyService >nul 2>&1
    sc delete VipersKeyService >nul 2>&1

    echo [2/3] Removing shortcuts...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Remove-Item -Force ([Environment]::GetFolderPath('Desktop') + '\Viper Key Manager.lnk') -ErrorAction SilentlyContinue"
    set "START_MENU=%ProgramData%\Microsoft\Windows\Start Menu\Programs\Viper Key"
    if exist "!START_MENU!" (
        rd /s /q "!START_MENU!" >nul 2>&1
    )

    echo [3/3] Removing installed files...
    taskkill /F /IM ViperManager.exe >nul 2>&1
    if exist "%INSTALL_DIR%" (
        timeout /t 1 >nul
        rd /s /q "%INSTALL_DIR%" >nul 2>&1
    )

    echo.
    echo =========================================================
    echo   UNINSTALLATION COMPLETE!
    echo   All Viper Key files and services have been removed.
    echo =========================================================
    echo.
    pause
    exit /B 0
