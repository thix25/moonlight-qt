# Script to generate moc files for .cpp files that include them
# This is a workaround for qmake not properly detecting in-source moc requirements
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File generate-mocs.ps1 -SourceRoot <path> [-QtBinDir <path>]
#
# Parameters:
#   -SourceRoot  Root of the moonlight-qt repo (e.g. C:\BUILDS\Moonlight_QT\moonlight-qt)
#   -QtBinDir    Qt bin directory (default: auto-detected via 'where qmake')

param(
    [Parameter(Mandatory=$true)]
    [string]$SourceRoot,

    [Parameter(Mandatory=$false)]
    [string]$QtBinDir
)

$ErrorActionPreference = "Stop"

# Auto-detect Qt bin directory if not provided
if (-not $QtBinDir) {
    $qmakePath = (Get-Command qmake -ErrorAction SilentlyContinue).Source
    if ($qmakePath) {
        $QtBinDir = Split-Path -Parent $qmakePath
    } else {
        Write-Error "Qt bin directory not found. Pass -QtBinDir or ensure qmake is on PATH."
        exit 1
    }
}

$QtRoot = Split-Path -Parent $QtBinDir
$QtMoc = Join-Path $QtBinDir "moc.exe"
$SourceDir = Join-Path $SourceRoot "app"

$MocFiles = @(
    @{
        Source = "$SourceDir\backend\boxartmanager.cpp"
        Output = "$SourceDir\backend\boxartmanager.moc"
        Includes = @("-I$SourceDir", "-I$SourceDir\backend")
    },
    @{
        Source = "$SourceDir\backend\computermanager.cpp"
        Output = "$SourceDir\backend\computermanager.moc"
        Includes = @("-I$SourceDir", "-I$SourceDir\backend")
    },
    @{
        Source = "$SourceDir\gui\computermodel.cpp"
        Output = "$SourceDir\gui\computermodel.moc"
        Includes = @("-I$SourceDir", "-I$SourceDir\gui")
    }
)

$QtInclude = Join-Path $QtRoot "include"
$CommonIncludes = @(
    "-I$QtInclude",
    "-I$QtInclude\QtCore",
    "-I$QtInclude\QtNetwork",
    "-I$QtInclude\QtGui"
)

$CommonDefines = @(
    "-DUNICODE",
    "-D_UNICODE",
    "-DWIN32",
    "-DWIN64",
    "-DQT_CORE_LIB",
    "-DQT_NETWORK_LIB",
    "-DQT_GUI_LIB"
)

Write-Host "Generating moc files..." -ForegroundColor Cyan

foreach ($mocFile in $MocFiles) {
    $args = @($mocFile.Includes) + $CommonIncludes + $CommonDefines + @($mocFile.Source, "-o", $mocFile.Output)
    
    Write-Host "  Generating $(Split-Path -Leaf $mocFile.Output)..." -ForegroundColor Gray
    & $QtMoc $args
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to generate $(Split-Path -Leaf $mocFile.Output)"
        exit 1
    }
}

Write-Host "All moc files generated successfully!" -ForegroundColor Green
