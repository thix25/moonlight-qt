@echo off
setlocal EnableExtensions

echo ============================================
echo  Moonlight Passthrough - VHCI Uninstall
echo ============================================
echo.

REM Check admin privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click it and choose "Run as administrator".
    echo.
    pause
    exit /b 1
)

set "PS=powershell -NoProfile -ExecutionPolicy Bypass -Command"

echo Step 1: Removing usbip / VHCI device instances...
%PS% ^
"$ErrorActionPreference='SilentlyContinue';" ^
"$devs = Get-PnpDevice | Where-Object { ($_.FriendlyName -match 'usbip|USB/IP|VHCI|VHCI_ude') -or ($_.InstanceId -match 'usbip|vhci|VHCI_ude') }; " ^
"if(-not $devs){ Write-Host 'No matching PnP devices found.'; exit 0 };" ^
"$devs | ForEach-Object { Write-Host ('Removing device: ' + ($_.FriendlyName) + ' [' + $_.InstanceId + ']'); & pnputil /remove-device $_.InstanceId | Out-Host }"

echo.
echo Step 2: Removing installed usbip driver packages...
%PS% ^
"$ErrorActionPreference='SilentlyContinue';" ^
"$drivers = Get-WindowsDriver -Online | Where-Object { $_.OriginalFileName -match 'usbip_vhci(_ude)?\.inf|usbip_root\.inf|usbip.*\.inf' -or $_.ProviderName -match 'usbip' }; " ^
"if(-not $drivers){ Write-Host 'No matching driver packages found.'; exit 0 };" ^
"$drivers | Sort-Object PublishedName -Unique | ForEach-Object { Write-Host ('Deleting driver package: ' + $_.PublishedName + ' (' + $_.OriginalFileName + ')'); & pnputil /delete-driver $_.PublishedName /uninstall /force | Out-Host }"

echo.
echo Step 3: Removing usbip certificates...
%PS% ^
"$ErrorActionPreference='SilentlyContinue';" ^
"$stores = @('Cert:\LocalMachine\My','Cert:\LocalMachine\Root','Cert:\LocalMachine\TrustedPeople','Cert:\CurrentUser\My','Cert:\CurrentUser\Root','Cert:\CurrentUser\TrustedPeople');" ^
"$found = $false;" ^
"foreach($s in $stores){" ^
"  if(Test-Path $s){" ^
"    Get-ChildItem $s | Where-Object { $_.Subject -match 'usbip' -or $_.Issuer -match 'usbip' -or $_.FriendlyName -match 'usbip' } | ForEach-Object {" ^
"      $found = $true;" ^
"      Write-Host ('Deleting certificate: ' + $_.Thumbprint + ' from ' + $s);" ^
"      Remove-Item $_.PSPath -Force" ^
"    }" ^
"  }" ^
"}" ^
"if(-not $found){ Write-Host 'No matching usbip certificates found.' }"

echo.
set /p DISABLE_TS="Step 4: Disable Windows test signing mode? (y/n): "
if /i "%DISABLE_TS%"=="y" (
    bcdedit /set testsigning off
    if %errorLevel% equ 0 (
        echo Test signing disabled. A reboot is required.
    ) else (
        echo WARNING: Failed to disable test signing.
    )
) else (
    echo Leaving test signing unchanged.
)

echo.
echo ============================================
echo  Uninstall complete
echo ============================================
echo.
echo Recommended:
echo   1. Reboot your PC
echo   2. Open Device Manager and enable "Show hidden devices"
echo   3. Check for any leftover "usbip", "VHCI", or "USB/IP" entries
echo.
pause