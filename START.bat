@echo off
REM ===========================================================================
REM  START.bat - der EINZIGE Einstieg fuer SWBF2 Import.
REM
REM    1  CMake suchen
REM    2  fbdump.exe bauen (ohne Max-SDK) und gegen die Python-Referenz pruefen
REM    3  fuer JEDEN installierten Max-Jahrgang SWBF2Import.dlu bauen
REM    4  das Paket nach %ALLUSERSPROFILE%\Autodesk\ApplicationPlugins installieren
REM       (ohne Adminrechte nach %APPDATA%\Autodesk\ApplicationPlugins)
REM
REM  Keine Schalter, nichts zu waehlen. Aufbau uebernommen von BAUE_ALLE.bat
REM  aus XFBIN Import, das denselben Versionsbereich schon baut.
REM
REM  Diese Datei MUSS Windows-Zeilenenden haben, sonst arbeitet cmd.exe die
REM  Sprungmarken nicht zuverlaessig ab. %ProgramFiles(x86)% steht bewusst
REM  ausserhalb jedes Klammerblocks - die Klammer in "(x86)" wuerde ihn sonst
REM  vorzeitig schliessen.
REM ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "LOG=%~dp0START.log"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "OUTPUT=%~dp0output"
set "PKG=%~dp0package\SWBF2Import"
set "DEST=%APPDATA%\Autodesk\ApplicationPlugins\SWBF2Import"
set "CMAKE="
set "GEN=Visual Studio 17 2022"
set GEBAUT=0

echo START.bat  %DATE% %TIME%> "%LOG%"
echo.
echo ================================================================
echo   SWBF2 Import - bauen, pruefen, installieren
echo ================================================================
echo Ordner:    %CD%
echo Protokoll: %LOG%
REM  Die Pruefskripte haengen ihre Ausgabe selbst ans Protokoll - cmd kennt
REM  kein "tee", und der Umweg ueber eine Zwischendatei ist beim zweiten
REM  Aufruf an einer Dateisperre gescheitert.
set "SWBF2_LOG=%LOG%"
echo.

REM ---- 1. CMake und Visual Studio finden -----------------------------------
where cmake >nul 2>&1
if errorlevel 1 goto suche_vs_cmake
set "CMAKE=cmake"
goto habe_cmake

:suche_vs_cmake
echo CMake steht nicht im PATH - ich nehme das von Visual Studio mitgelieferte.
if not exist "%VSWHERE%" goto kein_cmake
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath 2^>nul`) do call :pruefe_cmake "%%i"
if "%CMAKE%"=="" goto kein_cmake
goto habe_cmake

:pruefe_cmake
if exist "%~1\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%~1\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
goto :eof

:kein_cmake
echo.
echo [1/4] KEIN CMAKE GEFUNDEN.
echo       CMake installieren (https://cmake.org/download/) oder im
echo       Visual-Studio-Installer "C++-CMake-Tools fuer Windows" nachtragen.
goto ende

:habe_cmake
if not exist "%VSWHERE%" goto generator_fertig
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [18.0^,19.0^) -property installationPath 2^>nul`) do set "GEN=Visual Studio 18 2026"

:generator_fertig
echo [1/4] CMake: %CMAKE%
echo       Generator: %GEN%
echo CMake: %CMAKE%  Generator: %GEN%>> "%LOG%"

REM ---- 2. Werkzeug bauen ---------------------------------------------------
echo [2/4] Werkzeug fbdump.exe bauen (ohne Max-SDK) ...
"%CMAKE%" -S . -B build_tool -G "%GEN%" -A x64 -DSWBF2_BUILD_PLUGIN=OFF -DSWBF2_BUILD_TOOL=ON >> "%LOG%" 2>&1
if errorlevel 1 goto werkzeugfehler
"%CMAKE%" --build build_tool --config Release >> "%LOG%" 2>&1
if errorlevel 1 goto werkzeugfehler
set "EXE="
set "EXE_CASTOOL="
if exist "build_tool\bin\Release\castool.exe" set "EXE_CASTOOL=%~dp0build_tool\bin\Release\castool.exe"
if not defined EXE_CASTOOL if exist "build_tool\bin\castool.exe" set "EXE_CASTOOL=%~dp0build_tool\bin\castool.exe"
if exist "build_tool\bin\Release\fbdump.exe" set "EXE=build_tool\bin\Release\fbdump.exe"
if exist "build_tool\bin\fbdump.exe" set "EXE=build_tool\bin\fbdump.exe"
if "%EXE%"=="" goto werkzeugfehler
echo       ok - %EXE%

echo       Gegenprobe gegen die Python-Referenz ...
where python >nul 2>&1
if errorlevel 1 echo       Python steht nicht im PATH - Gegenprobe faellt aus.
if errorlevel 1 goto plugins
python tools\VERGLEICHE.py

REM ---- Containerschicht gegen das Mini-Spiel halten ------------------------
REM  fbtools baut sich mit "tests\synth_test.py --keep <ordner>" ein
REM  vollstaendiges Mini-Spiel: alle fuenf Entpackwege, ein Delta-Patch, ein
REM  Manifest, ein Bundle. Genau daran wird der C++-Unterbau gemessen.
echo.
echo       Containerschicht gegen das Mini-Spiel ...
set "MINI=%TEMP%\swbf2_minispiel"
for /f "usebackq delims=" %%i in (`python tools\FINDE_FBTOOLS.py 2^>nul`) do set "FBT=%%i"
if not defined FBT echo       fbtools nicht gefunden - uebersprungen.
if not defined FBT goto echtes_spiel
rmdir /s /q "%MINI%" >nul 2>&1
python "%FBT%\tests\synth_test.py" --keep "%MINI%" >nul 2>&1
if not exist "%MINI%" echo       Mini-Spiel liess sich nicht bauen (zstandard/lz4 fehlen?)
if not exist "%MINI%" goto echtes_spiel
python tools\VERGLEICHE_CONTAINER.py "%MINI%" "%FBT%"
rmdir /s /q "%MINI%" >nul 2>&1
goto echtes_spiel

:werkzeugfehler
echo       FEHLGESCHLAGEN. Die letzten Zeilen aus dem Protokoll:
echo ----------------------------------------------------------------
powershell -NoProfile -Command "Get-Content -Tail 30 '%LOG%'"
echo ----------------------------------------------------------------
echo Vollstaendiges Protokoll: %LOG%
goto ende

REM ---- MeshSet-Leser gegen fbtools halten ---------------------------------
REM  Geprueft wird an ECHTEN Spieldaten: den .res- und .chunk-Dateien, die
REM  fbtools bei einem Lauf unter ERGEBNIS\meshprobleme\ sichert.
:echtes_spiel
if not defined FBT goto spiel_suchen
echo.
echo       MeshSet-Leser gegen fbtools ...
REM  Das Skript sucht die gesicherten MeshSets selbst und meldet ins
REM  Protokoll, wenn es keine findet. Frueher entschied das die Batchdatei
REM  ueber einen festen Pfad - und ihre Meldung landete nur im Fenster,
REM  nie im Log.
python tools\VERGLEICHE_MESH.py "%FBT%\ERGEBNIS\meshprobleme" "%FBT%"

echo.
echo       EBX-Leser gegen fbtools ...
python tools\VERGLEICHE_EBX.py "%FBT%\ERGEBNIS\dump" "%FBT%"

echo.
echo       GD-Baenke gegen fbtools ...
python tools\VERGLEICHE_GD.py "%FBT%\ERGEBNIS\dump_anim" "%FBT%"

REM ---- Probe am ECHTEN Spiel ----------------------------------------------
REM  Der Index ueber alle 4.777 Bundles. Das dauert etwas und ist die
REM  eigentliche Probe fuer den ganzen Unterbau.
:spiel_suchen
set "SPIEL="
if exist "%ProgramFiles%\EA Games\STAR WARS Battlefront II\Data\layout.toc" set "SPIEL=%ProgramFiles%\EA Games\STAR WARS Battlefront II"
if not defined SPIEL if exist "C:\Program Files\EA Games\STAR WARS Battlefront II\Data\layout.toc" set "SPIEL=C:\Program Files\EA Games\STAR WARS Battlefront II"
if not defined SPIEL if exist "D:\Program Files\EA Games\STAR WARS Battlefront II\Data\layout.toc" set "SPIEL=D:\Program Files\EA Games\STAR WARS Battlefront II"
if not defined SPIEL echo       Battlefront II nicht gefunden - Probe am echten Spiel entfaellt.
if not defined SPIEL goto plugins
if not defined EXE_CASTOOL goto plugins
echo.
echo       Index ueber das ECHTE Spiel bauen (dauert etwas) ...
echo       %SPIEL%
REM  In EINEM Lauf: die Figurenliste UND Anakin komplett herausziehen. Der
REM  Index ueber 4.777 Bundles ist ohnehin gebaut - ein zweiter Lauf waere
REM  reine Wartezeit.
REM  --cache legt den Index ab. Beim naechsten Lauf wird er geladen statt neu
REM  ueber 4.777 Bundles gebaut; nach einem Spiel-Patch faellt er von selbst
REM  weg, weil die Kopfnummer mit drinsteht.
"%EXE_CASTOOL%" --figuren "%SPIEL%" --extrahiere vur_anakin_01_bpb "%~dp0figur_direkt.fbmodel" --cache "%~dp0index.fbidx" --waffen "%~dp0figur_waffen.fbmodel" > "%~dp0figuren.txt" 2>&1
if errorlevel 1 echo       FEHLGESCHLAGEN - die ersten Zeilen:
if errorlevel 1 powershell -NoProfile -Command "Get-Content -TotalCount 5 '%~dp0figuren.txt'"
if errorlevel 1 goto plugins
REM  Eigener Dateiname je Verwendung - ein gemeinsamer hat sich beim zweiten
REM  Aufruf selbst gesperrt.
powershell -NoProfile -Command "Select-String -Path '%~dp0figuren.txt' -Pattern '^INDEX|^FIGUREN |^FIGUR ' | ForEach-Object { $_.Line }" > "%TEMP%\swbf2_kopf.txt" 2>&1
type "%TEMP%\swbf2_kopf.txt"
type "%TEMP%\swbf2_kopf.txt" >> "%LOG%"
del "%TEMP%\swbf2_kopf.txt" >nul 2>&1
echo       vollstaendige Liste: figuren.txt
powershell -NoProfile -Command "Select-String -Path '%~dp0figuren.txt' -Pattern 'characters/hero/' | Select-Object -First 12 | ForEach-Object { $_.Line }" > "%TEMP%\swbf2_helden.txt" 2>&1
type "%TEMP%\swbf2_helden.txt"
type "%TEMP%\swbf2_helden.txt" >> "%LOG%"
del "%TEMP%\swbf2_helden.txt" >nul 2>&1

REM  Materialien und Texturen von Anakin MESSEN (Vorarbeit fuer Stufe 4):
REM  jede EBX im Bundle mit Typ, je MeshAsset die Materials samt
REM  TextureParameters, die Eintraege der MeshVariationDatabase, je Textur
REM  der Kopf mit Gegenprobe. Alles ins START.log - daraus entsteht der
REM  Texturimport, nicht aus Vermutungen.
echo       Materialien und Texturen von Anakin messen ...
"%EXE_CASTOOL%" --material "%SPIEL%" vur_anakin_01_bpb --cache "%~dp0index.fbidx" > "%TEMP%\swbf2_material.txt" 2>&1
echo.>> "%LOG%"
echo ================================================================>> "%LOG%"
echo   Materialmessung vur_anakin_01_bpb>> "%LOG%"
echo ================================================================>> "%LOG%"
type "%TEMP%\swbf2_material.txt" >> "%LOG%"
powershell -NoProfile -Command "Select-String -Path '%TEMP%\swbf2_material.txt' -Pattern '^TEXTUREN |^MVDB |^MESHASSET ' | ForEach-Object { $_.Line }"
del "%TEMP%\swbf2_material.txt" >nul 2>&1

REM  Stufe 4: die Texturen von Anakin dekodieren (BC1/BC5/BC7 -> PNG), samt
REM  Beipackzettel - derselbe Weg wie im Figurenfenster. Je Textur stehen
REM  Mittelwerte der Kanaele im Log, bei Normalen, ob das alte B schon Z war.
echo       Texturen von Anakin dekodieren ...
"%EXE_CASTOOL%" --texturen "%SPIEL%" vur_anakin_01_bpb "%~dp0texturen_anakin" --cache "%~dp0index.fbidx" > "%TEMP%\swbf2_texturen.txt" 2>&1
echo.>> "%LOG%"
echo ================================================================>> "%LOG%"
echo   Texturen vur_anakin_01_bpb ^(Ordner texturen_anakin^)>> "%LOG%"
echo ================================================================>> "%LOG%"
type "%TEMP%\swbf2_texturen.txt" >> "%LOG%"
powershell -NoProfile -Command "Select-String -Path '%TEMP%\swbf2_texturen.txt' -Pattern '^MATERIALIEN |^TEXTUREN |^BEIPACK ' | ForEach-Object { $_.Line }"
del "%TEMP%\swbf2_texturen.txt" >nul 2>&1

REM  Stufe 5a: alle Animationsclips in allen AssetBanks lesen und zaehlen.
REM  (0.36.0 bei DH: 82.229 Clips in 307 Baenken - die 1.469 aus fbtools
REM  waren eine Stichprobe von animstat, nicht die Gesamtzahl.)
echo       Animationsclips zaehlen ...
"%EXE_CASTOOL%" --clips "%SPIEL%" --cache "%~dp0index.fbidx" --liste "%~dp0clips.txt" --figur anakin,darthvader,vader,countdooku,dooku,generalgrievous,grievous,darthmaul,maul,kyloren,kylo,trooper,stormtrooper,clone,droid,rebel,soldier,infantry,rifle,assault,heavy,officer,specialist,walrus,humanmale > "%TEMP%\swbf2_clips.txt" 2>&1
type "%TEMP%\swbf2_clips.txt"
echo.>> "%LOG%"
echo ================================================================>> "%LOG%"
echo   Animationsclips ^(vollstaendige Liste: clips.txt^)>> "%LOG%"
echo ================================================================>> "%LOG%"
type "%TEMP%\swbf2_clips.txt" >> "%LOG%"
del "%TEMP%\swbf2_clips.txt" >nul 2>&1

REM  Vorarbeit Stufe 5b: die Namenskette je Clip MESSEN (ChannelToDof ->
REM  RigAsset -> DofSets -> Slotnamen). Selbstpruefung: Drehkanaele muessen
REM  auf .q enden, Vektorkanaele auf .t/.s. Einmal an der Referenzbank von
REM  fbtools (outro_team1), einmal an Anakin.
echo       Namenskette der Clips messen ...
"%EXE_CASTOOL%" --clipinfo "%SPIEL%" outro_team1 --max 2 --cache "%~dp0index.fbidx" > "%TEMP%\swbf2_clipinfo.txt" 2>&1
"%EXE_CASTOOL%" --clipinfo "%SPIEL%" anakin --max 4 --cache "%~dp0index.fbidx" >> "%TEMP%\swbf2_clipinfo.txt" 2>&1
echo.>> "%LOG%"
echo ================================================================>> "%LOG%"
echo   Namenskette der Clips ^(Vorarbeit Stufe 5b^)>> "%LOG%"
echo ================================================================>> "%LOG%"
type "%TEMP%\swbf2_clipinfo.txt" >> "%LOG%"
del "%TEMP%\swbf2_clipinfo.txt" >nul 2>&1

REM  Stufe 5b: RAW- und Frame-Clips in C++ entpacken und die KURVEN gegen die
REM  .fbanim-Dateien von fbtools halten (Namen, Art, Keyzeiten, jeder Wert).
python tools\VERGLEICHE_ANIM.py "%FBT%\ERGEBNIS" "%FBT%" "%SPIEL%" "%~dp0index.fbidx" "%EXE_CASTOOL%"
REM  Vor den Keys in Max: welche Lesart der Quaternion stimmt, und ob die
REM  Verschiebungen im selben Raum wie die Ruhelage stehen - gemessen am
REM  ersten Key gegen die Ruhelage des Skeletts.
"%EXE_CASTOOL%" --pose "%SPIEL%" outro_team1 --max 2 --cache "%~dp0index.fbidx" > "%TEMP%\swbf2_pose.txt" 2>&1
REM  Vor dem Waffenimport: Anakins Lichtschwert (und was sonst unter
REM  gameplay/equipment/ seinen Namen traegt) messen - starr oder geskinnt?
"%EXE_CASTOOL%" --waffe "%SPIEL%" anakin --cache "%~dp0index.fbidx" >> "%TEMP%\swbf2_pose.txt" 2>&1
REM  Ablage des Clipverzeichnisses (0.41.0): bauen, schreiben, laden und
REM  jedes Feld vergleichen - die Ablage darf nichts ungenauer machen.
"%EXE_CASTOOL%" --clipablage "%SPIEL%" "%TEMP%\swbf2_probe.fbclips" --cache "%~dp0index.fbidx" >> "%TEMP%\swbf2_pose.txt" 2>&1
del "%TEMP%\swbf2_probe.fbclips" >nul 2>&1
REM  Verzerrte Koepfe (0.42.0): welche Gesichtspose gehoert zu Anakin?
REM  0.43.0: Aufbau der Basispose am Kopf und der Lichtschwert-Emitter
REM  (Klinge ist ein Effekt) als Text.
"%EXE_CASTOOL%" --ebxdump "%SPIEL%" characters/heads/heads_anakin/heads_anakin_01/heads_anakin_01 --max 80 --cache "%~dp0index.fbidx" >> "%TEMP%\swbf2_pose.txt" 2>&1
"%EXE_CASTOOL%" --ebxdump "%SPIEL%" fx/weapons/lightsabers/emitters/em_lightsaber_anakin_meshp_3p --max 450 --cache "%~dp0index.fbidx" >> "%TEMP%\swbf2_pose.txt" 2>&1
REM  0.43.1: BasePoseTransforms sind bei Anakin LEER - die Gesichtspose haengt
REM  am FacePoserLibrary-AntRef des VisualUnlock. GUID -> Bank-Key nach
REM  Frostys Regel (AntAsset.SafeGuid) und die Felder des Treffers.
"%EXE_CASTOOL%" --gesicht "%SPIEL%" win32/characters/hero/anakin/anakin_01/vur_anakin_01_bpb --cache "%~dp0index.fbidx" >> "%TEMP%\swbf2_pose.txt" 2>&1
REM  0.49.0: Fahrzeuge - MeshSets, Skelett und Clips je Fahrzeugordner.
"%EXE_CASTOOL%" --fahrzeuge "%SPIEL%" --cache "%~dp0index.fbidx" >> "%TEMP%\swbf2_pose.txt" 2>&1
"%EXE_CASTOOL%" --fahrzeugliste "%SPIEL%" --cache "%~dp0index.fbidx" >> "%TEMP%\swbf2_pose.txt" 2>&1
type "%TEMP%\swbf2_pose.txt"
type "%TEMP%\swbf2_pose.txt" >> "%LOG%"
del "%TEMP%\swbf2_pose.txt" >nul 2>&1

REM  Groesste cas-Datei melden. Bis 0.32.0 las die Containerschicht mit einem
REM  32-Bit-Versatz (long unter Windows); ab 2 GiB waere still falsch gelesen
REM  worden. Seit 0.33.0 ist es 64 Bit - die Zeile sagt, ob es je zugeschlagen
REM  haette.
powershell -NoProfile -Command "$m = Get-ChildItem -LiteralPath '%SPIEL%' -Recurse -Filter 'cas_*.cas' -File -ErrorAction SilentlyContinue | Sort-Object Length -Descending | Select-Object -First 1; if ($m) { 'CAS groesste Datei: {0} Byte ({1}) {2}' -f $m.Length, $m.FullName, $(if ($m.Length -ge 2147483648) { '- UEBER 2 GiB' } else { '- unter 2 GiB' }) } else { 'CAS keine cas-Dateien gefunden' }" > "%TEMP%\swbf2_cas.txt" 2>&1
type "%TEMP%\swbf2_cas.txt"
type "%TEMP%\swbf2_cas.txt" >> "%LOG%"
del "%TEMP%\swbf2_cas.txt" >nul 2>&1

REM  Die byteweise Gegenprobe MUSS hier stehen, nach der Extraktion.
REM  VERGLEICHE.py laeuft in Schritt 2 - da gibt es die Figur noch nicht.
python tools\VERGLEICHE_FIGUR.py "%~dp0figur_direkt.fbmodel" "%FBT%"
REM  Und der Weg ueber das Figurenfenster: dieselbe Figur, im Max-Prozess
REM  gebaut und in der Ablage liegengelassen - muss byteweise gleich sein.
python tools\VERGLEICHE_FENSTER.py "%~dp0figur_waffen.fbmodel"
goto plugins

REM ---- 3. Plugin je Max-Jahrgang -------------------------------------------
:plugins
echo.
echo [3/4] Plugin bauen, fuer jeden installierten Max-Jahrgang ...
for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do call :baue_version %%V
if "%GEBAUT%"=="0" goto kein_sdk
goto installieren

:baue_version
set "VER=%~1"
set "SDK=C:\Program Files\Autodesk\3ds Max %VER% SDK\maxsdk"
if not exist "%SDK%\include\max.h" goto :eof
echo       Max %VER% ...
"%CMAKE%" -S . -B "build_%VER%" -G "%GEN%" -A x64 -DSWBF2_BUILD_PLUGIN=ON -DSWBF2_BUILD_TOOL=OFF -D3DSMAX_SDK_DIR="%SDK%" -DMAX_VERSION=%VER% >> "%LOG%" 2>&1
if errorlevel 1 goto :baufehler_version
"%CMAKE%" --build "build_%VER%" --config Release >> "%LOG%" 2>&1
if errorlevel 1 goto :baufehler_version
mkdir "%OUTPUT%\%VER%" >nul 2>&1
REM  Die .dlu GENAU bestimmen, nicht per Platzhalter suchen.
REM
REM  "SWBF2Import.dl*" sieht harmlos aus, trifft aber auch
REM  SWBF2Import.dlu.RECIPE - eine kleine XML-Datei, die MSBuild beim Binden
REM  anlegt. Die Schleife kopierte sie NACH der echten DLL auf denselben
REM  Zielnamen; heraus kam eine 498 Byte grosse "DLL", und Max meldete beim
REM  Start "Error code 193 - is not a valid Win32 application".
REM
REM  Deshalb: zuerst am festen Ort nachsehen, den CMake vorgibt, und beim
REM  Suchen die Endung ausdruecklich pruefen.
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
REM  WICHTIG: eine alte .dlu aus einem frueheren Lauf wegraeumen. Sonst bleibt
REM  sie in output\ liegen, wird installiert und Max laedt eine Fassung, die
REM  zum heutigen Quelltext gar nicht mehr passt - im schlimmsten Fall mit
REM  "Error code 193".
if exist "%OUTPUT%\%VER%\SWBF2Import.dlu" del /q "%OUTPUT%\%VER%\SWBF2Import.dlu"
echo             FEHLGESCHLAGEN - die Fehlerzeilen:
findstr /R /C:"error C" /C:"error LNK" /C:"error MSB" "%LOG%" 2>nul
echo             Vollstaendiges Protokoll: %LOG%
goto :eof

REM ---- 4. Installieren -----------------------------------------------------
:installieren
echo.
echo [4/4] Paket bauen und nach ProgramData installieren ...
set SWBF2_KEIN_PAUSE=1
call "%~dp0INSTALLIERE.bat"
set SWBF2_KEIN_PAUSE=
goto ende

:kein_sdk
echo       Kein Max-SDK gefunden. Ohne SDK gibt es nur das Werkzeug.
echo       Das SDK ist ein eigener Download von Autodesk und landet unter
echo       C:\Program Files\Autodesk\3ds Max ^<Jahr^> SDK
goto ende

:ende
REM ---- Protokolle mit ins START.log ------------------------------------
REM  Zuerst Max' eigenes Protokoll (Max.log, laut Autodesk unter
REM  %LOCALAPPDATA%\Autodesk\3dsMax\<Jahr>\<Sprache>\Network): Max schreibt dort,
REM  welche Pakete und Plug-ins geladen wurden und was dabei scheiterte -
REM  auch Meldungen wie "Core interface ... will not be registered".
powershell -NoProfile -Command "$w = Join-Path $env:LOCALAPPDATA 'Autodesk\3dsMax'; if (Test-Path $w) { Get-ChildItem -Path $w -Filter 'Max.log' -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { $f = $_.FullName; Select-String -LiteralPath $f -Pattern 'SWBF2','Swbf2Cpp','EAfront' -SimpleMatch | Select-Object -Last 40 | ForEach-Object { $f + ': ' + $_.Line.Trim() } } } else { 'Max.log: kein Ordner ' + $w }" > "%TEMP%\swbf2_maxlog.txt" 2>&1
echo.>> "%LOG%"
echo ================================================================>> "%LOG%"
echo   Max.log - Zeilen zu SWBF2 ^(Pakete, Plug-ins, Fehler^)>> "%LOG%"
echo ================================================================>> "%LOG%"
type "%TEMP%\swbf2_maxlog.txt" >> "%LOG%"
del "%TEMP%\swbf2_maxlog.txt" >nul 2>&1
REM  Was die .dlu beim Start selbst gemessen hat (LibInitialize, seit 0.33.2),
REM  und die Diagnose des Menues, wenn es das Plugin nicht erreicht hat.
if not exist "%LOCALAPPDATA%\SWBF2Import\start.log" goto kein_startlog
echo.>> "%LOG%"
echo ================================================================>> "%LOG%"
echo   start.log der SWBF2Import.dlu ^(beim letzten Start von 3ds Max^)>> "%LOG%"
echo ================================================================>> "%LOG%"
type "%LOCALAPPDATA%\SWBF2Import\start.log" >> "%LOG%"
:kein_startlog
if not exist "%USERPROFILE%\Downloads\EAfront Diagnose.log" goto kein_diagnoselog
echo.>> "%LOG%"
echo ================================================================>> "%LOG%"
echo   EAfront Diagnose.log ^(Menue fand das Plugin nicht^)>> "%LOG%"
echo ================================================================>> "%LOG%"
type "%USERPROFILE%\Downloads\EAfront Diagnose.log" >> "%LOG%"
:kein_diagnoselog
REM  Seit 0.24.1 gehoerte das Import-Protokoll aus Max hierher. Beim Umbau
REM  auf die Containerschicht ging der Block verloren - 0.32.0 hatte ihn
REM  nicht mehr, deshalb stand in DHs START.log kein Max-Protokoll.
REM  Jetzt wieder da, dazu das Protokoll der Installation.
if not exist "%~dp0INSTALLIERE.log" goto kein_installog
echo.>> "%LOG%"
echo ================================================================>> "%LOG%"
echo   INSTALLIERE.log>> "%LOG%"
echo ================================================================>> "%LOG%"
type "%~dp0INSTALLIERE.log" >> "%LOG%"
:kein_installog
set "IMPLOG=%USERPROFILE%\Downloads\Import swbf2.log"
if not exist "%IMPLOG%" goto kein_implog
echo.>> "%LOG%"
echo ================================================================>> "%LOG%"
echo   Import swbf2.log ^(letzter Import oder letztes Figurenfenster in 3ds Max^)>> "%LOG%"
echo ================================================================>> "%LOG%"
type "%IMPLOG%" >> "%LOG%"
echo.
echo Das Import-Protokoll aus Max steht mit im START.log - eine Datei genuegt.
goto implog_fertig
:kein_implog
echo.
echo (Noch kein Import-Protokoll - nach dem ersten Import in 3ds Max steht es
echo  beim naechsten Lauf mit im START.log.)
:implog_fertig
echo.
echo (Fenster bleibt offen - mit einer Taste schliessen)
pause >nul
endlocal
