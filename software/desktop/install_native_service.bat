@echo off
:: BatchGotAdmin
:-------------------------------------
REM  --> Check for permissions
>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"

REM --> If error flag set, we do not have admin.
if '%errorlevel%' NEQ '0' (
    echo Requesting administrative privileges...
    goto UACPrompt
) else ( goto gotAdmin )

:UACPrompt
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin.vbs"
    echo UAC.ShellExecute "%~s0", "", "", "runas", 1 >> "%temp%\getadmin.vbs"
    "%temp%\getadmin.vbs"
    del "%temp%\getadmin.vbs"
    exit /B

:gotAdmin
    pushd "%~dp0"
    
    echo Installing Viper Native Windows Service...
    
    :: Install the service using Python
    python viper_winsvc.py install
    
    :: Configure the service to start automatically on boot
    sc config VipersKeyService start= auto
    
    :: Start the service
    python viper_winsvc.py start
    
    echo.
    echo Installation complete! The Viper service is now a native Windows Service.
    echo You can manage it using 'services.msc'.
    pause
