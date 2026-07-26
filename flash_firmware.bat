@echo off
setlocal
set "FIRMWARE=%~1"
if not defined FIRMWARE set "FIRMWARE=%~dp0build\paste_dispenser.uf2"
if not exist "%FIRMWARE%" (
  echo ERROR: firmware not found: %FIRMWARE%
  echo Usage: %~nx0 [path-to-paste_dispenser.uf2]
  exit /b 2
)

echo Looking for a connected Raspberry Pi Pico...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$p=Get-CimInstance Win32_SerialPort | Where-Object {$_.PNPDeviceID -like 'USB\VID_2E8A*'} | Select-Object -First 1; if($p){cmd /c ('mode '+$p.DeviceID+': BAUD=1200 PARITY=n DATA=8 STOP=1') | Out-Null}; $drive=$null; $deadline=(Get-Date).AddSeconds(12); do {Start-Sleep -Milliseconds 500; $drive=Get-Volume | Where-Object {$_.FileSystemLabel -in @('RP2350','RPI-RP2')} | Select-Object -First 1} while(-not $drive -and (Get-Date)-lt $deadline); if(-not $drive){Write-Error 'BOOTSEL volume not found'; exit 3}; Copy-Item -LiteralPath '%FIRMWARE%' -Destination ($drive.DriveLetter+':\paste_dispenser.uf2') -Force; Write-Host ('Firmware copied to '+$drive.DriveLetter+':')"
if errorlevel 1 exit /b %errorlevel%
echo Flash complete. The controller will reboot automatically.
endlocal
