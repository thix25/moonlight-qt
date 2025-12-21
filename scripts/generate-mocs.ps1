# Script to generate moc files for .cpp files that include them
# This is a workaround for qmake not properly detecting in-source moc requirements

$ErrorActionPreference = "Stop"

$QtMoc = "C:\ProgramData\Qt\6.10.1\msvc2022_64\bin\moc.exe"
$BuildDir = "C:\BUILDS\Moonlight_QT\moonlight-qt\build\build-x64-release\app"
$SourceDir = "C:\BUILDS\Moonlight_QT\moonlight-qt\app"

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

$CommonIncludes = @(
    "-IC:\ProgramData\Qt\6.10.1\msvc2022_64\include",
    "-IC:\ProgramData\Qt\6.10.1\msvc2022_64\include\QtCore",
    "-IC:\ProgramData\Qt\6.10.1\msvc2022_64\include\QtNetwork",
    "-IC:\ProgramData\Qt\6.10.1\msvc2022_64\include\QtGui"
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
