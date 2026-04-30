@echo off
REM Moonlight Passthrough Server - VHCI Driver Installation Script
REM Must be run as Administrator
REM Requires usbip-win driver files in the same directory

echo ============================================
echo  Moonlight Passthrough - VHCI Driver Setup
echo ============================================
echo.

REM Check admin privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click and select "Run as administrator".
    pause
    exit /b 1
)

echo Step 1: Checking for driver files...
if not exist usbip_vhci_ude.sys (
    if not exist usbip_vhci.sys (
        echo ERROR: VHCI driver files not found in current directory.
        echo.
        echo Please download usbip-win from:
        echo   https://github.com/cezanne/usbip-win/releases
        echo.
        echo Extract and copy the following files to this directory:
        echo   - usbip_vhci_ude.sys and usbip_vhci_ude.inf ^(UDE version^)
        echo   OR
        echo   - usbip_vhci.sys, usbip_vhci.inf, usbip_root.inf ^(WDM version^)
        echo   - usbip_test.pfx ^(test certificate^)
        echo.
        pause
        exit /b 1
    )
)

echo Step 2: Checking test signing status...
bcdedit /enum {current} | findstr /i "testsigning.*Yes" >nul 2>&1
if %errorLevel% neq 0 (
    echo.
    echo WARNING: Test signing is not enabled.
    echo The VHCI driver requires test signing to load.
    echo.
    set /p ENABLE_TS="Enable test signing now? (y/n): "
    if /i "%ENABLE_TS%"=="y" (
        bcdedit /set testsigning on
        if %errorLevel% equ 0 (
            echo Test signing enabled. A reboot is required.
        ) else (
            echo ERROR: Failed to enable test signing.
            echo Make sure Secure Boot is disabled in BIOS.
            pause
            exit /b 1
        )
    ) else (
        echo Skipping test signing. Driver may not load.
    )
) else (
    echo Test signing is enabled. OK.
)

echo.
echo Step 3: Installing test certificate...
if exist usbip_test.pfx (
    certutil -f -p usbip -importPFX usbip_test.pfx >nul 2>&1
    if %errorLevel% equ 0 (
        echo Certificate installed.
    ) else (
        echo WARNING: Certificate import failed. You may need to import manually.
    )
) else (
    echo WARNING: usbip_test.pfx not found. Skipping certificate install.
)

echo.
echo Step 4: Installing VHCI driver...

if exist usbip_vhci_ude.inf (
    echo Installing UDE version...
    pnputil /add-driver usbip_vhci_ude.inf /install
) else if exist usbip_vhci.inf (
    echo Installing WDM version...
    pnputil /add-driver usbip_vhci.inf /install
    if exist usbip_root.inf (
        pnputil /add-driver usbip_root.inf /install
    )
) else (
    echo ERROR: No .inf driver file found.
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Installation complete!
echo ============================================
echo.
echo If test signing was just enabled, please reboot your PC.
echo After reboot, the VHCI driver will be available for
echo the Moonlight Passthrough Server.
echo.
pause
