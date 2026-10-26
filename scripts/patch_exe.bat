@echo off
rem Creates an AppContainer-free desktop copy of a game executable for Windows
rem 8/8.1; runs patch_exe.ps1, which needs no execution policy change this way.
rem   patch_exe.bat <game executable> [-IncludeRuntime]
rem Without arguments (double-clicked), asks for the executable and waits before closing.
setlocal
if not "%~1"=="" goto run
echo Paste a path, or drag the game executable onto this window, then press Enter.
set "APPX_EXE="
set /p "APPX_EXE=Game executable: "
if not defined APPX_EXE exit /b 1
set "APPX_EXE=%APPX_EXE:"=%"
set "APPX_RUNTIME="
set /p "APPX_RUNTIME=Also patch the VC runtime DLLs into a desktop folder? [y/N]: "
set APPX_OPTIONS=
if /i "%APPX_RUNTIME%"=="y" set APPX_OPTIONS=-IncludeRuntime
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_exe.ps1" -Exe "%APPX_EXE%" %APPX_OPTIONS%
pause
exit /b
:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_exe.ps1" -Exe %*
