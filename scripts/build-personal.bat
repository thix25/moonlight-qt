@echo off
setlocal EnableDelayedExpansion

rem ------------------------------------------------------------
rem  Resolve SOURCE_ROOT immediately, before any call can alter
rem  the working directory or %~dp0 context.
rem ------------------------------------------------------------
pushd "%~dp0.."
set "SOURCE_ROOT=%CD%"
popd

rem ============================================================
rem  Personal Build Script for Moonlight QT (Windows)
rem  Can be run from ANY terminal (SSH/cmd/PowerShell).
rem
rem  Usage:
rem    scripts\build-personal.bat [options] [version]
rem
rem  Options:
rem    --fast [N]   Use jom for parallel compilation.
rem                  N = number of jobs (default: 32 for Ryzen 9 7950X).
rem                  Requires jom.exe in scripts\ folder.
rem                  Also enables MSVC /MP multi-process compilation.
rem
rem  Examples:
rem    scripts\build-personal.bat                  (nmake, current version)
rem    scripts\build-personal.bat 6.3.0            (nmake, version 6.3.0)
rem    scripts\build-personal.bat --fast            (jom -j32, current ver)
rem    scripts\build-personal.bat --fast 16         (jom -j16, current ver)
rem    scripts\build-personal.bat --fast 16 6.3.0   (jom -j16, ver 6.3.0)
rem ============================================================

rem ------------------------------------------------------------
rem  Parse arguments: [--fast [N]] [version]
rem ------------------------------------------------------------
set "USE_JOM="
set "JOM_JOBS=32"
set "VERSION="
set "MP_FLAG="

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--fast" (
    set "USE_JOM=1"
    set "MP_FLAG=/MP"
    shift
    goto parse_check_jobs
)
rem Anything else is the version
if "!VERSION!"=="" set "VERSION=%~1"
shift
goto parse_args

:parse_check_jobs
rem After --fast, check if next arg is a number (job count)
rem Must be outside the if() block so shift takes effect on %~1
echo %~1| findstr /r "^[0-9][0-9]*$" >nul 2>&1
if !ERRORLEVEL! EQU 0 (
    set "JOM_JOBS=%~1"
    shift
)
goto parse_args

:args_done

if defined USE_JOM (
    echo.
    echo  *** FAST BUILD MODE: jom -j%JOM_JOBS% + /MP ***
    echo.
) else (
    echo.
    echo  Standard build mode ^(nmake^). Use --fast for parallel builds.
    echo.
)

rem ------------------------------------------------------------
rem  Step 0: Configure paths to VS and Qt environments
rem ------------------------------------------------------------
set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
set "QT_ENV_BAT=C:\ProgramData\Qt\6.10.2\msvc2022_64\bin\qtenv2.bat"

echo.
echo ============================================================
echo  Step 0: Initialize Visual Studio and Qt environment
echo ============================================================

rem --- Visual Studio environment ---
if not exist "%VS_DEV_CMD%" (
    echo ERROR: VS_DEV_CMD not found:
    echo        "%VS_DEV_CMD%"
    echo Please update the path in build-personal.bat.
    exit /b 1
)

echo Initializing Visual Studio environment...
call "%VS_DEV_CMD%" -arch=x64
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: Failed to initialize Visual Studio environment
    exit /b 1
)
echo OK - Visual Studio environment initialized

rem --- Qt environment ---
if not exist "%QT_ENV_BAT%" (
    echo ERROR: QT_ENV_BAT not found:
    echo        "%QT_ENV_BAT%"
    echo Please update the path in build-personal.bat.
    exit /b 1
)

echo Initializing Qt environment...
call "%QT_ENV_BAT%"
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: Failed to initialize Qt environment
    exit /b 1
)
echo OK - Qt environment initialized

echo.
echo ============================================================
echo  Step 1: Navigate to source root
echo ============================================================
cd /d "%SOURCE_ROOT%"
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: Failed to navigate to %SOURCE_ROOT%
    exit /b 1
)
echo OK - Working directory: %CD%

echo.
echo ============================================================
echo  Step 2: Set version (if provided)
echo ============================================================
if "%VERSION%"=="" (
    echo No version parameter provided - keeping current version.
    if exist "%SOURCE_ROOT%\app\version.txt" (
        for /f "usebackq delims=" %%v in ("%SOURCE_ROOT%\app\version.txt") do set "CURRENT_VERSION=%%v"
        echo Current version: !CURRENT_VERSION!
    ) else (
        echo WARNING: app\version.txt does not exist yet.
    )
) else (
    echo Setting version to: %VERSION%
    > "%SOURCE_ROOT%\app\version.txt" echo %VERSION%
    if !ERRORLEVEL! NEQ 0 (
        echo ERROR: Failed to write version.txt
        exit /b 1
    )
    echo OK - version.txt updated to %VERSION%
)

echo.
echo ============================================================
echo  Step 3: Update git submodules
echo ============================================================
git submodule update --init --recursive
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: git submodule update failed
    exit /b 1
)
echo OK - Submodules updated

echo.
echo ============================================================
echo  Step 4: Run qmake
echo ============================================================
if defined USE_JOM (
    echo Running qmake with /MP flag for multi-process compilation...
    qmake moonlight-qt.pro "QMAKE_CXXFLAGS+=/MP" "QMAKE_CFLAGS+=/MP"
) else (
    qmake moonlight-qt.pro
)
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: qmake failed
    exit /b 1
)
echo OK - qmake completed

echo.
echo ============================================================
echo  Step 5: Build release
echo ============================================================
if defined USE_JOM (
    if exist "%SOURCE_ROOT%\scripts\jom.exe" (
        echo Building with jom -j%JOM_JOBS% + /MP ...
        "%SOURCE_ROOT%\scripts\jom.exe" -j %JOM_JOBS% release
    ) else (
        echo WARNING: jom.exe not found in scripts\ - falling back to nmake
        nmake release
    )
) else (
    nmake release
)
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: build failed
    exit /b 1
)
echo OK - build completed

echo.
echo ============================================================
echo  Step 6: Clean QML cache files
echo ============================================================
cd /d "%SOURCE_ROOT%\app"
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: Failed to navigate to app directory
    exit /b 1
)

echo Cleaning QML cache files...
if exist "qml_qmlcache.qrc"                              del /Q "qml_qmlcache.qrc"
if exist "release\qmlcache_loader.cpp"                   del /Q "release\qmlcache_loader.cpp"
if exist "release\qrc_qml_qmlcache.cpp"                  del /Q "release\qrc_qml_qmlcache.cpp"
if exist "release\gui_ClientSettingsDialog_qml.cpp"      del /Q "release\gui_ClientSettingsDialog_qml.cpp"

for %%f in (release\gui_*.cpp) do (
    echo   Deleting %%f
    del /Q "%%f"
)

cd /d "%SOURCE_ROOT%"
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: Failed to navigate back to source root
    exit /b 1
)
echo OK - QML cache cleaned

echo.
echo ============================================================
echo  Step 7: Run build-arch.bat release
echo ============================================================
call scripts\build-arch.bat release
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: build-arch.bat failed
    exit /b 1
)
echo OK - build-arch.bat completed

echo.
echo ============================================================
echo  BUILD SUCCESSFUL
echo ============================================================
echo.

endlocal
exit /b 0