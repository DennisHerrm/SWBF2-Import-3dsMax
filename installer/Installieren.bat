@echo off
REM ===========================================================================
REM  SWBF2 Import fuer 3ds Max - Installation ohne Setup.exe
REM
REM  Liegt im ZIP neben dem Ordner SWBF2Import\ und kopiert ihn dorthin, wo
REM  3ds Max 2016-2027 Plugin-Pakete sucht (Autodesk "Packaging Plug-ins"):
REM    %ProgramData%\Autodesk\ApplicationPlugins\SWBF2Import   alle Benutzer
REM    %AppData%\Autodesk\ApplicationPlugins\SWBF2Import       nur du
REM  Fuer ProgramData fragt Windows einmal nach Adminrechten. Wer ablehnt,
REM  bekommt die Installation nur fuer sich.
REM ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "QUELLE=%~dp0SWBF2Import"
set "ZIEL_ALLE=%ProgramData%\Autodesk\ApplicationPlugins\SWBF2Import"
set "ZIEL_ICH=%APPDATA%\Autodesk\ApplicationPlugins\SWBF2Import"

echo.
echo  SWBF2 Import fuer 3ds Max - Installation
echo  =========================================
echo.

if not exist "%QUELLE%\PackageContents.xml" (
  echo  FEHLER: Der Ordner SWBF2Import fehlt neben dieser Datei.
  echo  Bitte das ZIP zuerst komplett entpacken, dann erneut starten.
  goto ende
)

REM ---- 3ds Max muss zu sein (sonst ist die alte .dlu gesperrt) -------------
:max_pruefen
tasklist /FI "IMAGENAME eq 3dsmax.exe" /NH 2>nul | find /I "3dsmax.exe" >nul
if not errorlevel 1 (
  echo  3ds Max laeuft noch. Bitte alle 3ds-Max-Fenster schliessen
  echo  und dann eine Taste druecken ...
  pause >nul
  goto max_pruefen
)

REM ---- Wohin? ProgramData, wenn schreibbar - sonst Adminrechte anfragen ----
set "ZIEL="
if /i "%~1"=="/nurich" goto nur_ich
mkdir "%ProgramData%\Autodesk\ApplicationPlugins" >nul 2>&1
set "PROBE=%ProgramData%\Autodesk\ApplicationPlugins\.swbf2probe"
2>nul (>>"%PROBE%" (call )) && set "ZIEL=%ZIEL_ALLE%"
del "%PROBE%" >nul 2>&1
if defined ZIEL goto kopieren
if /i "%~1"=="/erhoeht" goto nur_ich

echo  Fuer die Installation fuer alle Benutzer fragt Windows gleich
echo  nach Adminrechten. Lehnst du ab, installiere ich nur fuer dich.
powershell -NoProfile -Command "try { Start-Process -FilePath '%~f0' -ArgumentList '/erhoeht' -Verb RunAs -Wait -ErrorAction Stop; exit 0 } catch { exit 1 }" >nul 2>&1
if not errorlevel 1 (
  echo  Die Installation lief im Admin-Fenster.
  goto ende
)

:nur_ich
set "ZIEL=%ZIEL_ICH%"
echo  Installation nur fuer dich (ohne Adminrechte).

:kopieren
echo  Ziel: %ZIEL%
if exist "%ZIEL%\Contents" rmdir /s /q "%ZIEL%\Contents"
REM  robocopy statt xcopy: xcopy liest die Standardeingabe und bricht ab, wenn
REM  sie umgeleitet ist (gemessen: nur ein Teil der Dateien kam an).
REM  robocopy meldet Erfolg mit 0-7, Fehler ab 8.
robocopy "%QUELLE%" "%ZIEL%" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
  echo  FEHLER beim Kopieren. Ist 3ds Max wirklich geschlossen?
  goto ende
)
if not exist "%ZIEL%\PackageContents.xml" (
  echo  FEHLER: PackageContents.xml ist nicht angekommen.
  goto ende
)
REM  Eine alte Installation nur fuer diesen Benutzer wuerde doppelt laden.
if /i "%ZIEL%"=="%ZIEL_ALLE%" if exist "%ZIEL_ICH%\PackageContents.xml" rmdir /s /q "%ZIEL_ICH%" >nul 2>&1

set ANZAHL=0
for /R "%ZIEL%\Contents" %%f in (SWBF2Import.dl*) do if /i "%%~xf"==".dlu" set /a ANZAHL+=1
echo  OK: %ANZAHL% Max-Fassungen installiert.

REM ---- Visual-C++-Laufzeit (v14.50 oder neuer) vorhanden? ------------------
set "LZ="
for /f "tokens=3" %%v in ('reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" /v Minor 2^>nul ^| find "Minor"') do set /a LZ=%%v
if not defined LZ set LZ=0
if !LZ! LSS 50 (
  echo.
  echo  HINWEIS: Die Visual C++-Laufzeit von Microsoft fehlt oder ist zu alt.
  echo  Ohne sie laedt 3ds Max das Plugin nicht. Einmal installieren:
  echo    https://aka.ms/vc14/vc_redist.x64.exe
)

echo.
echo  Fertig. 3ds Max neu starten - danach gibt es das Menue "EAfront Tool"
echo  und unter Datei - Importieren "Star Wars Battlefront II".

:ende
echo.
pause
