@echo off
REM ================================================================
REM  BUILD.bat (1.04.0) - nur das Max-Plugin bauen und installieren.
REM  Kein Index, keine Messungen, keine Gegenproben - dafuer ist
REM  START.bat da. Gebaut wird inkrementell (nur was sich geaendert
REM  hat), fuer jeden installierten Max-Jahrgang.
REM    BUILD.log   - Bau und Installation
REM  Mit einem Jahrgang als Parameter nur diesen bauen:  BUILD.bat 2027
REM ================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "LOG=%~dp0BUILD.log"
set "OUTPUT=%~dp0output"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "CMAKE="
set "GEN=Visual Studio 17 2022"
set "GEBAUT=0"
set "NUR=%~1"
echo BUILD.bat  %DATE% %TIME%> "%LOG%"
echo.
echo ================================================================
echo   SWBF2 Import - nur bauen und installieren
echo ================================================================

where cmake >nul 2>&1
if errorlevel 1 goto suche_vs_cmake
set "CMAKE=cmake"
goto habe_cmake
:suche_vs_cmake
if not exist "%VSWHERE%" goto kein_cmake
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath 2^>nul`) do call :pruefe_cmake "%%i"
if "%CMAKE%"=="" goto kein_cmake
goto habe_cmake
:pruefe_cmake
if exist "%~1\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%~1\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
goto :eof
:kein_cmake
echo KEIN CMAKE GEFUNDEN - Visual Studio mit C++ und CMake installieren.
goto ende

:habe_cmake
if not exist "%VSWHERE%" goto generator_fertig
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [18.0^,19.0^) -property installationPath 2^>nul`) do set "GEN=Visual Studio 18 2026"
:generator_fertig

echo [1/2] Plugin bauen ...
if not "%NUR%"=="" goto nur_einer
for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do call :baue_version %%V
goto nach_bau
:nur_einer
call :baue_version %NUR%
:nach_bau
if "%GEBAUT%"=="0" goto kein_sdk

echo.
echo [2/2] Installieren ...
set SWBF2_KEIN_PAUSE=1
call "%~dp0INSTALLIERE.bat"
set SWBF2_KEIN_PAUSE=
echo.
echo Fertig: %GEBAUT% Jahrgang(Jahrgaenge) gebaut und installiert.
echo Protokoll: %LOG%
goto ende

:baue_version
set "VER=%~1"
set "SDK=C:\Program Files\Autodesk\3ds Max %VER% SDK\maxsdk"
if not exist "%SDK%\include\max.h" goto :eof
echo       Max %VER% ...
if exist "build_%VER%\CMakeCache.txt" goto nur_bauen_version
"%CMAKE%" -S . -B "build_%VER%" -G "%GEN%" -A x64 -DSWBF2_BUILD_PLUGIN=ON -DSWBF2_BUILD_TOOL=OFF -D3DSMAX_SDK_DIR="%SDK%" -DMAX_VERSION=%VER% >> "%LOG%" 2>&1
if errorlevel 1 goto :baufehler_version
:nur_bauen_version
"%CMAKE%" --build "build_%VER%" --config Release >> "%LOG%" 2>&1
if errorlevel 1 goto :baufehler_version
mkdir "%OUTPUT%\%VER%" >nul 2>&1
set "DLU="
if exist "build_%VER%\bin\SWBF2Import.dlu" set "DLU=build_%VER%\bin\SWBF2Import.dlu"
if not defined DLU if exist "build_%VER%\Release\SWBF2Import.dlu" set "DLU=build_%VER%\Release\SWBF2Import.dlu"
if not defined DLU for /R "build_%VER%" %%f in (SWBF2Import.dl*) do if /i "%%~xf"==".dlu" set "DLU=%%f"
if not defined DLU goto :baufehler_version
copy /Y "%DLU%" "%OUTPUT%\%VER%\SWBF2Import.dlu" >nul 2>&1
if not exist "%OUTPUT%\%VER%\SWBF2Import.dlu" goto :baufehler_version
echo             ok -^> output\%VER%\SWBF2Import.dlu
set /a GEBAUT+=1
goto :eof

:baufehler_version
REM  Alte .dlu wegraeumen, sonst installiert sich eine Fassung, die nicht
REM  mehr zum Quelltext passt (Max meldet dann "Error code 193").
if exist "%OUTPUT%\%VER%\SWBF2Import.dlu" del /q "%OUTPUT%\%VER%\SWBF2Import.dlu"
echo             FEHLGESCHLAGEN - die Fehlerzeilen:
findstr /R /C:"error C" /C:"error LNK" /C:"error MSB" "%LOG%" 2>nul
echo             Vollstaendiges Protokoll: %LOG%
goto :eof

:kein_sdk
echo KEIN 3ds-Max-SDK GEFUNDEN unter C:\Program Files\Autodesk\3ds Max ^<Jahr^> SDK.
goto ende

:ende
echo.
if not "%SWBF2_KEIN_PAUSE%"=="1" pause
