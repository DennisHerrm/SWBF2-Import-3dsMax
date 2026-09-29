@echo off
REM ================================================================
REM  BERICHT.bat (1.14.0) - bekommt jede Figur und jedes Fahrzeug
REM  Animationen? Baut nur castool und schreibt eine Zeile je Figur und je
REM  Fahrzeug: wie viele Clips, ueber welchen Weg, mit Beispielen; am Ende
REM  die Liste derer, die leer ausgehen. Ergebnis:
REM    bericht.log    - der Bericht
REM    BERICHT.log    - Bau und Aufruf
REM  Braucht index.fbidx im selben Ordner (START.bat legt ihn an).
REM ================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "LOG=%~dp0BERICHT.log"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "CMAKE="
set "GEN=Visual Studio 17 2022"
echo BERICHT.bat  %DATE% %TIME%> "%LOG%"
echo.
echo ================================================================
echo   SWBF2 Import - Bericht: Animationen je Figur und Fahrzeug
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
echo KEIN CMAKE GEFUNDEN - erst START.bat laufen lassen bzw. CMake installieren.
goto ende

:habe_cmake
if not exist "%VSWHERE%" goto generator_fertig
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [18.0^,19.0^) -property installationPath 2^>nul`) do set "GEN=Visual Studio 18 2026"
:generator_fertig

if exist "build_tool\CMakeCache.txt" goto nur_bauen
echo [1/3] build_tool einrichten (einmalig) ...
"%CMAKE%" -S . -B build_tool -G "%GEN%" -A x64 -DSWBF2_BUILD_PLUGIN=OFF -DSWBF2_BUILD_TOOL=ON >> "%LOG%" 2>&1
if errorlevel 1 goto baufehler
:nur_bauen
echo [1/3] castool bauen (nur was sich geaendert hat) ...
"%CMAKE%" --build build_tool --config Release --target castool >> "%LOG%" 2>&1
if errorlevel 1 goto baufehler

set "EXE_CASTOOL="
if exist "build_tool\bin\Release\castool.exe" set "EXE_CASTOOL=%~dp0build_tool\bin\Release\castool.exe"
if not defined EXE_CASTOOL if exist "build_tool\bin\castool.exe" set "EXE_CASTOOL=%~dp0build_tool\bin\castool.exe"
if not defined EXE_CASTOOL goto baufehler

echo [2/3] Spiel suchen ...
set "SPIEL="
if exist "%ProgramFiles%\EA Games\STAR WARS Battlefront II\Data\layout.toc" set "SPIEL=%ProgramFiles%\EA Games\STAR WARS Battlefront II"
if not defined SPIEL if exist "C:\Program Files\EA Games\STAR WARS Battlefront II\Data\layout.toc" set "SPIEL=C:\Program Files\EA Games\STAR WARS Battlefront II"
if not defined SPIEL if exist "D:\Program Files\EA Games\STAR WARS Battlefront II\Data\layout.toc" set "SPIEL=D:\Program Files\EA Games\STAR WARS Battlefront II"
if not defined SPIEL goto kein_spiel
echo       %SPIEL%

echo [3/3] Bericht erstellen (dauert einige Minuten) ...
"%EXE_CASTOOL%" --bericht "%SPIEL%" --cache "%~dp0index.fbidx" --clipcache "%LOCALAPPDATA%\SWBF2Import\clips.fbclips" --log "%~dp0bericht.log" >> "%LOG%" 2>&1
findstr /c:"mit Clips" "%~dp0bericht.log"
if not exist "%~dp0index.fbidx" echo       Hinweis: index.fbidx fehlt - der Index wird neu gebaut (dauert). START.bat legt ihn an.
if errorlevel 1 echo       castool meldete einen Fehler - siehe BERICHT.log
echo.
echo Fertig. Bitte diese Datei schicken:
echo    %~dp0bericht.log
goto ende

:baufehler
echo.
echo BAU FEHLGESCHLAGEN - die Fehlerzeilen:
findstr /c:"error " "%LOG%"
echo Vollstaendiges Protokoll: %LOG%
goto ende

:kein_spiel
echo Battlefront II nicht gefunden (Program Files\EA Games\STAR WARS Battlefront II).
goto ende

:ende
echo.
pause
