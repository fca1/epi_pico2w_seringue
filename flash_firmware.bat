@echo off
setlocal
set "FIRMWARE=%~1"
if not defined FIRMWARE set "FIRMWARE=%~dp0build\paste_dispenser.uf2"
python "%~dp0example\flash_firmware.py" "%FIRMWARE%"
if errorlevel 1 (
  echo ERROR: automatic firmware update failed. Close every serial terminal.
  exit /b %errorlevel%
)
echo Flash complete. The controller will reboot automatically.
endlocal
