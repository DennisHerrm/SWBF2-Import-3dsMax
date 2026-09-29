@echo off
REM ===========================================================================
REM  INSTALLIERE.bat - das gebaute Plugin ins Paket legen und installieren.
REM
REM  ZIELORDNER, nachgeschlagen in der Autodesk-Doku "Packaging Plug-ins":
REM  3ds Max durchsucht genau zwei Orte (plus ADSK_APPLICATION_PLUGINS ab 2019):
REM    1. %ALLUSERSPROFILE%\Autodesk\ApplicationPlugins\<name>  (ProgramData)
REM       Der bevorzugte Ort, alle Benutzer, braucht Adminrechte.
REM    2. %APPDATA%\Autodesk\ApplicationPlugins\<name>
REM       Der Ausweichort ohne Adminrechte, nur fuer den angemeldeten Benutzer.
REM  Die PackageContents.xml muss DIREKT im ersten Unterordner liegen.
REM ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "OUTPUT=%~dp0output"
set "PKG=%~dp0package\SWBF2Import"
set "ZIEL_ALLE=%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins\SWBF2Import"
set "ZIEL_ICH=%APPDATA%\Autodesk\ApplicationPlugins\SWBF2Import"
set "DEST="
if /i "%~1"=="/erhoeht" set SWBF2_ERHOEHT=1
REM  Eigenes Protokoll: START.bat haengt es an START.log an. Der erhoehte Lauf
REM  schreibt in dieselbe Datei weiter (Pfad ueber %~dp0, nicht ueber eine
REM  Umgebungsvariable - die erbt ein erhoehter Prozess nicht).
set "ILOG=%~dp0INSTALLIERE.log"
if defined SWBF2_ERHOEHT (echo --- erhoehter Lauf %DATE% %TIME%>> "%ILOG%") else (echo INSTALLIERE.bat %DATE% %TIME%> "%ILOG%")

echo.
echo ================================================================
echo   SWBF2 Import - Paket bauen und installieren
echo ================================================================
echo Quelle: %OUTPUT%
echo.

if not exist "%OUTPUT%" goto kein_output
if not exist "%PKG%\PackageContents.xml" goto kein_paket

REM ---- Wo darf geschrieben werden? -----------------------------------------
mkdir "%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins" >nul 2>&1
set "PROBE=%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins\.swbf2probe"
2>nul (>>"%PROBE%" (call )) && set "DEST=%ZIEL_ALLE%"
del "%PROBE%" >nul 2>&1
if defined DEST goto ziel_steht

if defined SWBF2_ERHOEHT goto ausweichen
echo Fuer C:\ProgramData fehlen die Rechte.
echo Ich starte diese Datei einmal als Administrator neu ...
>>"%ILOG%" echo Adminrechte fuer ProgramData angefordert
REM  -Wait: START.bat soll erst weitermachen, wenn der erhoehte Lauf fertig
REM  ist - sonst haengt sie ein unvollstaendiges INSTALLIERE.log an.
powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '/erhoeht' -Verb RunAs -Wait" >nul 2>&1
if errorlevel 1 goto ausweichen
echo Der erhoehte Lauf ist beendet - sein Ergebnis steht in INSTALLIERE.log.
goto ende

:ausweichen
echo Ohne Adminrechte - ich nehme den Ausweichort (nur fuer dich):
>>"%ILOG%" echo Ohne Adminrechte - Ausweichort AppData
set "DEST=%ZIEL_ICH%"

:ziel_steht
echo Ziel:   %DEST%
>>"%ILOG%" echo Ziel: %DEST%
echo.

REM ---- Ist eine installierte .dlu gesperrt? --------------------------------
REM  Nicht pruefen, OB 3dsmax.exe laeuft - gefragt ist, ob die Datei SCHREIBBAR
REM  ist. Zwei Fallen dabei:
REM   1. "for /R <ordner> in (name.dlu)" OHNE Platzhalter prueft NICHT, ob es
REM      die Datei gibt; cmd haengt den Namen an jeden Ordner. Deshalb dl* und
REM      zusaetzlich "if not exist" im Helfer.
REM   2. ">>datei echo." haengt eine LEERZEILE an - bei einer DLL waechst die
REM      Datei bei jedem Lauf. Richtig ist ">>datei (call )": oeffnet, schreibt
REM      nichts.
set GESPERRT=
if exist "%DEST%" for /R "%DEST%" %%f in (SWBF2Import.dl*) do if /i "%%~xf"==".dlu" call :pruefe_sperre "%%f"
if defined GESPERRT goto gesperrt

REM ---- Paket zusammenstellen ----------------------------------------------
echo [1/4] Paketinhalt zusammenstellen ...
if exist "%PKG%\Contents" rmdir /s /q "%PKG%\Contents"
mkdir "%PKG%\Contents" >nul 2>&1
set GEFUNDEN=0
for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do call :packe %%V
if "%GEFUNDEN%"=="0" goto nichts_gebaut
mkdir "%PKG%\Contents\MacroScripts" >nul 2>&1
mkdir "%PKG%\Contents\Post-Start-Up_Scripts" >nul 2>&1
mkdir "%PKG%\Contents\Pre-Start-Up_Scripts" >nul 2>&1
copy /Y "%~dp0scripts\EAfrontImport.mcr" "%PKG%\Contents\MacroScripts\" >nul 2>&1
REM  Bis 2024 braucht menuMan die fertige Oberflaeche: post-start-up.
REM  Ab 2025 muss der Menue-Callback VOR dem Menueaufbau angemeldet sein:
REM  pre-start-up (Autodesk, Startup Scripts und Menu System).
copy /Y "%~dp0scripts\EAfrontMenu_2016_2024.ms" "%PKG%\Contents\Post-Start-Up_Scripts\" >nul 2>&1
copy /Y "%~dp0scripts\EAfrontMenu_2025_2027.ms" "%PKG%\Contents\Pre-Start-Up_Scripts\" >nul 2>&1
echo       %GEFUNDEN% Max-Fassung(en), Menueskripte

REM ---- Jede .dlu pruefen, BEVOR sie installiert wird ------------------------
REM  Fehlerbild, das uns das eingebrockt hat: Max meldet beim Start
REM  "Error code 193 - is not a valid Win32 application". Das heisst, die Datei
REM  ist keine gueltige 64-Bit-DLL - abgeschnitten, leer, oder uebrig aus einem
REM  Bau, der gar nicht durchlief. Geprueft wird der Dateikopf selbst: "MZ" am
REM  Anfang, "PE" an der Stelle, auf die der Versatz bei 0x3C zeigt, und
REM  dahinter die Maschinenkennung 0x8664 fuer x64.
echo [2/4] Jede .dlu auf gueltiges 64-Bit-Format pruefen ...
set KAPUTT=
for /R "%PKG%\Contents" %%f in (SWBF2Import.dl*) do if /i "%%~xf"==".dlu" call :pruefe_pe "%%f"
if defined KAPUTT goto kaputte_dlu
echo       alle in Ordnung

REM ---- Kopieren ------------------------------------------------------------
REM  Erst raeumen, dann kopieren. xcopy loescht am Ziel NICHTS, was in der
REM  Quelle fehlt - eine .dlu aus einem alten Bau bliebe sonst liegen und wuerde
REM  von Max weiter geladen.
echo [3/4] Nach ApplicationPlugins kopieren ...
if exist "%DEST%\Contents" rmdir /s /q "%DEST%\Contents"
xcopy /E /I /Y /Q "%PKG%" "%DEST%" >nul
if errorlevel 1 goto kopierfehler
echo       ok

REM ---- Alte Installation am anderen Ort wegraeumen -------------------------
if /i "%DEST%"=="%ZIEL_ALLE%" if exist "%ZIEL_ICH%\PackageContents.xml" (
  rmdir /s /q "%ZIEL_ICH%" >nul 2>&1
  echo       alte Installation unter AppData entfernt
)

echo [4/4] Nachsehen, was angekommen ist ...
set INSTALLIERT=0
for /R "%DEST%" %%f in (SWBF2Import.dl*) do if /i "%%~xf"==".dlu" set /a INSTALLIERT+=1
if not exist "%DEST%\PackageContents.xml" goto xml_fehlt
echo       %INSTALLIERT% .dlu und die PackageContents.xml liegen richtig
>>"%ILOG%" echo FERTIG: %INSTALLIERT% .dlu und PackageContents.xml in %DEST%
echo       Ort: %DEST%
echo.
echo FERTIG. In 3ds Max:
echo   Datei -^> Importieren -^> "Star Wars Battlefront II (*.fbmodel)"
echo.
echo Max muss dafuer NEU GESTARTET werden.
goto ende

REM ==========================================================================
:packe
if not exist "%OUTPUT%\%~1\SWBF2Import.dlu" goto :eof
mkdir "%PKG%\Contents\%~1" >nul 2>&1
copy /Y "%OUTPUT%\%~1\SWBF2Import.dlu" "%PKG%\Contents\%~1\SWBF2Import.dlu" >nul 2>&1
set /a GEFUNDEN+=1
goto :eof

:pruefe_pe
set "PEART="
for %%z in ("%~1") do if %%~zz LSS 1024 set "KAPUTT=%~1 (nur %%~zz Byte)"
if defined KAPUTT goto :eof
for /f "usebackq delims=" %%r in (`powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('%~1'); if($b.Length -lt 512){'zu kurz'; exit}; if($b[0] -ne 0x4D -or $b[1] -ne 0x5A){'kein MZ'; exit}; $e=[BitConverter]::ToInt32($b,60); if($e -le 0 -or $e+6 -ge $b.Length){'PE-Versatz kaputt'; exit}; if($b[$e] -ne 0x50 -or $b[$e+1] -ne 0x45){'kein PE'; exit}; $m=[BitConverter]::ToUInt16($b,$e+4); if($m -eq 0x8664){'x64'} elseif($m -eq 0x14c){'32 Bit'} else{'Maschine 0x{0:X}' -f $m}"`) do set "PEART=%%r"
for %%p in ("%~dp1.") do echo          Max %%~np: %PEART%
for %%p in ("%~dp1.") do >>"%ILOG%" echo    Max %%~np: %PEART%
if /i not "%PEART%"=="x64" set "KAPUTT=%~1 (%PEART%)"
goto :eof

:pruefe_sperre
if not exist "%~1" goto :eof
2>nul (>>"%~1" (call )) || set "GESPERRT=%~1"
goto :eof

REM ==========================================================================
:kaputte_dlu
>>"%ILOG%" echo FEHLER: keine gueltige 64-Bit-DLL: %KAPUTT%
echo.
echo FEHLER: diese Datei ist keine gueltige 64-Bit-DLL:
echo   %KAPUTT%
echo.
echo Genau das meldet Max spaeter als "Error code 193 - is not a valid Win32
echo application". Meist ist es ein Ueberbleibsel aus einem Bau, der nicht
echo durchlief. Den Ordner output\ loeschen und START.bat neu starten.
goto ende

:gesperrt
>>"%ILOG%" echo FEHLER: gesperrt, Max laeuft noch: %GESPERRT%
echo FEHLER: Diese Datei ist gesperrt:
echo   %GESPERRT%
echo.
echo 3ds Max laeuft noch und haelt die alte .dlu fest.
echo Max schliessen und diese Datei erneut starten.
goto ende

:xml_fehlt
>>"%ILOG%" echo FEHLER: PackageContents.xml nicht angekommen
echo FEHLER: PackageContents.xml ist nicht angekommen.
echo Sie MUSS direkt in %DEST% liegen, nicht eine Ebene tiefer.
goto ende

:kein_output
>>"%ILOG%" echo FEHLER: output fehlt
echo FEHLER: der Ordner output\ fehlt.
echo Bitte zuerst START.bat laufen lassen - die baut das Plugin.
goto ende

:nichts_gebaut
>>"%ILOG%" echo FEHLER: keine SWBF2Import.dlu in output
echo FEHLER: in output\ liegt keine einzige SWBF2Import.dlu.
echo Bitte zuerst START.bat laufen lassen.
goto ende

:kein_paket
>>"%ILOG%" echo FEHLER: package\SWBF2Import\PackageContents.xml fehlt
echo FEHLER: package\SWBF2Import\PackageContents.xml fehlt.
goto ende

:kopierfehler
>>"%ILOG%" echo FEHLER beim Kopieren nach %DEST%
echo FEHLER beim Kopieren nach:
echo   %DEST%
echo Laeuft 3ds Max noch, oder fehlen die Rechte?
goto ende

:ende
echo.
REM Von START.bat aus wird nicht angehalten - dort kommt die Pause am Ende.
if defined SWBF2_KEIN_PAUSE goto raus
echo (Fenster bleibt offen - mit einer Taste schliessen)
pause >nul
:raus
endlocal
