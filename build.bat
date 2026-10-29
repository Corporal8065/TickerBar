@echo off
title TickerBar Build v0.2.0
REM ===========================================================
REM  TickerBar - one-click Windows 11 build (v0.2.0)
REM  Finds CMake via vswhere (official VS locator), PATH, the
REM  VS-bundled copy (2026/2022/2019, any edition, any drive
REM  registered with the installer), or a standalone CMake.
REM  Everything checked is written to build-log.txt.
REM  (Discovery flow copied from MeterBar's proven build.bat.)
REM ===========================================================
setlocal
cd /d "%~dp0"
set "LOG=%CD%\build-log.txt"
echo TickerBar build log v0.2.0 > "%LOG%"
echo Date: %DATE% %TIME% >> "%LOG%"
echo.
echo ============================================================
echo  TickerBar build v0.2.0 - this window stays open when done.
echo  Everything checked is written to build-log.txt.
echo ============================================================
echo.

REM ---- 1) CMake on PATH ----
set "CMAKE="
where cmake >nul 2>nul
if not errorlevel 1 set "CMAKE=cmake"
if defined CMAKE echo PATH cmake: FOUND >> "%LOG%"
if not defined CMAKE echo PATH cmake: missing >> "%LOG%"
if defined CMAKE goto :have_cmake

REM ---- 2) Standalone CMake installs ----
for %%P in ("%ProgramFiles%\CMake\bin\cmake.exe" "%ProgramFiles(x86)%\CMake\bin\cmake.exe" "C:\CMake\bin\cmake.exe") do (
  if not defined CMAKE if exist %%P set "CMAKE=%%~P"
)
if defined CMAKE echo Standalone cmake: %CMAKE% >> "%LOG%"
if defined CMAKE goto :have_cmake

REM ---- 3) vswhere: ask the VS installer where VS actually is ----
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :scan
echo vswhere found: %VSWHERE% >> "%LOG%"
"%VSWHERE%" -all -products * -property installationPath >> "%LOG%" 2>&1
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL goto :scan
echo vswhere latest VS: %VSINSTALL% >> "%LOG%"
if exist "%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if defined CMAKE echo VS-bundled cmake: %CMAKE% >> "%LOG%"
if defined CMAKE goto :have_cmake

REM ---- 4) Manual folder scan: 2026 / 2022 / 2019, all editions ----
:scan
for %%Y in (2026 2022 2019) do (
  for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined CMAKE if exist "%ProgramFiles%\Microsoft Visual Studio\%%Y\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%ProgramFiles%\Microsoft Visual Studio\%%Y\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if not defined CMAKE if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%Y\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%ProgramFiles(x86)%\Microsoft Visual Studio\%%Y\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
  )
)
if defined CMAKE echo Folder-scan cmake: %CMAKE% >> "%LOG%"
if defined CMAKE goto :have_cmake

echo [TickerBar] ERROR: No CMake found anywhere on this PC.
echo That almost always means Visual Studio with the C++ workload
echo is not installed yet, or its install has not finished.
echo RESULT: no cmake found - VS C++ workload likely missing >> "%LOG%"
goto :end

:have_cmake
echo [TickerBar] Using CMake: %CMAKE%
echo Using CMake: %CMAKE% >> "%LOG%"
echo [TickerBar] Configuring - first run downloads JUCE 8, needs internet...
"%CMAKE%" -S . -B build-app -DTICKERBAR_BUILD_APP=ON -A x64 >> "%LOG%" 2>&1
if not errorlevel 1 goto :build
echo [TickerBar] CONFIGURE FAILED.
echo Send Milo the build-log.txt file - the reason is in there.
goto :end

:build
echo [TickerBar] Building Release - this can take a few minutes...
"%CMAKE%" --build build-app --config Release -j >> "%LOG%" 2>&1
if not errorlevel 1 goto :tests
echo [TickerBar] BUILD FAILED.
echo Send Milo the build-log.txt file - the reason is in there.
goto :end

:tests
echo [TickerBar] Running core unit tests...
if exist "build-app\Release\test_core.exe" "build-app\Release\test_core.exe"
if exist "build-app\test_core.exe" "build-app\test_core.exe"
echo.
echo ============================================================
echo  DONE. Your app is at:
echo    build-app\app\TickerBar_artefacts\Release\TickerBar.exe
echo  Double-click it. The bar appears with BTC / ETH / HYPE and
echo  starts polling Hyperliquid - prices are perp marks.
echo ============================================================

:end
echo.
echo Press any key to close this window.
pause >nul
endlocal
