@echo off
REM ===========================================================================
REM  SWBF2 Import for 3ds Max - installation without Setup.exe
REM
REM  Sits in the ZIP next to the SWBF2Import\ folder and copies it to where
REM  3ds Max 2016-2027 looks for plugin packages (Autodesk "Packaging Plug-ins"):
REM    %ProgramData%\Autodesk\ApplicationPlugins\SWBF2Import   all users
REM    %AppData%\Autodesk\ApplicationPlugins\SWBF2Import       current user
REM  For ProgramData Windows asks once for admin rights. If you decline, the
REM  plugin is installed for the current user only.
REM    Install.bat /user   install for the current user without asking
REM ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "QUELLE=%~dp0SWBF2Import"
set "ZIEL_ALLE=%ProgramData%\Autodesk\ApplicationPlugins\SWBF2Import"
set "ZIEL_ICH=%APPDATA%\Autodesk\ApplicationPlugins\SWBF2Import"

echo.
echo  SWBF2 Import for 3ds Max - installation
echo  ========================================
echo.

if not exist "%QUELLE%\PackageContents.xml" (
  echo  ERROR: the SWBF2Import folder is missing next to this file.
  echo  Please extract the whole ZIP first, then run this again.
  goto ende
)

REM ---- 3ds Max must be closed (it locks the old .dlu) ----------------------
:max_pruefen
tasklist /FI "IMAGENAME eq 3dsmax.exe" /NH 2>nul | find /I "3dsmax.exe" >nul
if not errorlevel 1 (
  echo  3ds Max is still running. Please close all 3ds Max windows,
  echo  then press any key ...
  pause >nul
  goto max_pruefen
)

REM ---- Where to? ProgramData if writable - otherwise ask for admin rights --
set "ZIEL="
if /i "%~1"=="/user" goto nur_ich
mkdir "%ProgramData%\Autodesk\ApplicationPlugins" >nul 2>&1
set "PROBE=%ProgramData%\Autodesk\ApplicationPlugins\.swbf2probe"
2>nul (>>"%PROBE%" (call )) && set "ZIEL=%ZIEL_ALLE%"
del "%PROBE%" >nul 2>&1
if defined ZIEL goto kopieren
if /i "%~1"=="/elevated" goto nur_ich

echo  To install for all users, Windows will now ask for admin rights.
echo  If you decline, the plugin is installed for you only.
powershell -NoProfile -Command "try { Start-Process -FilePath '%~f0' -ArgumentList '/elevated' -Verb RunAs -Wait -ErrorAction Stop; exit 0 } catch { exit 1 }" >nul 2>&1
if not errorlevel 1 (
  echo  The installation ran in the admin window.
  goto ende
)

:nur_ich
set "ZIEL=%ZIEL_ICH%"
echo  Installing for the current user only (no admin rights).

:kopieren
echo  Target: %ZIEL%
if exist "%ZIEL%\Contents" rmdir /s /q "%ZIEL%\Contents"
REM  robocopy instead of xcopy: xcopy reads stdin and stops when it is
REM  redirected (measured: only part of the files arrived).
REM  robocopy reports success with 0-7, errors from 8.
robocopy "%QUELLE%" "%ZIEL%" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
  echo  ERROR while copying. Is 3ds Max really closed?
  goto ende
)
if not exist "%ZIEL%\PackageContents.xml" (
  echo  ERROR: PackageContents.xml did not arrive.
  goto ende
)
REM  An old per-user installation would load a second time.
if /i "%ZIEL%"=="%ZIEL_ALLE%" if exist "%ZIEL_ICH%\PackageContents.xml" rmdir /s /q "%ZIEL_ICH%" >nul 2>&1

set ANZAHL=0
for /R "%ZIEL%\Contents" %%f in (SWBF2Import.dl*) do if /i "%%~xf"==".dlu" set /a ANZAHL+=1
echo  OK: installed for %ANZAHL% versions of 3ds Max.

REM ---- Visual C++ runtime (v14.50 or newer) present? -----------------------
set "LZ="
for /f "tokens=3" %%v in ('reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" /v Minor 2^>nul ^| find "Minor"') do set /a LZ=%%v
if not defined LZ set LZ=0
if !LZ! LSS 50 (
  echo.
  echo  NOTE: the Microsoft Visual C++ runtime is missing or too old.
  echo  Without it 3ds Max cannot load the plugin. Install it once:
  echo    https://aka.ms/vc14/vc_redist.x64.exe
)

echo.
echo  Done. Restart 3ds Max - you will find the "EAfront Tool" menu and
echo  "Star Wars Battlefront II" under File - Import.

:ende
echo.
pause
