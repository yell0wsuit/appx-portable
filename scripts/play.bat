@echo off
rem Starts the staged game on Windows.
rem   play.bat [--log] [--trace] [--capture] [--diagnostics] [--passthrough]
rem The game folder is local\game; set APPX_GAME to use another one.
setlocal
if not defined APPX_GAME set "APPX_GAME=%~dp0..\local\game"
rem The executable the package manifest names (Game.exe without one).
set "APPX_EXE=Game.exe"
for /f "usebackq delims=" %%e in (`powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0game_exe.ps1" -GameDirectory "%APPX_GAME%"`) do set "APPX_EXE=%%e"
if not exist "%APPX_GAME%\%APPX_EXE%" (
    echo %APPX_EXE% not found in "%APPX_GAME%". Stage the game with scripts\setup.bat first.
    exit /b 1
)
start "" /D "%APPX_GAME%" "%APPX_GAME%\%APPX_EXE%" %*
