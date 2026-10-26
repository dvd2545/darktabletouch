@echo off
setlocal
title DarktableTouch - test build

if not exist "%~dp0build\bin\darktabletouch.exe" goto missing_build
if not exist "%~dp0build\bin\darktable-touch.exe" goto missing_build

if defined DT_MSYS2_ROOT goto check_runtime
set "DT_MSYS2_ROOT=%LOCALAPPDATA%\darktabletouch-build\msys64"
if exist "%DT_MSYS2_ROOT%\ucrt64\bin\Qt6Core.dll" goto check_runtime
set "DT_MSYS2_ROOT=C:\msys64"

:check_runtime
if not exist "%DT_MSYS2_ROOT%\ucrt64\bin\Qt6Core.dll" goto missing_runtime
set "PATH=%DT_MSYS2_ROOT%\ucrt64\bin;%PATH%"
set "QT_PLUGIN_PATH=%DT_MSYS2_ROOT%\ucrt64\share\qt6\plugins"
set "QML_IMPORT_PATH=%DT_MSYS2_ROOT%\ucrt64\share\qt6\qml"

echo Starting the current Touch build...
"%~dp0build\bin\darktabletouch.exe" --ui=touch %*
set "DT_BUILD_EXIT=%ERRORLEVEL%"
if "%DT_BUILD_EXIT%"=="0" exit /b 0
echo.
echo DarktableTouch exited with code %DT_BUILD_EXIT%. See the output above.
pause
exit /b %DT_BUILD_EXIT%

:missing_build
echo The Touch build was not found in "%~dp0build\bin".
echo Build the project in MSYS2 UCRT64 with: cmake --build build -j3
pause
exit /b 1

:missing_runtime
echo The MSYS2 UCRT64 runtime was not found.
echo Set DT_MSYS2_ROOT to your MSYS2 directory and run this file again.
pause
exit /b 1
