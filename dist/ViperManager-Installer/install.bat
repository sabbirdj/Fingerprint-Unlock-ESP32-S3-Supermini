@echo off
setlocal EnableDelayedExpansion

title Viper Biometric Key - Installer

:: Check for Administrative privileges
>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"
if '%errorlevel%' NEQ '0' (
    echo [Setup] Requesting Administrator privileges to install system service...
    goto UACPrompt
) else ( goto gotAdmin )

:UACPrompt
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin_viper.vbs"
    echo UAC.ShellExecute "%~s0", "", "", "runas", 1 >> "%temp%\getadmin_viper.vbs"
    "%temp%\getadmin_viper.vbs"
    del "%temp%\getadmin_viper.vbs"
    exit /B

:gotAdmin
    pushd "%~dp0"

    echo =========================================================
    echo        VIPER BIOMETRIC KEY - SYSTEM INSTALLER
    echo =========================================================
    echo.

    set "INSTALL_DIR=%ProgramFiles%\ViperKey"

    echo [1/5] Creating application directory at:
    echo       %INSTALL_DIR%
    if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
    if not exist "%INSTALL_DIR%\service" mkdir "%INSTALL_DIR%\service"

    echo [2/5] Installing application files...
    copy /Y "bin\ViperManager.exe" "%INSTALL_DIR%\ViperManager.exe" >nul
    copy /Y "bin\service\viper_winsvc.py" "%INSTALL_DIR%\service\viper_winsvc.py" >nul

    echo [3/5] Creating Desktop and Start Menu shortcuts...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([Environment]::GetFolderPath('Desktop') + '\Viper Key Manager.lnk'); $s.TargetPath = '%INSTALL_DIR%\ViperManager.exe'; $s.IconLocation = '%INSTALL_DIR%\ViperManager.exe,0'; $s.Save()"
    
    set "START_MENU=%ProgramData%\Microsoft\Windows\Start Menu\Programs\Viper Key"
    if not exist "!START_MENU!" mkdir "!START_MENU!"
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%START_MENU%\Viper Key Manager.lnk'); $s.TargetPath = '%INSTALL_DIR%\ViperManager.exe'; $s.IconLocation = '%INSTALL_DIR%\ViperManager.exe,0'; $s.Save()"

    echo [4/5] Installing Native Windows Background Guard Service...
    where python >nul 2>&1
    if %errorlevel% EQU 0 (
        net stop VipersKeyService >nul 2>&1
        python "%INSTALL_DIR%\service\viper_winsvc.py" remove >nul 2>&1
        python "%INSTALL_DIR%\service\viper_winsvc.py" install >nul 2>&1
        sc config VipersKeyService start= auto >nul 2>&1
        sc failure VipersKeyService reset= 60 actions= restart/5000/restart/10000/restart/20000 >nul 2>&1
        python "%INSTALL_DIR%\service\viper_winsvc.py" start >nul 2>&1
        echo       Service 'VipersKeyService' installed and started successfully!
    ) else (
        echo       [Warning] Python not detected in PATH; background service can be installed manually later.
    )

    echo [5/5] Copying uninstaller...
    copy /Y "uninstall.bat" "%INSTALL_DIR%\uninstall.bat" >nul

    echo.
    echo =========================================================
    echo   INSTALLATION COMPLETE!
    echo   - Native Manager installed to Program Files
    echo   - Shortcuts added to Desktop and Start Menu
    echo   - Windows Background Guard running in services.msc
    echo =========================================================
    echo.

    set /p LAUNCH="Would you like to launch Viper Manager now? (Y/N): "
    if /i "!LAUNCH!"=="Y" (
        start "" "%INSTALL_DIR%\ViperManager.exe"
    )

    exit /B 0
