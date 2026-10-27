@echo off
setlocal
title DarktableTouch - test build

if not exist "%~dp0build\bin\darktabletouch.exe" goto missing_build
if not exist "%~dp0build\bin\darktable-touch.exe" goto missing_build

if defined DT_MSYS2_ROOT goto check_runtime
if exist "%~dp0build\CMakeCache.txt" (
  for /f "usebackq tokens=1,* delims==" %%K in ("%~dp0build\CMakeCache.txt") do (
    if "%%K"=="CMAKE_CXX_COMPILER:FILEPATH" call :compiler_runtime "%%L"
    if "%%K"=="CMAKE_C_COMPILER:FILEPATH" call :compiler_runtime "%%L"
  )
)
if defined DT_MSYS2_ROOT goto check_runtime
call :try_runtime "%LOCALAPPDATA%\darktabletouch-build\msys64"
if defined DT_MSYS2_ROOT goto check_runtime
call :try_runtime "C:\msys64"
if defined DT_MSYS2_ROOT goto check_runtime
for /d %%D in ("%LOCALAPPDATA%\Packages\OpenAI.Codex_*") do call :try_runtime "%%~fD\LocalCache\Local\darktabletouch-build\msys64"
if not defined DT_MSYS2_ROOT goto missing_runtime

:check_runtime
call :validate_runtime "%DT_MSYS2_ROOT%"
if errorlevel 1 goto missing_runtime
set "PATH=%DT_MSYS2_ROOT%\ucrt64\bin;%PATH%"
set "QT_PLUGIN_PATH=%DT_MSYS2_ROOT%\ucrt64\share\qt6\plugins"
set "QML_IMPORT_PATH=%DT_MSYS2_ROOT%\ucrt64\share\qt6\qml"
echo Using MSYS2 UCRT64 runtime: "%DT_MSYS2_ROOT%"
if /i "%~1"=="--check-runtime" exit /b 0

echo Starting the current Touch build...
"%~dp0build\bin\darktabletouch.exe" --ui=touch %*
set "DT_BUILD_EXIT=%ERRORLEVEL%"
if "%DT_BUILD_EXIT%"=="0" exit /b 0
echo.
echo DarktableTouch exited with code %DT_BUILD_EXIT%. See the output above.
pause
exit /b %DT_BUILD_EXIT%

:compiler_runtime
if defined DT_MSYS2_ROOT exit /b 0
for %%P in ("%~1") do for %%R in ("%%~dpP..\..") do call :try_runtime "%%~fR"
exit /b

:try_runtime
if defined DT_MSYS2_ROOT exit /b 0
call :validate_runtime "%~1"
if errorlevel 1 exit /b 1
set "DT_MSYS2_ROOT=%~1"
exit /b 0

:validate_runtime
if not exist "%~1\ucrt64\bin\Qt6Core.dll" exit /b 1
if not exist "%~1\ucrt64\bin\Qt6Quick.dll" exit /b 1
if not exist "%~1\ucrt64\bin\libglib-2.0-0.dll" exit /b 1
if not exist "%~1\ucrt64\share\qt6\plugins\platforms\qwindows.dll" exit /b 1
exit /b 0

:missing_build
echo The Touch build was not found in "%~dp0build\bin".
echo Build the project in MSYS2 UCRT64 with: cmake --build build -j3
pause
exit /b 1

:missing_runtime
echo The MSYS2 UCRT64 runtime was not found.
echo Set DT_MSYS2_ROOT to your MSYS2 directory and run this file again.
if defined DT_MSYS2_ROOT echo Checked: "%DT_MSYS2_ROOT%"
pause
exit /b 1
