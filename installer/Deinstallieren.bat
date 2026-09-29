@echo off
REM ===========================================================================
REM  SWBF2 Import fuer 3ds Max - Deinstallation (Gegenstueck zu Installieren.bat)
REM  Entfernt das Paket aus beiden Orten. Wer mit der Setup.exe installiert hat,
REM  deinstalliert besser ueber Windows-Einstellungen - Apps.
REM ===========================================================================
setlocal
set "ZIEL_ALLE=%ProgramData%\Autodesk\ApplicationPlugins\SWBF2Import"
set "ZIEL_ICH=%APPDATA%\Autodesk\ApplicationPlugins\SWBF2Import"

echo.
echo  SWBF2 Import fuer 3ds Max - Deinstallation
echo  ===========================================
echo.

:max_pruefen
tasklist /FI "IMAGENAME eq 3dsmax.exe" /NH 2>nul | find /I "3dsmax.exe" >nul
if not errorlevel 1 (
  echo  3ds Max laeuft noch. Bitte schliessen und eine Taste druecken ...
  pause >nul
  goto max_pruefen
)

if exist "%ZIEL_ICH%" (
  rmdir /s /q "%ZIEL_ICH%"
  echo  Entfernt: %ZIEL_ICH%
)

if not exist "%ZIEL_ALLE%" goto fertig
rmdir /s /q "%ZIEL_ALLE%" >nul 2>&1
if not exist "%ZIEL_ALLE%" (
  echo  Entfernt: %ZIEL_ALLE%
  goto fertig
)
if /i "%~1"=="/erhoeht" (
  echo  FEHLER: %ZIEL_ALLE% liess sich nicht entfernen.
  goto fertig
)
echo  Fuer %ZIEL_ALLE% fragt Windows nach Adminrechten ...
powershell -NoProfile -Command "try { Start-Process -FilePath '%~f0' -ArgumentList '/erhoeht' -Verb RunAs -Wait -ErrorAction Stop } catch { }" >nul 2>&1

:fertig
echo.
echo  Fertig.
echo.
pause
