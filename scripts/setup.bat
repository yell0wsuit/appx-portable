@echo off
rem Stages an extracted Store package on Windows; runs setup.ps1, which needs no
rem execution policy change this way.
rem   setup.bat -SourceDirectory <folder> -Destination <folder> [-RuntimeDirectory <.appx or folder>] [-BuildDirectory <folder>]
rem Without arguments (double-clicked), asks for the folders and waits before closing.
setlocal
if not "%~1"=="" goto run
echo Paste a path, or drag a file or folder onto this window, then press Enter.
set "APPX_SOURCE="
set "APPX_DESTINATION="
set "APPX_RUNTIME="
set "APPX_BUILD="
set /p "APPX_SOURCE=Extracted package folder: "
set /p "APPX_DESTINATION=Folder to stage the game in: "
set /p "APPX_RUNTIME=VCLibs .appx or folder (Enter if the DLLs are beside the game): "
set /p "APPX_BUILD=Folder with appxportable.dll (Enter for build\windows-x86): "
if not defined APPX_SOURCE goto missing
if not defined APPX_DESTINATION goto missing
rem Explorer's "Copy as path" and dragging add quotes; setup.ps1 is passed its own.
set "APPX_SOURCE=%APPX_SOURCE:"=%"
set "APPX_DESTINATION=%APPX_DESTINATION:"=%"
set APPX_OPTIONS=
if defined APPX_RUNTIME set APPX_OPTIONS=-RuntimeDirectory "%APPX_RUNTIME:"=%"
if defined APPX_BUILD set APPX_OPTIONS=%APPX_OPTIONS% -BuildDirectory "%APPX_BUILD:"=%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" -SourceDirectory "%APPX_SOURCE%" -Destination "%APPX_DESTINATION%" %APPX_OPTIONS%
pause
exit /b
:missing
echo Both the package folder and the staging folder are needed.
pause
exit /b 1
:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
