@echo off
echo Installing Viper Background Service to Windows Startup...

set "STARTUP_FOLDER=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "SCRIPT_PATH=%~dp0viper_service.pyw"

echo Set oWS = WScript.CreateObject("WScript.Shell") > CreateShortcut.vbs
echo sLinkFile = "%STARTUP_FOLDER%\ViperService.lnk" >> CreateShortcut.vbs
echo Set oLink = oWS.CreateShortcut(sLinkFile) >> CreateShortcut.vbs
echo oLink.TargetPath = "%SCRIPT_PATH%" >> CreateShortcut.vbs
echo oLink.WorkingDirectory = "%~dp0" >> CreateShortcut.vbs
echo oLink.Description = "Viper Auto-Arm Service" >> CreateShortcut.vbs
echo oLink.Save >> CreateShortcut.vbs

cscript /nologo CreateShortcut.vbs
del CreateShortcut.vbs

echo.
echo Success! The Viper background service will now start automatically when you log into Windows.
echo You can test it immediately by double-clicking 'viper_service.pyw'.
pause
