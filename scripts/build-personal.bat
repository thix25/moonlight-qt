@echo off
setlocal enableDelayedExpansion

rem ============================================================
rem  Personal Build Script for Moonlight QT (Windows)
rem  Stops on any error.
rem
rem  Usage:
rem    scripts\build-personal.bat [version]
rem
rem  Examples:
rem    scripts\build-personal.bat           (keeps current version)
rem    scripts\build-personal.bat 6.3.0     (sets version to 6.3.0)
rem
rem  Prerequisites:
rem    - Run from "x64 Native Tools Command Prompt for VS"
rem    - Qt env must be set up first, e.g.:
rem      "C:\ProgramData\Qt\6.10.1\msvc2022_64\bin\qtenv2.bat"
rem ============================================================

set SOURCE_ROOT=C:\BUILDS\To-Delete-Moonlight_QT\moonlight-qt
set VERSION=%~1

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
    for /f "usebackq delims=" %%v in ("%SOURCE_ROOT%\app\version.txt") do set CURRENT_VERSION=%%v
    echo Current version: !CURRENT_VERSION!
) else (
    echo Setting version to: %VERSION%
    echo %VERSION%> "%SOURCE_ROOT%\app\version.txt"
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
qmake moonlight-qt.pro
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: qmake failed
    exit /b 1
)
echo OK - qmake completed

echo.
echo ============================================================
echo  Step 5: Build with nmake release
echo ============================================================
nmake release
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: nmake release failed
    exit /b 1
)
echo OK - nmake release completed

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
if exist "qml_qmlcache.qrc"              del /Q "qml_qmlcache.qrc"
if exist "release\qmlcache_loader.cpp"    del /Q "release\qmlcache_loader.cpp"
if exist "release\qrc_qml_qmlcache.cpp"  del /Q "release\qrc_qml_qmlcache.cpp"
if exist "release\gui_ClientSettingsDialog_qml.cpp" del /Q "release\gui_ClientSettingsDialog_qml.cpp"

rem Delete all gui_*.cpp files in release folder
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
