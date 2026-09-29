@echo off
REM ===========================================================================
REM  SWBF2 Import for 3ds Max - uninstall (counterpart of Install.bat)
REM  Removes the package from both locations. If you installed with the
REM  Setup.exe, better uninstall via Windows Settings - Apps.
REM ===========================================================================
setlocal
set "ZIEL_ALLE=%ProgramData%\Autodesk\ApplicationPlugins\SWBF2Import"
set "ZIEL_ICH=%APPDATA%\Autodesk\ApplicationPlugins\SWBF2Import"

echo.
echo  SWBF2 Import for 3ds Max - uninstall
echo  =====================================
echo.

:max_pruefen
tasklist /FI "IMAGENAME eq 3dsmax.exe" /NH 2>nul | find /I "3dsmax.exe" >nul
if not errorlevel 1 (
  echo  3ds Max is still running. Please close it and press any key ...
  pause >nul
  goto max_pruefen
)

if exist "%ZIEL_ICH%" (
  rmdir /s /q "%ZIEL_ICH%"
  echo  Removed: %ZIEL_ICH%
)

if not exist "%ZIEL_ALLE%" goto fertig
rmdir /s /q "%ZIEL_ALLE%" >nul 2>&1
if not exist "%ZIEL_ALLE%" (
  echo  Removed: %ZIEL_ALLE%
  goto fertig
)
if /i "%~1"=="/elevated" (
  echo  ERROR: could not remove %ZIEL_ALLE%.
  goto fertig
)
echo  Windows will ask for admin rights to remove %ZIEL_ALLE% ...
powershell -NoProfile -Command "try { Start-Process -FilePath '%~f0' -ArgumentList '/elevated' -Verb RunAs -Wait -ErrorAction Stop } catch { }" >nul 2>&1

:fertig
echo.
echo  Done.
echo.
pause
