# SWBF2 Import — 3ds Max importer for Star Wars Battlefront II (2017)

Imports characters, heroes, soldiers and vehicles from **Star Wars Battlefront II
(2017)** directly from your game installation into **3ds Max 2016–2027** —
skeleton, skinned meshes, materials/textures and animations (with note track and
sequences).

*Importiert Figuren, Helden, Soldaten und Fahrzeuge aus Star Wars Battlefront II
(2017) direkt aus der Spielinstallation in 3ds Max 2016–2027 — Skelett, geskinnte
Meshes, Materialien/Texturen und Animationen.*

## Installation

1. Download the latest release: **SWBF2Import-&lt;version&gt;-Setup.exe**
   (or the ZIP) from the [Releases](../../releases) page.
2. Close 3ds Max, run the setup. It installs for all users
   (`%ProgramData%\Autodesk\ApplicationPlugins\SWBF2Import`) or — if you choose —
   only for you (`%AppData%\…`), and installs the Microsoft Visual C++ runtime if
   it is missing.
3. Start 3ds Max: menu **EAfront Tool**, and *File → Import →
   Star Wars Battlefront II*.

ZIP instead of setup: extract everything, run `Installieren.bat`
(uninstall: `Deinstallieren.bat`).

**Requirements:** Windows 10/11 64-bit, 3ds Max 2016–2027, Star Wars
Battlefront II (2017) installed (the plugin reads the game data from your own
installation; no game data is included), Microsoft Visual C++ runtime 14.50+
([vc_redist.x64](https://aka.ms/vc14/vc_redist.x64.exe), the setup installs it).

**Windows warning:** the setup is not code-signed yet, so Windows SmartScreen may
show *"Windows protected your PC"* → *More info* → *Run anyway*. Compare the
file with `SHA256SUMS.txt` from the release page.

## Building

Visual Studio 2022/2026 with C++, CMake, and the 3ds Max SDKs of the versions
you want. `BUILD.bat` builds every installed SDK year; `installer\BAUE_RELEASE.bat`
creates the setup (Inno Setup 6) and the ZIP. Details (German): below and in
`CODING.md`.

## License

GPL-3.0 (see `LICENSE`), with an additional permission under GPL-3.0 §7:
you may link this program with the Autodesk 3ds Max SDK libraries and distribute
the resulting plugin. Bundled third-party code keeps its own license
(`vendor/`: lz4 BSD-2-Clause, miniz MIT, zstd BSD, bcdec MIT/Unlicense).

**Disclaimer:** unofficial, free fan project. It is **not** made by, affiliated
with, endorsed or sponsored by **Lucasfilm Ltd.**, **The Walt Disney Company**,
Electronic Arts, DICE or Autodesk. Star Wars is a trademark of Lucasfilm Ltd. /
Disney; Star Wars Battlefront II belongs to Electronic Arts; 3ds Max is a
trademark of Autodesk. All trademarks belong to their respective owners. No game
data is included — the plugin reads the files of your own installation.

*Hinweis: Inoffizielles, kostenloses Fan-Projekt — **nicht** von Lucasfilm,
Disney, EA, DICE oder Autodesk und nicht mit ihnen verbunden. Alle Marken gehören
ihren Inhabern. Es sind keine Spieldaten enthalten.*

---

# Entwicklerdoku (Deutsch)

Stand 1.43.0. Der ganze Weg vom Spiel bis zur Figur steht in C++ und ist
byteweise gegen fbtools bewiesen; Skelett und Meshes laufen in Max. Das
**Figurenfenster** holt eine Figur direkt aus dem Spiel — Spielordner, Liste,
Import, ohne Umweg über eine Datei.

Vorbild ist der XFBIN-Importer: erst der Parser mit Gegenprobe, dann die
Max-SDK-Schicht, jede Stufe mit 0 Abweichungen gegen Python.

---

## Woher die Daten kommen

`fbtools` holt alles aus dem Spiel und schreibt zwei Dumps:

| Datei | Inhalt | geschrieben von |
|---|---|---|
| `figur\<bundle>.fbmodel` | Skelett mit Ruhelage, alle Meshes je LOD und Section, Positionen, Normalen, Tangenten, UVs, Farben, **Bone-Indizes und Gewichte**, Dreiecke, Materialnamen, Bone-Liste je Section | `fb_model.py` |
| `ERGEBNIS\animation_<n>.fbanim` | ein Clip: Keyzeiten, Skelett, Kurven je Kanal (Drehung, Verschiebung, Float, konstant oder bewegt) | `fb_animdump.py` |

Daneben liegen `.json` und `.txt` mit demselben Inhalt. Die JSON ist zum
Nachschauen, die TXT ist die Referenz für die Gegenprobe.

**Warum nicht OBJ:** OBJ trägt weder Bone-Indizes noch Gewichte, kein Skelett und
keine Materialzuordnung. Für den Skin-Modifier reicht das nicht.

**Warum binär:** eine `animation_*.json` ist gut zwei Megabyte Text, die
`.fbanim` daneben 274 Kilobyte — achtmal kleiner, und ohne JSON-Parser in C++.

---

## Was hier herauskommt

`START.bat` — ein Doppelklick, und die Datei

1. baut `fbdump.exe` und prüft den Leser gegen die Python-Referenz,
2. baut für **jeden installierten Max-Jahrgang** `SWBF2Import.dlu`,
3. packt alles und installiert es nach
   `%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins\SWBF2Import` (ohne
   Adminrechte nach `%APPDATA%\...`).

Danach gibt es in 3ds Max zwei Wege:

- **Menü „EAfront Tool" → „SWBF2 importieren"** öffnet das Figurenfenster
  (siehe unten).
- **Datei → Importieren** → *Star Wars Battlefront II (\*.fbmodel)* liest einen
  Modelldump aus fbtools, wie bisher.

Aufbau und Bauregeln sind von **XFBIN Import** übernommen, das denselben
Versionsbereich 2016 bis 2027 schon baut.

## Installieren — wohin und warum

3ds Max durchsucht laut Autodesk-Doku genau **zwei** Orte nach Plugin-Paketen
(ab Max 2019 zusätzlich die Pfade in `ADSK_APPLICATION_PLUGINS`):

| Ort | Für wen | Rechte |
|---|---|---|
| `%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins\SWBF2Import`<br>also `C:\ProgramData\...` | alle Benutzer des Rechners | Administrator |
| `%APPDATA%\Autodesk\ApplicationPlugins\SWBF2Import` | nur der angemeldete Benutzer | keine |

Autodesk nennt den ersten den **bevorzugten** Ort; der zweite ist ausdrücklich
der Ausweichweg, wenn keine Adminrechte da sind. Beide gelten für jede
Max-Fassung, die das Paketformat kennt — das ist seit Max 2013 der Fall.

`INSTALLIERE.bat` nimmt deshalb **ProgramData**. Fehlen die Rechte, fragt sie
einmal nach Erhöhung; klappt auch das nicht, weicht sie auf den Benutzerordner
aus und sagt es. `START.bat` ruft sie am Ende selbst auf.

Wichtig und leicht falsch zu machen: die `PackageContents.xml` muss **direkt**
im ersten Unterordner liegen — `...\ApplicationPlugins\SWBF2Import\PackageContents.xml`.
Eine Ebene tiefer findet Max sie nicht. Die Installationsdatei prüft das am Ende
nach.

**Zwei Sicherungen gegen veraltete Dateien.** Schlägt der Bau für einen Jahrgang
fehl, löscht `START.bat` die alte `.dlu` aus `output\` — sonst würde sie
installiert und Max lädt eine Fassung, die zum heutigen Quelltext nicht mehr
passt. Und vor dem Kopieren prüft `INSTALLIERE.bat` jede `.dlu` auf ein gültiges
64-Bit-PE: `MZ` am Anfang, `PE` an der Stelle, auf die der Versatz bei 0x3C
zeigt, dahinter die Maschinenkennung 0x8664. Genau das meldet Max sonst beim
Start als *„Error code 193 — is not a valid Win32 application"*. Der Zielordner
`Contents` wird außerdem vor dem Kopieren geleert, weil `xcopy` am Ziel nichts
löscht, was in der Quelle fehlt.

Liegt dasselbe Paket an **beiden** Orten, weiß niemand, welche `.dlu` Max nimmt —
beide tragen denselben `UpgradeCode`. Nach einer Installation in ProgramData
räumt die Datei eine ältere Installation unter AppData deshalb weg.

Nach der Installation muss 3ds Max **neu gestartet** werden.

**Das Menü.** Bis Max 2024 legt es ein Post-Start-Up-Skript über `menuMan` an.
Ab 2025 läuft das Menüskript als **Pre-Start-Up**: der Callback
`#cuiRegisterMenus` muss angemeldet sein, bevor Max die Menüs aufbaut. Bis
0.32.0 stand es als Post-Start-Up im Paket — laut Autodesk zu spät; dass das
Menü trotzdem erschien, war nicht dokumentiertes Verhalten (CODING.md 14).

## Das Figurenfenster (0.33.0)

Menü **EAfront Tool → SWBF2 importieren**. Das Fenster ist ein Win32-Dialog in
der `.dlu` (`src/swbf2import_fenster.cpp`, Vorlage `src/swbf2import.rc`):

1. **Spielordner** — wird selbst gesucht (EA-App-, Origin- und Steam-Pfade) und
   gemerkt; „Browse…" wählt einen anderen. Gültig ist ein Ordner mit
   `Data\layout.toc`.
2. **Index** — beim ersten Mal über alle 4.777 Bundles gebaut (mit Fortschritt
   und Abbrechen), danach aus der Ablage geladen. Die Kopfnummer des Spiels
   steht mit drin; nach einem Patch wird neu gebaut.
3. **Liste** — Reiter Heroes / Light side / Dark side / Other / All mit Anzahl,
   Suchfeld (mehrere Wörter, alle müssen passen), Egoperspektive-Bundles
   (`_bundle1p`) standardmäßig aus. Die Liste kommt aus `fbauswahl` —
   derselben Funktion, die castool benutzt.
4. **Import** — Doppelklick oder „Import". Die Figur wird aus den Spieldateien
   gebaut, als `.fbmodel` in der Ablage abgelegt und dann über **genau
   denselben Weg** importiert wie über Datei → Importieren. Bone- und
   Mesh-Gegenprobe liegen daneben.

**Threads.** Das Fenster ist modal, die Arbeit (Spiel öffnen, Index, Figur
bauen) läuft in **einem** Arbeitsthread ohne einen einzigen SDK-Aufruf — so
beschreibt es das SDK für Importer. Knoten, Bones und Meshes entstehen im
Hauptthread. Der Thread hat genau einen Besitzer, wird immer eingesammelt, und
keine Ausnahme verlässt ihn (CODING.md 15).

**Aussehen.** Alle Farben aus Max' Theme (`GetCustSysColor`), Knöpfe und Liste
selbst gezeichnet, feine Ränder statt heller Windows-Striche, dunkle
Titelleiste im dunklen Theme. Die Kontraste werden gemessen und ins Protokoll
geschrieben (WCAG 2.2: Text mindestens 4,5:1); reicht die Auswahlschrift nicht,
nimmt das Fenster Schwarz oder Weiß. Abstände nach Microsofts Layoutregeln.

**Ablage** unter `%LOCALAPPDATA%\SWBF2Import`: `index.fbidx`,
`einstellungen.ini` (Spielordner, Reiter) und `figuren\<bundle>.fbmodel` mit
den Gegenproben daneben.

**Protokoll.** Das Fenster schreibt in dasselbe `Import swbf2.log` wie jeder
Import: Kontraste, Spielordner, Index (Quelle, Zahlen, Zeit), Figurenzahl, je
Figur die Kennzahlen und — als Messung für die offene Skelettfrage — die
EBX-Namen im Bundle, die nach Skelett aussehen.

**MAXScript:** `Swbf2Cpp.showDialog()` und `Swbf2Cpp.version()`. Bis 0.33.0
hieß die Schnittstelle `SWBF2Import` und war in Max nicht erreichbar (siehe
unten).

**Zweiter Weg ins Fenster:** Datei → Importieren → die `Data\layout.toc` des
Spiels wählen. Das hängt nicht an MAXScript; das Menü nutzt es als Rückweg.

### 0.33.1 — „SWBF2Import.dlu ist nicht geladen"

DHs erster Lauf von 0.33.0: Bau 12/12, Installation 12/12 (x64 geprüft), aber
das Menü meldete, die `.dlu` sei nicht geladen — `SWBF2Import.version()` war
aus MAXScript nicht erreichbar. Was 0.33.1 dagegen tut, **ohne** die Ursache
schon zu kennen:

- Die Schnittstelle heißt jetzt `Swbf2Cpp`. Der alte Name glich dem
  Klassennamen „SWBF2 Import" bis auf das Leerzeichen, und MAXScript macht aus
  Klassennamen selbst globale Namen. **Unbewiesen** — deshalb:
- das Menü **misst**, wenn `Swbf2Cpp` fehlt: Fehlertext, Kerninterface
  registriert ja/nein, Importer-Klasse geladen ja/nein, wofür der alte Name
  steht, welche `SWBF2Import.dlu` und welche `msvcp140.dll` im Prozess stecken
  (Pfad und Dateifassung). Das geht in den Listener und nach
  `Downloads\Import swbf2.log` — START.bat hängt es ans START.log;
- `START.bat` hängt zusätzlich die Zeilen zu SWBF2 aus Max' eigenem `Max.log`
  an (Paket geladen, Plug-in-Fehler, „Core interface … will not be
  registered");
- die `.dlu` trägt eine Dateifassung (Explorer → Eigenschaften → Details);
- `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`: ohne ihn stürzt `std::mutex` ab,
  wenn Max eine ältere `msvcp140.dll` lädt als die, gegen die gebaut wurde
  (CODING.md 15) — das Fenster sperrt beim Start des Arbeitsthreads.

### 0.33.2 — auch der neue Name war nicht erreichbar

Mit 0.33.1 ging im Menü der Rückweg auf (Dateiauswahl für `layout.toc`). Das
heißt: die `.dlu` war geladen (die Importer-Klasse war da), aber `Swbf2Cpp`
aus MAXScript weiterhin nicht erreichbar — der Namensverdacht aus 0.33.1 ist
damit **widerlegt**. 0.33.2:

- das Menü sucht die Schnittstelle zusätzlich über `getCoreInterfaces()` —
  das hängt nicht daran, dass MAXScript einen globalen Namen angelegt hat;
- `LibInitialize` (neu exportiert) misst beim Start, ob das Kerninterface
  angemeldet ist, holt die Anmeldung notfalls mit `RegisterCOREInterface`
  nach und schreibt beides nach `%LOCALAPPDATA%\SWBF2Import\start.log`;
- die Diagnose des Menüs geht nach `Downloads\EAfront Diagnose.log` statt ins
  Import-Protokoll — das Plugin hat sie in 0.33.1 beim nächsten Import
  überschrieben;
- START.bat hängt `start.log` und `EAfront Diagnose.log` ans START.log.

**Gegenprobe.** `START.bat` hält `%LOCALAPPDATA%\SWBF2Import\figuren\vur_anakin_01_bpb.fbmodel`
byteweise gegen `figur_direkt.fbmodel` aus castool (`tools/VERGLEICHE_FENSTER.py`),
sobald Anakin einmal über das Fenster importiert wurde.

**Ohne Max prüfen.** `tools/PRUEFE_FENSTER.sh` baut das Fenster mit MinGW gegen
echte Windows-Header, samt der ganzen Containerschicht, als
`fenster_probe.exe <figuren.txt> [suche] [hell]`. Das Bild oben ist dieser
Probelauf unter Wine, mit der echten Figurenliste aus DHs Lauf.

## Der Weg direkt ins Spiel (begonnen)

Ziel ist, dass das Plugin die Spieldateien **selbst** liest und niemand mehr
Dumps hin- und herschieben muss. Dafür muss alles nach C++, was fbtools heute in
Python kann. Erster Baustein steht:

`src/fbcas.h/.cpp` — Entschleierung, `cas.cat` und die Blockentpackung.

- **Entschleierung:** beginnt eine Datei mit `0x01CED100` oder `0x03CED100`,
  steht der Inhalt erst ab `0x22C`. Gilt für `layout.toc`, `initfs_win32` und
  `cas.cat` gleichermaßen.
- **`cas.cat`:** die Zuordnung SHA-1 → Archiv, Versatz, Länge, dazu die
  Patchliste.
- **Blöcke:** roh, zlib, lz4, zstd und zstd mit Wörterbuch, dazu der Deltapfad
  für Patches. Oodle-Blöcke werden benannt, nicht stillschweigend übergangen —
  bei SWBF2 kommen sie nicht vor.

Die drei fremden Entpacker liegen unter `vendor/`, alle nur zum **Entpacken**
und alle mit freizügiger Lizenz; Herkunft und Erzeugung stehen in
`vendor/HERKUNFT.md`.

**Geprüft wird gegen ein Mini-Spiel.** fbtools baut mit
`tests/synth_test.py --keep <ordner>` eine vollständige kleine Installation:
alle fünf Entpackwege, ein Delta-Patch, ein Manifest, ein Bundle.
`START.bat` lässt das automatisch laufen und vergleicht Katalog und Nutzdaten
mit fbtools:

    Katalog: zeichengleich, 10 Zeilen
      Eintrag  0:    3000 Byte, byteweise gleich
      Eintrag  1:   70000 Byte, byteweise gleich   (zstd, zwei Blöcke)
      Eintrag  2:  150000 Byte, byteweise gleich   (lz4)
      Eintrag  3:   40000 Byte, byteweise gleich
      Patch   0:   37768 Byte, byteweise gleich    (Deltapfad)

Die Prüfskripte hängen ihre Ausgabe seit 0.27.1 **selbst** ans Protokoll — über
die Umgebungsvariable `SWBF2_LOG`. Der frühere Umweg über eine Zwischendatei in
der Batchdatei ist beim zweiten Aufruf an einer Dateisperre gescheitert, und mit
ihm sind die Ergebnisse zweier Proben verschwunden.

`START.log` enthält alles, was zum Nachsehen gebraucht wird: die CMake-Ausgabe,
die Ergebnisse beider Gegenproben und seit 0.24.1 auch das **Import-Protokoll**
aus 3ds Max, das an den Schluss angehängt wird. **Eine Datei genügt.**

`src/fbdb.h/.cpp` — **DbObject**, das Baumformat von Frostbite. Darin stehen
`layout.toc`, `initfs_win32` und die Chunkangaben des Manifests. Ein Byte Typ
(Bit 7 heißt „ohne Namen"), Listen und Objekte tragen ihre Länge als LEB128
davor und werden bis genau dorthin gelesen — nicht bis zum ersten Nullbyte.

Auch das wird gegen fbtools gehalten, Zeile für Zeile:

    layout.toc       zeichengleich, 18 Zeilen
    initfs_win32     zeichengleich,  9 Zeilen

`src/fbgame.h/.cpp` — **das Spiel als Ganzes.** Hier laufen die Bausteine
zusammen: Pfade auflösen (`native_data/` sucht ab dem Patch, `native_patch/` nur
im Patch), `layout.toc` mit Superbundles und Katalogen, das `initfs` mit dem
zstd-Wörterbuch, alle `cas.cat` und das Manifest. Danach kann die Schicht das
Entscheidende:

    SHA-1  ->  entpackte Nutzdaten

einschließlich Deltapfad, ohne dass der Aufrufer wissen muss, in welcher
`cas`-Datei etwas liegt.

    Spielsicht        zeichengleich, 11 Zeilen
    ueber SHA-1 dc4fc33a:     3000 Byte, byteweise gleich
    ueber SHA-1 76d1c6e9:    70000 Byte, byteweise gleich
    ueber SHA-1 697f5e61:   150000 Byte, byteweise gleich
    ueber SHA-1 57507ed0:    40000 Byte, byteweise gleich
    ueber SHA-1 37d276e4:    37768 Byte, byteweise gleich   (Deltapfad)

`src/fbbundle.h/.cpp` — **das Bundle.** Die Liste dessen, was zusammengehört:
EBX (Objekte mit Namen), RES (Rohdaten wie MeshSets und Texturen) und Chunks.
Zu jedem Eintrag steht die SHA-1 dabei — damit ist der Weg geschlossen:

    Name -> Bundle -> SHA-1 -> cas.cat -> entpackte Daten

Zwei Fallen dabei, beide von der Gegenprobe gefunden: der Bundleblob liegt
**roh** in der `cas`-Datei und darf *nicht* durch den Blockentpacker; und der
Rumpf beginnt bei **0x24**, nicht bei 0x20 — vier Byte Größe plus ein Kopf von
0x20, während die Versätze im Kopf ab dem Kopfanfang zählen.

    Bundles           zeichengleich, 6 Zeilen

`src/fbindex.h/.cpp` — **der Index.** Läuft über alle Bundles und baut daraus die
Namenslisten für EBX, RES und Chunks. Damit ist die Kette vollständig: aus einem
Namen wird ein SHA-1, aus dem SHA-1 werden Nutzdaten.

Bundle-Namen sind nicht gespeichert, nur ihr Hash. Sie werden in dieser
Reihenfolge bestimmt: aus Frostys Liste der 25 bekannten geteilten Bundles, sonst
aus dem EBX-Namen *im* Bundle, dessen Hash den Bundle-Hash trifft (erst so
geschrieben, dann klein), sonst der Hash als Hexzahl.

Zwei Fallen, wieder von der Gegenprobe gefunden:

- **Der Hash ist kein FNV-1.** Trotz des Namens `Fnv1` in Frosty ist es
  `h = 5381; h = (h * 33) ^ c` — mit dem echten FNV-1 (2166136261 / 16777619)
  trifft kein einziger Bundle-Hash.
- **GUIDs in großendigen Bundles sind gedreht.** `ReadGuid(Endian.Big)` dreht
  Data1 (4 Byte), Data2 und Data3 (je 2 Byte) um; der Rest bleibt. Im Manifest
  wird dagegen nicht gedreht.

    Index             zeichengleich, 8 Zeilen

Die Gegenprobe steht damit bei **15 von 15**.

## Erste Probe am echten Spiel

Bisher wurde der C++-Unterbau nur gegen das Mini-Spiel gehalten. Findet
`START.bat` die Installation von Battlefront II, baut sie jetzt den **Index über
das echte Spiel** — 4.777 Bundles — und schreibt die vollständige Figurenliste
nach `figuren.txt`. Im Fenster und im Log stehen die Kopfzahlen und die ersten
Heldenbundles.

**Gemessen an der echten Installation, C++ gegen fbtools:**

| | fbtools | C++ |
|---|---|---|
| Bundles | 4.777 | **4.777** |
| EBX | 155.117 | **155.117** |
| RES | 84.130 | **84.130** |
| Chunks | 211.705 | **211.705** |
| Helden | 112 | **112** |

Ein Bundle zählt dabei nur dann als Figur, wenn darin ein MeshSet liegt, dessen
**Name mit `characters/` beginnt**. Ohne diese zweite Bedingung kommen 2.572
statt 1.858 heraus — dann zählen Fahrzeuge, Waffen und Kulisse mit, die
ebenfalls MeshSets haben.

## MeshSet in C++

`src/fbmeshset.h/.cpp` — die Geometrie. Das schwierigste Format im ganzen
Projekt, und die drei teuer erarbeiteten Punkte stecken alle darin:

- **Zwei `GeometryDeclarationDesc` je Section.** Welche zu den Daten gehört,
  steht nirgends. Beide werden probiert und die glaubwürdigere genommen —
  gemessen daran, ob die Positionen endlich sind, die Bone-Gewichte auf 1
  summieren und die UV im üblichen Bereich liegen.
- **Vertexelemente über ihren Offset lesen**, nicht über die Summe der Größen.
  Die Elemente folgen nicht immer lückenlos aufeinander.
- **Alle LODs können in einem Puffer liegen**, jeder Block auf 16 Byte
  aufgerundet. Ohne diesen Versatz laufen die Positionen bis 1e37.

Dazu der zweite Satz Bone-Einflüsse (`BoneIndices2`/`BoneWeights2`) bei Sections
mit `bonesPerVertex 8` — nur den ersten zu lesen ergibt Gewichtssummen bis
hinunter zu 0,667.

**Geprüft an echten Spieldaten.** `VERGLEICHE_MESH.py` sucht die gesicherten
MeshSets selbst, egal wo im fbtools-Ordner sie nach dem Entpacken gelandet sind,
und meldet ins Protokoll, wenn es keine findet. Entscheidend ist dabei der
**`resType` in der `.meta.json`**: `49B156D4` ist ein MeshSet. fbtools sichert
nämlich auch Animationsbänke als `.res` mit derselben Art Metadatei daneben —
ohne diese Prüfung landen die im MeshSet-Leser und werden reihenweise als
„lodCount 288" abgelehnt.

    anakin_01_flaps_mesh     Kopf zeichengleich, 139 Zeilen
                             LOD 5: Vertices zeichengleich
    anakin_01_mesh           Kopf zeichengleich, 445 Zeilen
                             LOD 0: Vertices zeichengleich
    anakin_01_sleeves_mesh   Kopf zeichengleich, 139 Zeilen
    heads_anakin_01_mesh     Kopf zeichengleich, 450 Zeilen

    9 gleich, 0 abweichend

Eine Falle dabei: die dekodierten Werte werden in **doppelter** Genauigkeit
gerechnet, nicht in einfacher. 131/255 gibt in double 0,513725, in float
0,513726 — verengt wird erst beim Schreiben.

## EBX in C++

`src/fbebx.h/.cpp` — Objekte mit Namen. Daraus kommen das Skelett und die
Materialzuordnung.

Der entscheidende Punkt: eine EBX-Datei bringt ihre **Typbeschreibung selbst
mit**. Klassen- und Feldnamen stehen als Zeichenketten im Kopf und werden über
ihren Hash wiedergefunden — die SDK-Typtabelle wird also nicht gebraucht.

Zwei Fallen, die schon in der Python-Fassung Zeit gekostet haben und hier
gleich richtig stehen:

- Die **Art** eines Feldes steht in Bit 4 bis 8: `(t >> 4) & 0x1F`. Die unteren
  Bits sind die Kategorie.
- Der Namenshash ist **FNV-1 mit Startwert 5381** und `h = (h * 33) ^ c` —
  dieselbe Rechnung wie bei den Bundle-Namen, nur vorzeichenbehaftet gelesen.

Geprüft am echten Skelett `Walrus_HumanMale`:

    walrus_humanmale.ebx    zeichengleich, 13788 Zeilen

## GD-Bänke in C++

`src/fbgd.h/.cpp` — EAs GenericData. Darin stecken die Animationen: je Bank eine
Typbeschreibung (REF2) und dahinter je Eintrag ein Datenblock (DAT2).

Die Zahlen sind an echten Bänken nachgemessen, nicht geraten: Kopf 36 Byte und
**großendig**; `GD.STRM` umspannt alles Folgende, die übrigen Blöcke liegen
**darin** ab dem 16-Byte-Kopf; Klassenkopf und Feldeintrag je 32 Byte, Feldzahl
= `(stringTableOffset − 32) / 32`, die nächste Klasse bei
`stringTableOffset + stringTableLength` auf acht aufgerundet; bei DAT2 steht der
Klassenhash bei `p+28` und der Datensatz beginnt bei `p+44`.

Und die Falle, die in der Python-Fassung viel Zeit gekostet hat: **die
Offsetliste vor den Klassen taugt nichts.** Sie zählt in Schritten von 32,
während die Klassen 40 und mehr auseinanderliegen — gelesen wird der Reihe nach.

Geprüft an zwölf echten Bänken:

    12 gleich, 0 abweichend

## Das Skelett aus dem EBX

`fbebx::LiesSkelett` zieht aus einem `SkeletonAsset` heraus, was der Import
braucht: Namen, Hierarchie und die drei Lagesätze (`LocalPose`, `ModelPose`,
`InverseModelPose`). Ein Vec3 ist dabei ein Struct mit `x`, `y`, `z` und einem
Füllwert; eine `LinearTransform` besteht aus `right`, `up`, `forward`, `trans`.

    walrus_humanmale.ebx    zeichengleich, 13788 Zeilen
                            Skelett zeichengleich, 249 Zeilen

**Damit ist der ganze Weg in C++ vorhanden:** vom `cas.cat` über Bundles und
Index bis zu MeshSet, Skelett und den Animationsbänken.

## Der Zusammenbau

`src/fbfigur.h/.cpp` — hier laufen alle Schichten zusammen. Aus einem
Bundlenamen werden die MeshSets und das Skelett geholt und daraus ein Modell
gebaut: Bones mit Ruhelage, je Section die Vertexspalten, Dreiecke,
Materialnamen und die Bone-Liste. Dieselben Filter wie in fbtools — Tiefen- und
Schattengeometrie fallen weg, die glaubwürdigere der beiden Vertexdeklarationen
gewinnt.

Dazu der **Schreiber für `.fbmodel`**, Fassung 2. Geprüft wird er mit einer
Rundprobe: eine echte Datei einlesen und wieder ausgeben.

    vur_anakin_01_bpb.fbmodel     201464 Byte, byteweise gleich

201 KB, 12 Meshes, 248 Bones, 9 Materialien — **Byte für Byte dieselbe Datei**.
Ältere Formatfassungen werden erkannt und beim Vergleich übersprungen, weil der
Schreiber sie auf Fassung 2 hebt.

**Nicht jedes MeshSet hat einen Chunk.** Bei manchen ist die `chunkId` lauter
Nullen — ihr Vertex- und Indexpuffer steht dann **in der `.res` selbst**, und wo
genau, sagen die ersten acht Byte der `resMeta` aus dem Bundle: Versatz und
Länge. Ohne das fehlen bei Anakin Schöße, Rock und Ärmel.

**Der Katalog gehört zum Eintrag.** Eine SHA-1 allein sagt noch nicht, in
welcher `cas`-Datei sie liegt: `cas_01.cas` gibt es in fast jedem der 23
Kataloge. Wer den ersten nimmt, der zufällig passt, liest die falsche Datei und
bekommt „Block: Nutzdaten reichen nicht". Zu jedem Eintrag wird deshalb gemerkt,
aus welchem Katalog er stammt.

Eine weitere Falle steckte im Kleinsten: die Stringtabelle beginnt mit **einem
Nullbyte**, und die leere Zeichenkette liegt fest auf Versatz 0. Ohne das ist
die Datei gleich groß, aber ab dem 65. Byte verschoben.

## Der ganze Weg am Stück

Findet `START.bat` die Installation, zieht sie in **einem** Lauf beides heraus:
die Figurenliste **und** Anakin komplett — vom `cas.cat` über Index, Bundle,
MeshSet und Skelett bis zur fertigen `figur_direkt.fbmodel`. Der Index über die
4.777 Bundles wird dabei nur einmal gebaut.

Danach hält `VERGLEICHE.py` diese Datei **byteweise** gegen die, die fbtools aus
denselben Spieldateien geschrieben hat:

    Der ganze Weg am Stueck (Spieldateien -> .fbmodel):
      201464 Byte, BYTEWEISE GLEICH mit vur_anakin_01_bpb.fbmodel

Die Probe läuft **nach** der Extraktion, nicht davor — `VERGLEICHE.py` arbeitet
in Schritt 2, die Figur entsteht erst in Schritt 3. Sie hat ihr eigenes Skript,
`VERGLEICHE_FIGUR.py`, und meldet bei einer Abweichung die Stelle und die
Kennzahlen beider Köpfe, damit die Ursache eingrenzbar ist.

Das ist die schärfste Probe im ganzen Projekt: sie deckt **jede** Schicht ab —
Entschleierung, Katalog, alle fünf Entpackwege, DbObject, Manifest, Bundle,
Index, MeshSet mit beiden Deklarationen und dem LOD-Versatz, EBX-Skelett, und
den Schreiber. Weicht irgendwo ein einziges Byte ab, sagt sie es und nennt die
Stelle.

## Der Index wird abgelegt

Den Index über 4.777 Bundles zu bauen dauert — für ein Plugin, das bei jedem
Import läuft, zu lange. `--cache <datei>` legt ihn deshalb ab und lädt ihn beim
nächsten Mal:

    INDEX neu gebaut (kein abgelegter Index)
    INDEX aus der Ablage

Die **Kopfnummer** des Spiels steht mit in der Datei. Nach einem Patch ändert
sie sich, der alte Index wird verworfen und neu gebaut — statt still falsche
Versätze zu liefern. Geprüft: gebauter und geladener Index liefern Zeile für
Zeile dieselbe Ausgabe.

**Was noch fehlt:** derselbe Weg im Plugin selbst, ohne Umweg über eine Datei —
dann bekommt die Auswahlliste ihren Sinn. Und die Animationen: dafür fehlen noch
die Werte aus den DAT2-Datensätzen und die vier Codecs.

## Vorabprüfung ohne Max-SDK

Unter `tools/sdkstub/` liegt eine Attrappe des Max-SDK: dieselben Typen und
Signaturen, aber leer. Damit übersetzt der Plugin-Quelltext auch ohne Windows
und ohne installiertes Max. Das fängt Tippfehler und falsche Signaturen, bevor
der richtige Bau daran scheitert — die beiden Fehler, die uns je eine Runde
gekostet haben, wären damit sofort aufgefallen.

## Stufe 1 — das Skelett

Aus einer `.fbmodel` werden die Bones angelegt, in Hierarchie gehängt und in
ihre Ruhelage gesetzt. Keine Keyframes: der Animationsmodus bleibt aus, damit
`SetNodeTM` wirklich die Ruhelage setzt und nicht heimlich Keys anlegt.

**Maßstab.** Der Dump steht in Metern. Was ein Meter in Max ist, hängt an der
Systemeinheit; `GetSystemUnitScale(UNITS_METERS)` (bis 2021 `GetMasterScale`,
ab 2023 gibt es nur noch das erste) liefert die Zahl der Meter je
Systemeinheit, also wird geteilt. Ohne das war Anakin **1,8 Einheiten**
groß, während Max Bones mit Breite **4** anlegt — jeder einzelne Knochen war
doppelt so breit wie die ganze Figur. Das war der Kastenhaufen im ersten
Versuch.

**Bonebreite.** Ein Viertel der *mittleren* Bonelänge, nicht ein Anteil der
Gesamtausdehnung: einzelne Bones sitzen meterweit vom Elternteil entfernt
(`Wep_Aim_Target_Rig` 3,4 m, `Camera3pDefPos_Rig` 2,5 m). Der Median liegt bei
8 cm und beschreibt das Rig richtig.

**Protokoll.** Jeder Import schreibt `Import swbf2.log` in den Downloads-Ordner:
Datei und Größe, Systemeinheit und Maßstab, Ausmaße des Rigs vor und nach der
Umrechnung, mittlere Bonelänge, gewählte Bonebreite, Zahl der angelegten Knoten
und wohin die Gegenprobe geschrieben wurde. Wird nach jeder Zeile geleert —
stürzt Max ab, steht trotzdem drin, wie weit es kam.

Weicht etwas ab, meldet die Probe seit 0.21.1 nicht nur den Spitzenwert,
sondern auch **wie viele** Bones betroffen sind und ob es die Drehung
(Zeilen 0 bis 2) oder die Lage (Zeile 3) trifft. Eine einzelne Abweichung hat
eine andere Ursache als 248 gleichartige.

**Reihenfolge beim Anlegen der Bones:** erst alle Knoten anlegen, dann
einhängen, **dann** die Weltlage setzen — Elternteil vor Kind. Andersherum
(Lage setzen, dann mit `keepTM` einhängen) hat Max bei `Wep2_Root` die Matrix um
0,009571 verändert. Nachgewiesen mit der `SETZ`-Zeile im Dump: gesetzt und
zurückgelesen waren verschieden, es war also Max und nicht die Rechnung.
`SetBoneAutoAlign(FALSE)` hat daran nichts geändert.

**Reihenfolge beim Prüfen:** `START.bat` vergleicht in Schritt 2, baut das Plugin
aber erst in Schritt 3 und installiert es in Schritt 4. Geprüft wird also immer
der Dump des **vorigen** Imports. Deshalb steht seit 0.22.1 die **Fassung** im
Kopf des Dumps, und die Probe sagt es ausdrücklich, wenn sie einen älteren
Dump vor sich hat. Ablauf: `START.bat`, dann Max neu starten und importieren,
dann `START.bat` noch einmal.

Der Bone-Dump trägt seit 0.19.3 den **Maßstab** im Kopf. Ohne den rechnet die
Gegenprobe die Sollwerte in Metern, während Max in Systemeinheiten steht — das
meldete eine Abweichung von 116,11 Einheiten, die keine war.

**Die Gegenprobe läuft automatisch.** Nach dem Import liegt neben der `.fbmodel`
eine Datei `..._max_bones.txt` mit der Weltlage jedes erzeugten Knotens. Der
nächste Lauf von `START.bat` rechnet dasselbe aus der `.fbmodel` nach und
vergleicht — mit dem richtigen Ergebnis liegt die größte Abweichung bei 5e-7,
ein eingebauter Fehler von 0,05 wird gefunden und benannt.

**Die Achsenumrechnung ist nachgeschlagen, nicht geraten.** 3ds Max ist
rechtshändig mit Z nach oben und Y in den Bildschirm; das Spiel hat Y oben. Die
Umrechnung ist damit eine Drehung um X: `(x, y, z) → (x, −z, y)`. Der
Ogre-Importer für Max macht es Zeile für Zeile genauso, und Ogre ist ebenfalls
Y-oben. Die Determinante bleibt +1, die Händigkeit also erhalten.

Was daran offen bleibt: ob das Spiel selbst rechts- oder linkshändig ist. Ist es
linkshändig, steht die Figur zwar aufrecht, aber seitenverkehrt — daran wird man
es sofort sehen. Und ob die Ruhelage zeilen- oder spaltenweise gemeint ist,
zeigt sich ebenso am Rig. Beides steht an genau einer Stelle
(`swbf2::AchsenMatrix`, `swbf2::RuheAlsMatrix`).

**Der alte Exporter-Trick gilt hier NICHT.** „y und z tauschen UND Zeile 1 mit Zeile 2
tauschen", wie man es in alten Exportern findet, ist eine Spiegelung
(Determinante −1) und war für linkshändige Ziele wie DirectX gedacht. Für zwei
rechtshändige Systeme ergibt das ein seitenverkehrtes Modell.

## Stufe 3: Skinning (0.34.0, gebaut ab 0.34.1)

0.34.0 brach in allen zwölf Jahrgängen ab: `IDerivedObject` und
`CreateDerivedObject` stehen in `modstack.h`, das nicht eingebunden war. Seit
0.34.1 kommt der Skin-Modifikator auf dem Weg, den das SDK dafür beschreibt:
`GetCOREInterface7()->AddModifier(*node, *mod)`. Die Attrappe bildet die
Aufteilung jetzt nach (CODING.md 22).

Je Mesh ein Skin-Modifikator, gefüllt über `ISkinImportData`: Modifikator
anlegen, nur die Bones anmelden, die das Mesh wirklich braucht, den Knoten
einmal auswerten (erst dabei legt Skin seine Daten an), dann die Gewichte —
bis **acht** Einflüsse je Vertex, wie in der Datei. Bones und Mesh stehen dabei
in der Ruhelage; die merkt sich Skin als Bindung. Danach wird **jedes Gewicht
zurückgelesen** (`ISkinContextData`) und gegen die Datei gehalten; die größte
Abweichung steht je Mesh im Protokoll.

## Stufe 4 vorbereitet: die Materialmessung (0.34.0)

`castool --material <spiel> <bundle>` druckt, was für Texturen gebraucht wird,
entlang Frostys eigener Kette (`FrostyMeshSetEditor.cs`):
`MeshAsset.Materials[section.MaterialId]` → `Shader.TextureParameters` →
Parametername und Verweis auf die Textur-EBX; sind die leer, kommen sie aus der
MeshVariationDatabase. Dazu je Textur der Kopf nach Frostys `Texture.cs`
(SWBF2-Zweig, 132 Byte) mit Gegenprobe: Summe der Mip-Größen gegen die
Chunkgröße, Mip 0 gegen Breite × Höhe je Format. START.bat misst Anakin und
schreibt alles ins START.log — daraus entsteht der Texturimport.

## Stufe 4: Texturen und Materialien (0.35.0)

**Zuordnung** (`src/fbmaterial`): Section → `MeshAsset.Materials[MaterialId]` →
MeshMaterial-Instanz → deren Eintrag in der **MeshVariationDatabase** des
Bundles → `TextureParameters` (Name + Textur). Abgeglichen über die
Instanz-GUID, sonst über die Reihenfolge (so macht es Frosty). Gemessen an
Anakin: die Materialien der MeshAssets haben leere Texturlisten, alle
Texturen stehen in der Variationsdatenbank.

**Texturen** (`src/fbtextur`): Kopf nach Frostys `Texture.cs` (132 Byte, 21/21
stimmig), Chunk holen (enthält **alle** Mips — gemessen), Mip 0 mit **bcdec**
dekodieren (BC1, BC5, BC7; MIT/Unlicense, `vendor/bcdec`), als PNG schreiben
(miniz). Normalen: R und G aus der Textur, B = √(1 − x² − y²). Gemessen hier:
2048² BC7 in 59 ms dekodiert, PNG in rund 70 ms. Andere Formatnummern werden
gemeldet, nicht geraten.

**Ablage:** `%LOCALAPPDATA%\SWBF2Import\texturen\*.png` (beim zweiten Mal nur
noch gelesen) und der **Beipackzettel** `<figur>.fbmodel.material.txt` neben der
`.fbmodel` — die `.fbmodel` selbst bleibt byteweise gleich mit fbtools.

**In Max:** je Material ein Standardmaterial (in allen zwölf Jahrgängen mit
dokumentierter C++-Schnittstelle): Farbe → Diffuse, Normale → Normal Bump im
Bump-Kanal (Bitmap mit Gamma 1,0), Anzeige im Viewport an. Danach stehen die
Materialien im **Compact Material Editor** in den ersten Slots
(`PutMtlToMtlEditor`, Modus über `Interface13::SetMtlDlgMode`); ist der
Slate-Editor offen, wird nicht umgeschaltet.

**0.35.1 nach DHs erstem Texturlauf:** der Kopf nannte seine Normale „NS"
(R und G um 127 wie jede Normale, B mit eigenem Signal) — jetzt als Normale
erkannt. Haar-Farbtexturen mit Endung `_ca`/`_cm` tragen im Alpha die
Deckkraft: eigenes Graubild `<name>_a.png` in den Opacity-Kanal, Material
beidseitig. Das ist eine Namensregel; im Log steht dazu, wie zweigipflig das
Alpha ist.

**Gegenprobe ohne Max:** `castool --texturen <spiel> <bundle> <ordner>` —
START.bat legt Anakins Texturen nach `texturen_anakin` und schreibt je Textur
Format, Größe, Kanal-Mittelwerte und bei Normalen den Anteil, bei dem das
alte B schon Z war, ins START.log.

## Stufe 5a: die Werte der Animationsbänke (0.36.0)

`src/fbgdwerte` liest die Datensätze der GD-Bänke — portiert aus dem eigenen
`fb_gd.py` (lies_datensatz, lies_basis, _string_feld): Grundtypen, Keys,
Zeichenketten, Arrays (auch aus Unterstrukturen), der eingebettete Basis-Satz
über `__base`, alle Versätze relativ zu sich selbst. Die Gegenprobe benutzt
eine **exakte Vergleichsform** (Gleitzahlen als Bitmuster, jedes Array mit
FNV-1a-Prüfsumme über alle Bytes), die `VERGLEICHE_GD.py` aus fbtools genauso
erzeugt. Geprüft hier an einer selbst gebauten Testbank
(`tools/tests/gd_testbank.py`): Aufbau und Werte zeichengleich mit fbtools,
unter ASan/UBSan. `castool --clips` zählt alle Clips in allen Bänken.

**Bei DH (0.36.0):** 12 von 12 Bänken in Aufbau **und Werten** zeichengleich
mit fbtools (bis 4 657 Wertezeilen je Bank). Alle Bänke zusammen: **82 229
Clips in 307 Bänken** (646 AssetBanks, 33 ohne GD-Kopf): RAW 37 213, VBR
25 220, DCT 12 082, FRAME 7 693, CURV 21. Die 1 469 Clips aus fbtools waren
eine **Stichprobe** von `animstat` (gleichmäßig verteilte Auswahl), nicht die
Gesamtzahl. `castool --clipinfo` misst die Namenskette Clip → Rig → Slotnamen
(Selbstprüfung über die Anhänge `.q`/`.t`).

## Stufe 5b: RAW und FRAME entpacken (0.37.0)

**Namenskette am Spiel gemessen (0.36.1, Referenzbank outro_team1):**
Verzeichnis 138 029 Keys, 545 RigAssets; Clip 1: Rig in
`sharedbundleanimation_common` deckt 178 von 180 DofIds, Drehkanäle 109/109
enden auf `.q`, Vektoren 61/63 auf `.t`, Floats 8/8; Clip 2: 176/176,
107/107, 61/61, 8/8. Die Namen sind die Bonenamen des Skeletts (Spine.q,
LeftHand.q …).

`src/fbanim` baut das Clipverzeichnis über alle Bänke, wählt je Clip das Rig
mit der **größten** Deckung (mindestens 90 %), entpackt RAW und FRAME nach
`fb_anim.py` und schreibt `.fbanim` nach `fb_animdump` — für dieselben Kurven
**byteweise gleich** mit fbtools (hier gemessen). `VERGLEICHE_ANIM.py` hält
bei DH die Kurven jedes Clips gegen die `.fbanim` von fbtools.

**Bei DH (0.37.0):** 4 von 4 entpackbaren Referenzclips **gleich** mit fbtools
(Namen, Art, Keyzeiten, jeder Wert); 8 weitere sind DCT/VBR.

**Vor den Keys in Max (0.37.1):** `castool --pose` misst, welche Lesart der
Quaternion stimmt (x,y,z,w oder w,x,y,z; Matrixzeilen oder -spalten) — am
ersten Key gegen die Ruhelage des Skeletts — und ob die Verschiebungen im Raum
der Ruhelage stehen. Dazu die Bildrate aus EndFrame/Dauer. `castool --clips`
sucht lesbare Namen je Klasse und die ClipController, die auf Clips verweisen.

**Clipnamen:** 82 025 von 82 229 sind Kennzahlen (`0bab04b860e4c0cf.chan`),
nur 204 lesbar (Gesichtsposen). Lesbar sind die Banknamen.

## Animationsfenster (0.38.0)

Menü **EAfront Tool → SWBF2 Animationen** (oder `Swbf2Cpp.showAnimDialog()`):
beim ersten Öffnen liest ein Arbeitsthread alle Animationsbänke (~10 s), danach
bleibt das Verzeichnis für die Max-Sitzung. Die Liste zeigt die **lesbaren
Namen aus den ClipControllern** (79 261 von 79 261 lesbar, alle verweisen auf
einen Clip), dazu Codec, Bildzahl, Bildrate und Bank; Suche über Namen, Kennzahl
und Bank. **Laden** (oder Doppelklick) legt den Clip auf das Skelett der Szene
(Auswahl → oberster Vorfahr → alle Bones nach Namen) und **ersetzt** den vorigen:
neue Linear-Controller je Bone, Ruhelage für Bones ohne Kanal, Keys über
`SetValue(CTRL_ABSOLUTE)`, Bildrate und Bereich aus dem Clip. Ladbar sind RAW
und FRAME; DCT und VBR folgen. Regeln und Messungen: CODING.md, Abschnitt 20d.

**Bei DH (0.38.0):** `A_Anakin_AttackLoop_Strike4_V2` auf Anakin — „sieht
super aus": 248 Bones, 75 mit Keys (7 050 Keys), 30 fps, Bereich 0 bis 93.

**0.38.1 — nur die Animationen der geladenen Figur:** Der Import vermerkt die
Figur am obersten Bone (Benutzereigenschaft `swbf2_figur`, bleibt mit der Szene
gespeichert). Das Fenster liest daraus das Suchwort (`…/characters/hero/anakin/…`
→ `anakin`) und zeigt mit **„nur Figur"** nur Clips, deren Controller- oder
Bankname es trägt. Szenen aus älteren Fassungen: das häufigste Wort der
Meshnamen (ohne Skelett). Zusätzlich lehnt **Laden** jeden Clip ab, von dessen
Bones weniger als 80 % in der Szene stehen — ohne etwas zu verändern.

**0.39.0 — „Alle in die Zeitleiste"** (nach dem Sequenzmodus des
XFBIN-Importers 2.1.6): nimmt alle ladbaren Clips der aktuellen Auswahl (Figur,
Suche) in Namensreihenfolge und legt sie hintereinander: Bild 0 hält die
Bindepose, jeder Clip beginnt nach dem **Abstand** (Vorgabe 10 Bilder). Bones,
die ein Clip nicht bewegt, bekommen an beiden Enden Ruhe-Keys, damit kein Clip
in den nächsten hineinläuft. Mit **Notizspur** entsteht auf der Szenenwurzel
der Note Track `animations` mit zwei Keys je Clip (Anfang und Ende, beide mit
dem Namen) — dieselbe Form wie XFBIN und das Animation Merge Tool. Unpassende
Clips (unter 80 % der Bones) werden übersprungen und gezählt.

## Layer und Waffen (0.40.0)

**Layer** wie im XFBIN-Importer: nach dem Import stehen die Bones in
„anakin_01 Bones" und die Meshes in „anakin_01 Meshes" (Name aus dem
Figurenordner). Eine zweite Figur desselben Namens bekommt „ #2", „ #3" — ein
Layer gilt als frei, wenn beide leer sind. Gebaut per MAXScript
(`LayerManager`), die Knoten über ihre Handles.

**Waffen — gefunden, noch nicht importiert:** Helden-Ausrüstung liegt unter
`gameplay/equipment/heroes/<waffe>/` (49 Meshes). Lichtschwerter tragen den
Helden im Namen (`lightsaberanakin/lightsaberanakin_meshp_mesh`, dazu
`t_lightsaberanakin_cs` und `_nam`, in 187 Bundles geteilt); Blaster nicht
immer (`dl44`, `ee3`, `bowcaster`). `castool --waffe <spiel> anakin` misst je
Kandidat Meshtyp, Sections, Bones je Vertex und Bone-Liste — daran entscheidet
sich, ob die Waffe an `Wep_Root` gehängt oder geskinnt wird.

## Animationsfenster 2 (0.41.0)

* **Gestaltung wie das Figurenfenster** (gemeinsame Regeln in
  `src/swbf2import_ui.h`): alle Farben aus Max' Theme, Kontraste nach WCAG
  mindestens 4,5:1, eigene Zeichnung für Knöpfe, Liste, Auswahlfelder und
  Statuszeile, dunkle Titelleiste und dunkle Bildlaufleisten, Oberfläche auf
  Englisch wie das Figurenfenster.
* **Figur als Auswahlfeld** („Character"): „All characters" und jede Figur
  mit ladbaren Clips samt Anzahl; die Figur der Szene ist vorgewählt
  („anakin (126) - in scene").
* **Virtuelle Liste mit Spalten** (Name, Codec, Frames, FPS, Bank) — keine
  Anzeigegrenze mehr; Suche mit mehreren Wörtern (alle müssen passen).
* **Sequenzen durchklicken:** Das Auswahlfeld „Sequence" liest die Notizspur
  der Szenenwurzel; ein Eintrag stellt Max' Zeitbereich auf diesen Clip und
  den Zeitschieber an seinen Anfang, „Whole timeline" zeigt wieder alles.
* **Notizspur über die C++-API** (`NewDefaultNoteTrack`, `NoteKey`,
  `AddNoteTrack` auf der Szenenwurzel) statt MAXScript, danach zurückgelesen;
  das Protokoll nennt Spuren und Keys (Soll: zwei je Clip).
* **Clipverzeichnis in der Ablage** (`clips.fbclips`, an die Kopfnummer des
  Spiels gebunden): beim zweiten Öffnen statt ~10 s nur noch die Ladezeit der
  Datei. `castool --clipablage` vergleicht Ablage und Neubau Feld für Feld und
  entpackt Probeclips aus beiden.

## Waffen und verzerrte Köpfe (0.42.0)

**Waffen:** Das Figurenfenster nimmt die Waffen der Figur mit
(`fbfigur::FuegeWaffenHinzu`): MeshSets unter `gameplay/equipment/` mit dem
Figurennamen, ohne Frontend- und Egoperspektive-Fassung. Gemessen starr —
deshalb hängt jeder Vertex mit Gewicht 1 an `Wep_Root`, die Positionen kommen
mit dessen Ruhelage aus dem Waffenraum in den Modellraum. So folgt die Waffe
jeder Animation. Texturen aus dem Waffenordner (`_cs` Farbe, `_nam` Normalen),
eigener Layer „anakin_01 Weapons". In der Ruhelage liegt `Wep_Root` am
Boden unter der Figur (Weltlage 0,0 / -0,004 / 0,023 m, nachgerechnet aus
`figur_waffen.fbmodel`); in den Animationen liegt er in der Hand.

**Verzerrte Köpfe:** Ein bekanntes Frostbite-Problem — Köpfe sehen in der
Bindepose verzerrt aus und brauchen eine Gesichtspose. Laut id-daemon (ZenHAX,
Werkzeuge für SWBF2 und weitere Frostbite-Spiele) verweist die
VisualUnlock-Datei der Figur auf ein Asset in den Animationsbänken; manchmal
hängt der Verweis an Zähnen, Haaren oder Bart statt am Kopf.
`castool --gesicht` sucht alle Verweise aus den EBX des Figuren-Bundles in den
Keys aller Bänke und vermisst, was dort liegt (Klasse, Name, FACIAL-Kanäle,
Abstand zur Ruhelage). Danach wird die Pose beim Import eingebacken.

## Basispose, Bones, Sequenz-Attribute (0.43.0)

* **Gesicht:** Die Messung 0.42.0 fand in den ObjectBlueprints des
  Figuren-Bundles (`heads_anakin_01`, `hair_anakin_01`, `anakin_01`) das Feld
  `BasePoseTransforms` (Typ `SparseTransformArray`, laut SWBF2-Typ-SDK mit
  `Indices`, `Transforms`, `Count`). `fbfigur::LiesBasispose` liest es, misst,
  ob die Transforms lokal oder im Modellraum stehen (Median-Abstand zur
  Ruhelage), und schreibt je Bone die lokale Lage in `<fbmodel>.pose.txt`.
  Der Import legt sie **nach** dem Skin auf die Bones (der Skin bleibt in der
  Ruhelage gebunden) — das Gesicht nimmt seine Form an, auch ohne Animation.
  Das Animationsfenster nimmt dieselbe Pose als Ruhelage.
  **Gemessen (0.43.0 bei DH): bei Anakin sind die BasePoseTransforms LEER**
  (Kopf, Haare, Körper: 0 Einträge). Die Gesichtspose hängt also am
  VisualUnlock (`FacePoserLibrary`, `MorphDofSet` als AntRef). Frosty löst
  ANT-Assets per GUID auf: `__guid`, sonst der u64-`__key` als GUID
  (little-endian in den ersten acht Bytes). `castool --gesicht` prüft jetzt
  genau diese Lesart und gibt die Felder des Treffers aus.
  **Gefunden (0.43.1 bei DH):** AntRef `02cbffe4-3719-4888-…` → Bank-Key
  `e4ffcb0219378848` → `FacePoseLibraryAsset "Heads_Anakin_01"` (97 Posen,
  77 Dreh- und 79 Verschiebungs-DOFs) mit `BindPoseAsset`. **0.44.0**
  (`src/fbgesicht`): liest das BindPoseAsset (Index- und Wertelisten für
  Drehung und Verschiebung, nur abweichende Joints), baut daraus die lokale
  Lage je Bone und schreibt sie als `.pose.txt`; der Import legt sie nach dem
  Skin an. Gegenprobe in START.bat gegen den ersten Key von
  `UI_FrontEnd_Anakin_MainFace_01` — der repariert das Gesicht bei DH.
  **0.44.0 bei DH:** Das BindPoseAsset heißt „ToolBindPoseAsset"; seine
  Indexlisten (77 Drehungen, 79 Verschiebungen) zeigen auf die Gesichtsgelenke
  (größte Indizes 60/71), die Werte stehen unter anderen Namen („je 0 Werte")
  — nichts angewendet, Gesicht blieb verzerrt. **0.44.1:** misst alle Felder
  von BindPose- und FacePoseJointDofs-Asset und nimmt bis dahin den ersten Key
  des MainFace-Clips der Figur (nur `FACIAL_`-Bones) — der repariert das
  Gesicht bei DH nachweislich.
* **Bones** mit Breite und Höhe 0 (Wunsch DH).
* **Sequenz-Attribute wie WhiteoutDex:** „Load all to timeline" legt neben
  der Notizspur die Custom Attributes `NeoDexSequenceData` auf die
  Szenenwurzel (seqNames, startFrames, endFrames, nonLooping, rarity,
  moveSpeed, seqExtents, sharedGroup — dieselbe Definition wie
  `WhiteoutDexGlobals.ms`; ist WhiteoutDex geladen, dessen Definition).
  Zurückgelesen per MAXScript-Statusdatei, im Protokoll.
* **Lichtschwertklinge:** kein Teil des Griff-Meshes; im Spiel ein Effekt
  (`fx/weapons/lightsabers/emitters/em_lightsaber_anakin_meshp_3p`, dazu
  Klingen-Meshes unter `fx/meshes/weapon/`). START.bat gibt den Emitter als
  Text aus (Länge, Farbe), bevor eine Klinge nachgebaut wird.

## Clipzahlen im Animationsfenster (0.44.2)

Seit 0.44.0 steht jeder Clip nur einmal in der Liste — derselbe Clip (gleicher
Key) liegt oft in vielen Bänken. Anakin: 126 ladbare Clips, genau die frühere
castool-Messung (RAW 122 + FRAME 4); vorher standen sie bis zu zehnmal da.
Die Bankspalte zeigt jetzt „(+N)" für die weiteren Bänke. Figuren werden
zusätzlich unter ihrer Kurzform gefunden (Ordner `darthvader` → Clips mit
`Vader`, ebenso Dooku, Grievous, Phasma, Maul); Kurzformen und kurze
Schlüssel (rey, luke) nur am Wortanfang.

## PBR-Materialien (0.45.0)

* **Klassen:** Max 2016 Standardmaterial wie bisher; 2017–2025 Physical
  Material; ab 2026 OpenPBR_Material (Standardmaterial seit 2026, in der
  MAXScript-Hilfe ab 2025.3). Gebaut per MAXScript, Rückmeldung über
  `%LOCALAPPDATA%\SWBF2Import\materialien.txt`, bei Fehler Rückfall auf
  Standardmaterial.
* **Kanalbelegung, gemessen an Anakin:** `_cs` Farbe + Alpha = Glanz
  (Körper A-Mittel 55), `_ns` Normale + B = Glanz (Kopf 101, Haare 37/74),
  `_nm` Normale + B = Metall (Stoff 3,9). Glanz und Metall werden als eigene
  Graustufen-PNGs (`_gloss.png`, `_metal.png`) geschrieben, bevor die
  Normale ihr Z ins B bekommt.
* **Belegung im Material:** Farbe → Base Color (sRGB); Glanz → Physical
  `roughness_map` mit `roughness_inv`, OpenPBR `specular_roughness_map` mit
  invertiertem Ausgang; Metall → metalness; Normale über Normal_Bump
  (Gamma 1,0); Deckkraft → Physical `cutout_map`, OpenPBR
  `geometry_opacity_map` + `geometry_thin_walled` (Haarkarten).
* **Augen und Zähne (0.46.0):** Ihr MVDB-Eintrag hat keine
  Texturparameter; der Shader nimmt eigene. Die MVDB nennt den Shader
  (`SurfaceShaderGuid`): Augen `…/eyes/eyes_mp/ss_character_eye_mp_grey`,
  Zähne `shaders/presets/ss_characterspreset_teeth`. Texturen kommen aus dem
  Ordner des Shaders (bei gleicher Endung die Fassung mit seinem Zusatz,
  hier `_grey`), sonst aus `characters/heads/_shared/` nach dem Wort des
  Materials (eyes → eye, teeth). Im Beipackzettel als Weg „shaderordner"
  bzw. „materialname".
* **Fester Maßstab (0.47.0, Wunsch DH):** 1 m = 39,37 Einheiten in JEDER
  Szene, egal ob Systemeinheit Zoll oder Meter — Autodesks
  Standard-Systemeinheit ist 1 Zoll, in einer Zoll-Szene ist das also
  zugleich die echte Größe. Anpassbar in
  `%LOCALAPPDATA%\SWBF2Import\einstellungen.ini`, `[Import]
  EinheitenJeMeter=100` (wie Zentimeter) bzw. `0` (wieder maßstabsgetreu zur
  Systemeinheit).
* **Einheiten (0.46.0):** Der Importbericht nannte die Systemeinheit. In Max 2025 bei
  DH erschien Anakin winzig — maßstabsgetreu wäre das bei Meter als
  Systemeinheit (1,8 Einheiten neben dem 10er-Raster).
* **Früher offen:** Augen und Zähne hatten im Beipackzettel keine Texturen
  (MVDB leer); ihre Texturen liegen im Bundle (`t_eye_mp_da_grey`,
  `t_eye_mp_n`, Zähne `t_mp_teeth_*`) und hängen am Shader.

## Mehrere Rigs, Anhängen, Skelett je Figur (0.48.0)

* **Rigname:** Jeder Bone einer importierten Figur trägt `swbf2_rig` —
  derselbe freie Name wie ihre Layer („anakin_01", „darthvader_01",
  „anakin_01 #2"). Der oberste Bone behält `swbf2_figur` und `swbf2_pose`.
* **Animationsfenster:** Auswahlfeld „Rig" neben „Sequence": welches Rig in
  der Szene die Clips bekommt. Die Liste wird beim Aufklappen neu gelesen;
  beim Wechsel wird die Figur des Rigs vorgewählt (bleibt frei — Vaders
  Clips auf Anakins Rig gehen weiter). Alte Szenen ohne Rignamen: wie
  bisher (Auswahl bzw. erster Treffer nach Namen).
* **„Load all" hängt an:** Stehen schon Sequenzen in der Notizspur, kommen
  die neuen hinter die letzte (plus Abstand). Controller mit Keys bleiben,
  Bones ohne Keys bekommen Linear-Controller und die Bindepose bei 0.
  Notizspur bekommt die neuen Keys dazu, die Custom Attributes
  `NeoDexSequenceData` werden mit alten + neuen Sequenzen neu geschrieben.
* **Skelett je Figur (gemessen im Index):** unter `characters/rigs/humanoids`
  gibt es nur `walrus_humanmale` — Anakin, Vader, Chewbacca und fast alle
  Helden teilen es. Eigene Skelette (`<ordner>/<name>_ske`) haben b2, bossk,
  dio, ewok, generalgrievous und yoda; das Figurenfenster nimmt sie jetzt
  (erst Figurenordner, dann Heldenordner: grievous_03 → generalgrievous_01_ske).
  Probe gegen DHs Index: Anakin/Vader/Chewbacca → walrus_humanmale, Yoda →
  yoda_01_ske, Grievous 03 → generalgrievous_01_ske, Bossk → bossk_01_ske.
  Offen: Ruhelagen im Animationsfenster kommen noch aus walrus_humanmale.

## Standardklassen und Fahrzeuge (0.49.0)

* **Standardfiguren sind schon da:** Das Figurenfenster führt sie unter
  `characters/light|dark/<l|d>_<klasse>_<ära>/` — gezählt in DHs Liste z. B.
  143 Klon-Assault-, 56 Rebellen-Assault-, 14 Imperium-Assault-Fassungen.
  0.49.0 zeigt in Zeile 2 den Klartext: Empire / Rebels / First Order /
  Resistance / Separatists (droids) / Republic (clones) · Assault / Heavy /
  Officer / Specialist.
* **Messung Clips:** START.bat zählt Clips für trooper, stormtrooper, clone,
  droid, rebel, soldier, infantry, rifle, assault, heavy, officer,
  specialist, walrus, humanmale — samt Codec-Aufteilung (Verdacht: viele
  Standardanimationen sind VBR/DCT und damit noch nicht ladbar).
* **Fahrzeuge (offline im Index gemessen):** 99 Fahrzeugordner unter
  `gameplay/vehicles/` (air 39, ground 21, stationary 14, pilots 12,
  capital 11, corvette 1, spacebattles 1); 8 mit Skelett: AT-AT, AT-ST,
  AT-TE, AT-RT, Droideka, Dwarf Spider Droid, Homing Spider Droid, SPHA-T.
  Fahrzeuge haben VisualUnlock-Bundles wie Figuren
  (`win32/gameplay/vehicles/air/xwing_t65/vur_air_xwing_t65_bpb`).
  `castool --fahrzeuge` zählt je Ordner MeshSets, Skelett und Clips nach Namen.

## Codec-Labor: ANIMLAB.bat (0.50.0)

Eigene, schnelle .bat für die Arbeit an den noch nicht lesbaren
Animationsformaten. Baut nur `castool` (inkrementell), lädt Index und
Clipverzeichnis aus den Ablagen und startet `castool --codeclab` — alle
Ansätze in einem Lauf, Ergebnis in `codeclab.log`, Hex in `codeclab_hex.txt`.

**Warum:** gemessen (START.log 0.49.0) sind die Standardklassen fast nur VBR:
rebel 1794 von 2054, droid 650 von 738, heavy 1008 von 1178, officer 1056
von 1264, rifle 2573 VBR + 719 DCT von 3734. Insgesamt VBR 31 %, DCT 15 %.

**Methodik (Recherche):** Entropie trennt Abschnitte und zeigt gepackte
Daten; bekannte Werte suchen (hier: derselbe Clip als RAW-Zwilling);
Differenzen ähnlicher Dateien; Hypothesen als Parser prüfen statt nur
beschreiben (FOSDEM 2021 „Reverse-Engineering of (binary) File-Formats",
ServiceNow Security Lab 2025 „The Role of Entropy"); VBR in der
Spielanimation = je Spur eigene Bitbreite mit Bereichsnormierung
(Frechette, Animation Compression Library); Festkomma mit Log2-Bereich
(Unreal Per-Track-Codec); DCT: Energie in den niedrigen Koeffizienten,
quantisiert.

**Ansätze:** A1 Feldkatalog je Codecklasse · A2 RAW-Zwillinge und
Zählerabgleich (QuaternionCount, NumKeys, Bits je Key) · A3 Entropie,
Nullbytes, Bitebenen, FrameBlockSizes gegen Data · A4 Bitstrom-Suche der
quantisierten RAW-Werte (Breite 2..16, LSB/MSB, Bereich global/je Spur,
Abstand 1/3/4, Zufallsschwelle) · A5 DCT-II-Korrelation gegen int8/int16 ·
A6 Differenzen gleich großer Clips · A7 Hexdateien · Referenzclips für eine
Gegenprobe mit einem fremden Exporter (nur dessen Ausgabe, kein Code).
Geprüft hier: Bitlesen LSB/MSB, Entropie, und die Bitstrom-Suche findet einen
eingepflanzten 11-Bit-Strom (Abstand 3) an der richtigen Stelle.

## Codec-Labor Runde 1 → 2 (0.51.0)

**Runde 1 bei DH (4,7 s):** eindeutige Clips VBR 11 720, DCT 4 016, RAW 5 515,
FRAME 730, CURV 7. **Keine Zwillinge** — keine Animation liegt unter demselben
Namen einmal RAW und einmal VBR/DCT; die Suche mit bekannten Werten braucht
also eine andere Quelle. Entropie VBR 6,95, DCT 7,15 Bit/Byte; DCT-Bitebenen
alle 0,41 (gepackter Bitstrom), VBR fallend 0,45 → 0,36 und 11,6 % Nullbytes
(Tabellen). Differenzen: kein gemeinsamer Kopf im Feld Data.

**Aus den Hexdumps abgelesen und an allen drei VBR-Beispielen exakt
nachgerechnet** (Nur-Float-Clips, Gesichts-/Sprachkurven):
`Data = ConstFloatCount Byte (Palettenindizes) + ConstChanMapSize Byte
(Lauflängen, abwechselnd animiert/konstant, beginnt animiert) + 4 Byte je
animierter Spur + FloatOffsetSize + Summe FrameBlockSizes`; jeder Block beginnt
mit `ff XX XX`; die Läufe ergeben genau FloatCount/ConstFloatCount (7/87, 8/86,
8/86). **DCT:** DeltaBaseX/Y/Z/W je Spur (Anzahl = NumQuats + NumVec3),
Drehungen mit W ≈ 16384 → vermutlich Festkomma 1,0 = 16384; Vektoren mit W = 0.

**Runde 2 (castool --codeclab):** B1 prüft die Aufteilung an ALLEN VBR-Clips
und probiert die unbekannten Faktoren für konstante/animierte Drehungen und
Vektoren durch · B2 Lauflängen-Summen gegen die Zähler · B3 Blockköpfe
(`ff`, Byte 2 = Byte 3) · B4 Halbbyte-Verteilung der 4-Byte-Deskriptoren ·
B5 DCT-DeltaBase als Festkomma (Betrag/16384) · B6 DCT-Größen (BitsPerSubblock,
Data, DataSize gegen Keys und Spuren) · Hexdatei jetzt auch mit den kleinsten
Clips mit Drehungen.

## Codec-Labor Runde 2 → 3 (0.52.0)

**Runde 2 bei DH:** VBR-Aufteilung stimmt bei **11 637 von 11 720** Clips
(99,3 %): Tabelle 4 Byte je konstanter Drehung, 3 je Vektor, 1 je Float;
Deskriptor **4 Byte je animierter Komponente** (16 je Drehung, 12 je Vektor,
4 je Float); dazu Map, FloatOffsetSize, VectorOffsetSize, KeyTimeSize (×1) und
die Blöcke. Alle 10 476 Nur-Float-Clips passen. Lauflängen-Summen stimmen bei
10 563 (Drehungs-/Vektor-Clips ordnen die Karte anders). Blöcke: 88,7 %
beginnen mit `ff`, bei 94,5 % sind Byte 2 und 3 gleich. **Deskriptor-Halbbytes
in der Folge „unteres zuerst" fallen streng ab** (Mittel etwa 9, 6, 4, 2,7, 2,
1,4, 1,1, 0,8) — das Bild von Bitbreiten für 8 DCT-Koeffizienten je Block.
DCT: DeltaBase-Betrag/16384 im Median 0,9991 (Festkomma bestätigt, 59,8 %
innerhalb ±1 %).

**Recherche dazu:** DCT-Codecs quantisieren Koeffizienten mit Gewichten je
Frequenz und einem Quantisierer, der Rest wird entropiekodiert; manche geben
dem DC-Koeffizienten eine feste Bitlänge; jeder Block wird ein Bitstring
eigener Länge (passt zu den verschieden großen Blöcken). Werkzeug: ImHex mit
eigener Pattern Language zum farbigen Aufschlüsseln.

**Runde 3:** B7 konstante Drehungen aus der Palette in zwei Anordnungen × drei
Abbildungen, bewertet an der Einheitslänge · B8 animierte Drehungen in Block 0,
24 Lesevarianten (Bitreihenfolge, Vorzeichen, Reihenfolge, Abbildung),
bewertet an der Konstanz von |q| über die Keys — skalenfrei, ohne
Vergleichsdaten; Konzeptprobe hier: richtige Variante 0,00013, nächste 0,014 ·
ImHex: die kleinsten Clips als `.bin` mit passender `.hexpat`.

## Codec-Labor Runde 3 → 4 (0.53.0)

**Runde 3 bei DH:** B7 (konstante Drehungen direkt als xyzw aus der Palette):
Einheitslänge nur bei 52,9 % (Min/Max) bzw. 43,9 % (2p−1) — planar schlechter.
B8: alle 24 Varianten gleich gut (0,0065–0,0085) — nicht aussagekräftig, die
kleinen Clips drehen kaum. Drei ImHex-Dateien (T_Sentry_…).

**An DHs drei .bin-Dateien abgelesen und nachgerechnet:** Die **Keyzeiten
stehen VOR der Lauflängen-Karte** (Tabelle → Keyzeiten → Karte → Deskriptoren
→ Offsets → Blöcke). Damit gehen bei allen drei die Karten auf (anim 3 /
konst 7, anim 3 / konst 7, anim 4 / konst 6), die Deskriptoren liegen auf
4-Byte-Grenzen, die Offsets lauten `01 00 01 00`, die Blöcke enden genau am
Dateiende. Keyzeiten sind Abstände in Bildern (z. B. 4 4 3 4 4 bzw.
6 6 6 6 6 6 1 6 6). Erklärt die 1 074 Abweichungen von B2 (dort Keyzeiten ≠ 0).

**Recherche:** „Kleinste drei" — die größte Komponente wird weggelassen, ihr
Index in 2 Bit gespeichert, der Rest im Bereich ±0,7071 (Gaffer on Games).
Zu Frostbites Animationskompression gibt es keinen öffentlichen Vortrag.

**Runde 4:** B2 mit berichtigter Reihenfolge · B7b konstante Drehungen als
„kleinste drei" (Indexbyte vorn/hinten, Bereich ±1/±0,7071) · B8 nur mit
wirklich bewegten Drehungen · ImHex-Muster in richtiger Reihenfolge.

## Codec-Labor Runde 4 → 5 (0.54.0) — ohne fremdes Programm

**Runde 4 bei DH:** Lauflängen-Karte mit Keyzeiten davor: **11 637 von 11 637**
stimmen — die VBR-Aufteilung ist damit vollständig bestätigt. „Kleinste drei"
für konstante Drehungen: nur 51,5 % gültig, Indexbyte 0..3 nur bei 62 % —
verworfen. B8: alle Varianten 0,0011–0,0105, nicht trennscharf.

**Frosty entfällt** (DH kennt die Bedienung nicht). Stattdessen zwei eigene
Vergleichsquellen:
* **Ruhelage des Skeletts (B9):** unbewegte Knochen (Finger, Gesicht, Waffen)
  sollten in einem Clip ihre Ruhelage behalten. Neu: `Quelle::Kanalnamen` —
  dieselbe Namenskette wie bei RAW, jetzt für jeden Codec. Geprüft werden zwei
  Anordnungen der Konstantentabelle (nach Art gruppiert / in DOF-Reihenfolge
  verschränkt) × zwei Abbildungen, gemessen an Einheitslänge UND Winkel zur
  Ruhelage (walrus_humanmale).
* **B8 neu:** alle animierten Komponenten in beiden Reihenfolgen, 96
  Lesevarianten, bewertet an der Schwankung von |q| im Verhältnis zur Bewegung
  der Komponenten (unbewegte Kanäle zählen nicht). Konzeptprobe hier:
  richtige Variante 0,0010, nächste 0,27.

## Codec-Labor Runde 5 → 6 (0.55.0)

**Runde 5 bei DH (151,5 s):** B9 — konstante Drehungen direkt gelesen liegen im
Median **~150 Grad** neben der Ruhelage, nur ~5 % unter 2 Grad; beide
Tabellenanordnungen gleich. Die Bedeutung der vier Tabellenbytes ist also
falsch angenommen (nicht nur die Reihenfolge). B8 neu: alle 96 Varianten
1,08–1,70 (richtig wäre ≈ 0,001) — das Lesen der Blöcke stimmt ebenfalls noch
nicht.

**Runde 6:** B10 lernt die Umrechnung aus den Daten: Für konstante Drehkanäle
mit bekannter Ruhelage wird jedes Tabellenbyte (sein Palettenwert) mit jeder
Ruhelage-Komponente x/y/z/w korreliert — auch mit dem Betrag, falls das
Vorzeichen woanders steckt — samt Geradengleichung. Die richtige Zuordnung
fällt heraus, wenn unbewegte Knochen ihre Ruhelage behalten (bei RAW-Clips
gemessen). Dazu ein Namens-Cache je ChannelToDofAsset gegen die lange Laufzeit.

## Codec-Labor Runde 6 → 7 (0.56.0) — die Tabelle stand verschoben

**Runde 6 bei DH (23,1 s dank Namens-Cache):** B10 fand **keinerlei**
Zusammenhang zwischen Tabellenbytes und Ruhelage (|r| < 0,13) — ein Zeichen,
dass die Bytes gar nicht den Knochen zugeordnet waren.

**Die Ursache, gefunden mit der Erst-Verwendungs-Probe:** Eine Palette entsteht
in der Reihenfolge, in der der Kodierer die Werte trifft; neue Indizes kommen
dann lückenlos 0, 1, 2, … Das gilt beim Dio-Clip und bei allen drei
Sentry-Clips **nur**, wenn die **Keyzeiten ganz vorn** stehen und die Tabelle
dahinter (5/5, 5/5, 7/7 und 92/92 lückenlos; ab Byte 0 dagegen 0/1, 0/2, 4/5).
Die Summe der Keyzeit-Abstände ergibt dann 31, 31, 11 und 20 Bilder. Die
Sentry-Tabelle lautet damit z. B. `0 1 0 2 | 0 0 0 3 | 0 0 0 3 | 0 0 0 3 | …` —
dreimal dieselbe Drehung (vermutlich Identität) und eine Drehung um eine
Achse. Beim Dio-Clip ergeben die ersten Palettenwerte als xyzw mit 2p−1
(0,0064; 0,6955; −0,0009; 0,7185) die Länge 1,0000. Alle bisherigen
Tabellenprüfungen lasen bei Clips mit Keyzeiten also verschoben — daher die
~53 % (das waren die Clips ohne Keyzeiten).

**Richtige Reihenfolge:** Keyzeiten → Konstantentabelle (4/3/1 Byte je
Drehung/Vektor/Float) → Lauflängen-Karte → Deskriptoren → Offsets → Blöcke.
**Runde 7:** B7/B7b/B9/B10 mit der Tabelle hinter den Keyzeiten; B7c prüft die
Erst-Verwendungs-Reihenfolge an allen Clips; ImHex-Muster berichtigt.

## VBR: konstante Kanäle gelöst (Runde 7 bei DH) → Runde 8 (0.57.0)

**Gemessen an allen Clips:**
* Erst-Verwendungs-Probe: Tabelle hinter den Keyzeiten lückenlos bei **933 von
  1 070** Clips mit Keyzeiten, ab Byte 0 bei **0**.
* Konstante Drehungen als xyzw mit `QuatMin + p·(QuatMax − QuatMin)`:
  **Einheitslänge bei 53 515 von 53 515 (100,0 %)**.
* Gegen die Ruhelage (walrus_humanmale): **Median 5,77 Grad, 42,7 % unter
  2 Grad** — gleiche Größenordnung wie bei RAW-Clips (dort Median 8,3 Grad,
  weil gehaltene Posen nicht die Ruhelage sind).

**Damit steht die VBR-Lesung der konstanten Kanäle:** Keyzeiten (Abstände in
Bildern) → Tabelle (4 Palettenindizes xyzw je Drehung, 3 je Vektor, 1 je
Float) → Karte → Deskriptoren → Offsets → Blöcke; Wert = Min + p·(Max − Min)
mit dem Bereich der jeweiligen Art (Quat/Vec3/Float).

**Offen: die Blöcke der animierten Kanäle.** Runde 8: C1 listet je Block Kopf,
Größe und die Summe der Bitbreiten (mit/ohne Vorzeichenbits); C2 liest die
Nur-Float-Clips in 72 Varianten (Bitreihenfolge, Vorzeichen, Leserichtung,
Kopf 0/1/3 Byte, DC absolut oder als Differenz zum vorigen Block) und bewertet
die Stetigkeit über die Blockgrenzen.

## Codec-Labor Runde 8 → 9 (0.58.0): Blöcke mit variabler Länge

**Runde 8 bei DH:** Blöcke umfassen 8 Keys (31 Keys → 4 Blöcke, 133 → 17).
Bei gleichen Deskriptoren schwankt ihre Größe stark und liegt oft **unter**
der Summe der Bitbreiten (z. B. 768 Bit bei Summe 881; 456–1904 Bit bei Summe
1362) — die Werte stehen also nicht mit fester Breite da, sondern mit
**variabler Länge**. Das Kopfbyte XX (`ff XX XX`) läuft gegen die Größe
(5d→1296, 70→1120, 81→1000, 8c→768 Bit). C2 (feste Breiten, 72 Varianten):
beste Stetigkeit 7,4 statt ≈ 1 — feste Breiten verworfen.

**Runde 9:** C3 misst den Zusammenhang Kopfbyte ↔ Blockgröße je Clip. C5 liest
die Blöcke mit Codes variabler Länge (Rice, Exp-Golomb, Kennbit + Wert, zum
Vergleich feste Breite; Halbbyte als Parameter; Bitreihenfolge, Breite 0
ausgelassen/mitgelesen, Leserichtung, Kopf 1–3 Byte — 144 Varianten) und
prüft, ob die Lesung in jedem Block genau im letzten Byte endet.
Konzeptprobe hier: Rice-gepackte Testblöcke gehen mit der richtigen Variante
40 von 40 auf, mit allen anderen 0 von 40.

## Codec-Labor Runde 9 → 10 (0.59.0): DCT dazu, VBR weiter

**Runde 9 bei DH:** Kopfbyte XX ↔ Blockgröße im Median r = −0,716 (323 von 334
Clips negativ) → Quantisierer je Block. Keine der 144 Lesarten variabler Länge
geht am Blockende auf (höchstens 1,3 %). Offline an drei Float-Clips: eine
einheitliche Verschiebung „Breite − s" je Block passt nur zufällig.

**Entscheidung DH: „zuerst alles knacken"** — DCT und VBR parallel.

**DCT, an zwei Hexdumps lückenlos abgelesen:** Das obere Halbbyte von
`DofTableDescBytes` je Kanal (0x30, 0x60, 0x70, 0x80 …) ist die **Anzahl der
UInt16-Einträge** dieses Kanals in `BitsPerSubblock` (3, 6, 7, 8 …) — der Reihe
nach, ohne Lücke (WalkFwdTwistEnd: Kanal 9 → 6 Einträge `5582 4470 2250 0040
0030 0020`, Kanal 10 → 8, Kanal 11 → 6 …; T_1p_Pistol_CrouchToStand ebenso).
Kanäle mit 0 sind konstant (Wert = DeltaBase). Jeder Eintrag: 4 Halbbytes,
von Eintrag zu Eintrag fallend — Bitbreiten je Komponente und Koeffizient.

**Runde 10:** D1 prüft die Zuordnung an allen DCT-Clips · D2 Bitbudget (Summe
der Halbbytes mit/ohne Vorzeichenbit × 1, Blöcke zu 4/8/16 Keys, Keys) gegen
die Datenmenge · D3 mittlere Breite je Halbbyte-Position für Drehungen und
Vektoren · C6 (VBR) beste Verschiebung je Block gegen das Kopfbyte.

## Codec-Labor Runde 10 → 11 (0.60.0): DCT-Aufbau bestätigt

**Runde 10 bei DH:** **D1 4 016 von 4 016** — oberes Halbbyte von
DofTableDescBytes = Einträge je Kanal in BitsPerSubblock (unteres immer 0;
278 122 Kanäle mit 0 = konstant, 203 005 mit 8 Einträgen). **D2:** Data*8 /
(Summe der Halbbytes × Blöcke zu 8 Keys) im Median **0,971** (10 % 0,945,
90 % 0,986) — Blöcke zu 8 Keys, knapp darunter (letzter Block kürzer). **D3:**
mittlere Breite je Halbbyte (oben zuerst) Drehungen 4,70/4,61/6,81/5,74,
Vektoren 6,86/6,19/6,78/1,19 — Drehungen mit vier Komponenten, Vektoren mit
drei. **C6 (VBR):** einheitliche Verschiebung je Block passt nur bei 184 von
3 393 Blöcken — verworfen.

**Runde 11:** D4 exakte Bitsumme (alle Einträge / letzter Block min(n, Keys) /
zusätzlich Byte je Block) gegen Data (genau bzw. auf 16 Byte gerundet). D5
liest Block 0 und rechnet zurück: Einheitslänge der Drehungen mit dem
bekannten Grundwert DeltaBase/16384 — 32 Varianten (Bitreihenfolge,
Vorzeichen, je Koeffizient/Komponente, Halbbyte-Reihenfolge, Grundwert
addiert oder nicht) × 15 Skalen.

## Codec-Labor Runde 11 → 12 (0.61.0)

**Runde 11 bei DH:** D4 — keine der drei Blockregeln trifft die Datenmenge
genau (Data − Bytes im Median +14…+16, aber Streuung −225…+626): es gibt
zusätzliche Daten mit variabler Menge (Kandidat: `CatchAllBitCount`, Ausreißer).
D5 — alle Varianten „gewannen" mit der kleinsten Skala; dort zählt nur der
Grundwert, der ohnehin Länge 1 hat → Messung nicht aussagekräftig.

**Runde 12:** D5b „Tangentenprobe", skalenfrei: q(t) = Grundwert + kleine
Änderung; eine echte Drehbewegung bleibt auf der Einheitskugel, ihre Änderung
steht senkrecht zum Grundwert, |q| schwankt dann kaum im Verhältnis zur
Bewegung. Konzeptprobe hier: echte Bewegung 0,016, zufällige Bits 1,05.
64 Varianten (Bitreihenfolge, Vorzeichen, Leserichtung, Halbbyte-Reihenfolge,
Koeffizient 0 mit/ohne, Kanal für Kanal / Eintrag für Eintrag über alle Kanäle).

## Codec-Labor Runde 12 → 13 (0.62.0): die einfachsten Fälle

**Runde 12 bei DH:** D5b — alle 64 Varianten 0,56–0,71 (Zufall ~0,5–1). Die
Reihenfolge, in der Kanäle/Blöcke im DCT-Strom stehen, stimmt in keiner
getesteten Form, oder vor den Werten liegt ein Bereich (Sammelbereich?).

**Runde 13:** Fälle, bei denen die Kanal-Reihenfolge keine Rolle spielt.
D5c — DCT-Clips mit genau EINER bewegten Drehung: vier Reihenfolgen über
Blöcke/Einträge/Komponenten, Start bei Byte 0 oder hinter einem Sammelbereich,
Tangentenprobe über alle Blöcke. D6 — die drei kleinsten davon komplett als
Hex (Einträge, Grundwert, KeyTimes, Data). C7 — VBR-Clips mit genau einer
Float-Spur, Blöcke als Hex. Beides steht vorn in `codeclab_hex.txt` zur
Auswertung von Hand.

## Codec-Labor Runde 13 → 14 (0.63.0)

**Runde 13 bei DH:** Es gibt **keinen** DCT-Clip mit genau einer bewegten
Drehung (≥ 9 Keys) und unter den ersten 400 Float-Clips keinen mit genau einer
Spur — D5c/D6/C7 blieben leer. **Runde 14:** D5c bewertet jetzt nur den ERSTEN
bewegten Kanal (muss eine Drehung sein): bei kanalweiser Ordnung stehen seine
Daten ganz vorn, bei blockweiser seine Block-0-Daten — die Reihenfolge der
übrigen Kanäle stört so nicht. Der mögliche Sammelbereich am Anfang wird aus
der Summe ALLER Kanäle berechnet. D6/C7 zeigen die Clips mit den wenigsten
bewegten Kanälen bzw. Spuren (alle Kanäle mit Grundwert und Einträgen).

## Codec-Labor Runde 14 → 15 (0.64.0)

**Runde 14 bei DH:** D5c (nur erster bewegter Kanal, 372 Clips): beste Lesart
**0,383** — MSB, Zweierkomplement, Block > Eintrag > Komponente, Halbbytes oben
zuerst; Zufall bis 0,92, richtig wäre ≈ 0,02. Erstmals eine klar bevorzugte
Richtung, aber noch nicht richtig. Start bei Byte 0 und „hinter
Sammelbereich" gleich (kein Sammelbereich vorn). D6 zeigte nur Clips ganz ohne
Bewegung (Data 32 Nullbytes) — jetzt nur noch Clips mit Bewegung.

**Runde 15:** D5d um die beste Lesart herum — alle 24 Zuordnungen der vier
Halbbytes zu X/Y/Z/W, Koeffizient 0 mit/ohne, nur Clips mit
CatchAllBitCount = 0 (384 Varianten).

## VBR und DCT: eigene Decoder (0.65.0)

**Auf Wunsch von DH** habe ich das AssetBankPlugin im FrostyToolsuite-Fork von
Virjoinga gelesen (Lizenz CC BY-NC-ND 4.0), um die letzten Formatfragen zu
klären. **Kein Code übernommen** — die Decoder in `fbanim.cpp` sind eine eigene
Umsetzung aus diesen Fakten:

* **VBR:** Reihenfolge Keyzeiten → Palettenindizes (1 Byte, 2 Byte ab 257
  Palettenwerten) → Lauflängen-Karte → 4 Byte Bitbreiten je animierter
  Komponente → Vektor-/Float-Versatzkurven → Blöcke (alles davor im Labor an
  allen Clips gemessen). Neu: die drei Kopfbytes je Block sind Quantisierer
  für Drehung / Trajektorie / Verschiebung; der Block wird **von hinten**
  gelesen (Bytes rückwärts, Bits je Byte vom niedrigsten an); je Komponente
  erst ein Anwesenheitsbit je Koeffizient mit Breite > 0, dann ein
  Vorzeichenbit je vorhandenem, dann die Werte; Stufe je Koeffizient
  (s·ln(k+2)+1)·Dct/32768 mit s = (Kopfbyte+1)·0,2·Dct; Wert = (0,5 + Σ …)·Spanne
  + Minimum; der letzte Block deckt die letzten 8 Keys ab; die erste animierte
  Vektorspur ist die Trajektorie (eigener Bereich).
* **DCT:** Bits vom höchsten an; je Block zu 8 Keys je Kanal die Einträge aus
  `BitsPerSubblock` (X Y Z W = Halbbytes von oben), Breite 15 =
  `CatchAllBitCount`; im ersten Block fehlt Eintrag 0 — Koeffizient 0 ist dort
  der Grundwert `DeltaBase`, in späteren Blöcken wird er dazugezählt; Gewicht je
  Koeffizient (QuantizeMultSubblock·0,1·i + 1)/QuantizeMultBlock.
* **Additive Clips** (viele DCT-Clips): die Drehung wird auf die Ruhelage
  gesetzt, die Verschiebung addiert.

**Hier geprüft:** VBR-Decoder am Float-Clip SW02_VO_…0049_IDEN — alle Blöcke
gehen auf, Werte glatt (z. B. 4,98 5,55 6,51 … 9,53), linke/rechte Kurven
spiegelbildlich, konstante Spuren genau 0,0000. DCT hier ohne vollständiges
Beispiel — E1 im Labor prüft beide an allen Clips (gelesen, Länge 1,
Sprungwinkel). Im Animationsfenster sind VBR/DCT jetzt ladbar.

### 0.65.1 — Absturz im Labor behoben

Bei DH brach das Labor in E1 ab (Log endet mitten in einer Zeile). Ursache:
bei VBR-Clips, deren Kanäle keine Rig-Namen haben (alle heißen „kanalN" und
gelten damit als Floats), lief die Zuordnung über den Wertepuffer hinaus.
Jetzt: Passen die Namen nicht zu den Zählern, gilt die Reihenfolge Drehungen →
Vektoren → Floats, geprüft gegen die Karte; jeder Zugriff wird vorher
geprüft. Robustheitsprobe hier: 6 000 zufällig verbogene VBR-/DCT-Datensätze
unter ASan/UBSan — kein Absturz. Das Labor schreibt sein Log jetzt Zeile für
Zeile, damit bei einem Abbruch nichts verloren geht. Derselbe Fehler hätte
auch das Plugin in Max treffen können.

### 0.65.2 — Ergebnis E1 bei DH, schneller

**E1 an allen Clips (0.65.1):** VBR **11 688 von 11 720** gelesen (99,7 %;
24× Kanalarten nicht bestimmbar, 8× Block zu kurz), animierte Drehungen
|1 − |q|| Median **0,00031** (99 %: 0,0048), Winkel zwischen zwei Keys Median
0,61 Grad. DCT **4 016 von 4 016** gelesen, |1 − |q|| Median **0,00018**
(99 %: 0,041), Winkel Median 0,46 Grad. Einzelne Ausreißer (max 1,14 bzw.
0,74; max 180 Grad) bleiben zu untersuchen.

**In Max (0.65.0):** Vader „Load all" — 240 Clips entpackt (1 nicht),
1 025 605 Keys in 63 s, Notizspur 480 Keys und 240 Sequenz-Attribute
zurückgelesen.

**Tempo:** Die Namenskette (Rig-Suche über alle Rigs) kostete je Clip ~0,1 s
(E1: 1 130 s für VBR; Max: 240 Clips in 27 s). Jetzt je ChannelToDofAsset
einmal gesucht und zwischengespeichert. E1 prüft nur noch jeden dritten Clip.

## Animationsfenster: Standardklassen (0.66.0)

Helden findet das Fenster über ihren Namen im Clipnamen (vader, dooku …).
Soldaten-Clips tragen stattdessen Klassen- und Waffenwörter. Neu in der
Figurenliste: **Profile** — „Soldier: Assault (rifle)", „Soldier: Heavy",
„Soldier: Officer (pistol)", „Soldier: Specialist (sniper)", „Droid soldier
(B1/B2)". Ein Profil passt, wenn eines seiner Wörter im Clip vorkommt; mit dem
Suchfeld weiter eingrenzen (z. B. „imperial", „rebel", „clone"). Steht eine
Standardfigur in der Szene (z. B. `l_assault_newera`), wird das passende
Profil vorgewählt — auch beim Wechsel des Rigs. Das ist eine Namensregel,
nicht die exakte Zuordnung des Spiels (die stünde in den Animationszuständen
der Soldaten-Blueprints — späterer Schritt).

## Zuordnung wie im Spiel — erster Versuch (0.67.0)

**Frosty** ordnet bei SWBF2 nichts zu: man öffnet eine AntState-Bank und stellt
das Export-Skelett in den Optionen selbst ein; exportiert werden alle Clips
der Bank (Namen = Asset-Namen der Bank, bei SWBF2 meist Kennungen).

**Recherche (Nexus-Anleitungen):** Figuren haben „Actor"-EBX
(`Actor_Weapon_<Held>`, `Actor_Lightsaber_<Held>` unter Characters/NPC/
Characters); unter Object → Components steht ein Feld `AntGameStates`, dessen
Wert in die ANT-Daten zeigt — Modder tauschen darüber Menü-, Intro- und
Siegesanimationen. Haltung und Bewegungsstil kommen über die Waffen-Blueprint
aus dem Kit (`Gameplay/Kits/Hero/<Held>/Kits/Kit_Hero_<Held>`).

**Z1 im Labor:** je Held alle EBX mit seinem Namen im Pfad nach AntRefs
(`AssetGuid`) absuchen, in ANT-Schlüssel umrechnen (wie beim Gesicht), dann
`Quelle::Folge` — allen Verweisen im ANT-Graphen folgen und die erreichbaren
Clips sammeln. Ausgabe: EBX, gefundene AntRefs, besuchte Knoten und Klassen,
erreichte Clips (davon mit Namen / ladbar), Beispielnamen.

### Z1 bei DH und Grievous (0.68.0)

**Z1 (Zuordnung über AntRefs):** Die Verweise führen in einen **gemeinsamen**
Zustandsautomaten: alle Lichtschwert-Helden (Vader, Anakin, Luke, Rey …)
erreichen dieselben 272 Clips (gemischt: C_Yoda…, L_Grievous…, L_Vader…),
alle Blaster-Helden (Boba Fett, Han, Chewbacca) dieselben 503 (viele 1p-
Waffenclips). Die Helden-Auswahl steckt darin in Tag-/Zustandsbedingungen
(EnumerationValueAsset, TagCollectionSetAsset, BranchOutTagCollectionAsset,
FloatGameStateTag, StateFlowTransitionAsset). Außerdem erreicht der Graph nur
einen Teil (Vader: 240 ladbare Clips mit „Vader" im Namen, der Graph erreicht
insgesamt nur 284 über alle Helden). Hauptquellen der AntRefs:
`gameplay/characters/heroes/specializations/hero_lightsaber_<held>` bzw.
`hero_weapon_<held>` und `characters/npc/characters/actor_…_<held>`.
Nächster Schritt dort: die Tag-Bedingungen je Held auswerten.

**Grievous (DH, 0.66.0):** Startpose kaputt, Arme falsch. Ursache: die
Ruhelage im Animationsfenster kam für jede Figur aus walrus_humanmale — Knochen
ohne Verschiebungskanal bekamen menschliche Längen, die Startpose die
Walrus-Pose. Jetzt: `RuheFuer()` nimmt das eigene Skelett der Ziel-Figur
(`…<figur>…_ske` unter characters/, ohne _1p), sonst Walrus; Protokoll nennt
das Skelett. „Nicht entpackbar" wird mit Grund protokolliert (bis 12).

### Zuordnung, Runde 2 (0.69.0)

DH: Grievous mit eigenem Skelett ist behoben. **Z2** macht den gemeinsamen
Zustandsautomaten sichtbar: je Held (Vader, Boba) die Wurzeln aus
`specializations/` und `actor_` mit Klasse, Name und Feldern; Beispiele jeder
Knotenklasse (BranchOutTagCollection, TagCollectionSet, EnumerationValue,
StateFlowTransition, GameStateTags, Layouts …) mit aufgelösten Verweisen; und
ob Verweise auch als 32-stellige GUID vorkommen (die `Folge()` bisher nicht
nimmt — möglicher Grund, warum der Graph nur einen Teil erreicht).
`ANIMLAB.bat` läuft jetzt nur noch die Zuordnungs-Abschnitte (schnell); die
Codec-Abschnitte mit `--alles`.

### Zuordnung, Runde 3 (0.70.0)

**Z2 bei DH (18,9 s):** Vaders 86 Wurzeln sind vor allem Zustandsgrößen
(BoolAsset 41, FloatAsset 25, GameStateEnumerationAsset 10) plus
SceneOpMatrix, StateFlowController, ActorAsset. Darunter
**`HeroCharacter.EnumGS`** → EnumerationAsset **`HeroCharacter.Enum`** — der
Schalter, über den der gemeinsame Automat den Helden unterscheidet.
EnumerationValueAssets tragen `EnumerationAsset` + `Value`; Übergänge
(`StateFlowTransitionAsset`) haben `ConditionsRequiredTrue/False`;
Game-State-Tags `ValueAsset` + `Value`. Verweise gibt es nur als
16-stellige Schlüssel (51 846, keine 32-stelligen).

**Z3:** (a) welche Zustandswerte die Vader-EBX setzen (Objekt, Feld, Ziel,
übrige Felder); (b) die Werte von HeroCharacter.Enum im Graphen mit ihren
Verweisern; (c) Verweiskette von drei Vader-Clips nach oben.

### Zuordnung, Runde 4 (0.71.0)

**Z3 bei DH:** Die Helden-Spezialisierung schreibt
`WriteEnumerationGameState HeroCharacter.EnumGS = Wert` (Vader **2**), der
Actor zusätzlich `CharacterState.Character.EnumGS = 2`. `HeroCharacter.Enum`
hat 47 Werte, benannt u. a.: Luke 1, Vader 2, Boba Fett 3, Han Solo 4, Leia 5,
Palpatine 6, Yoda 7, Kylo 8, Lando 9, Phasma 13, Rey 15, Iden 16, B2 17,
Finn 19, Ewok 20, ObiWan 21, Anakin 22, Dooku 23, EwokRebel 27, BB8 29,
BB9E 30 (weitere als HeroChar_24 … _45). Vaders Clips hängen unter
**IndexChooserControllerAssets** (`HeroMelee.Idle.AimFwd.Index`,
`HeroMelee.Sprint.HitReact.Left3.Index` …) im Automaten `.HeroMelee.Top.SF`.

**Z4:** Aufbau der Auswahlknoten mit allen Feldern und jeder Knoten, der
HeroCharacter.EnumGS direkt verwendet — Grundlage für den Helden-Filter.

### Zuordnung wie im Spiel — im Animationsfenster (0.72.0)

**Z4 bei DH:** `IndexChooserControllerAsset` hat `ChoiceAssetList[47]` — die
Stelle ist der Heldenwert: `HeroMelee.StandTrun.Left.Index` → 0 Rey (Vorgabe),
1 Luke, 2 **Vader**, 6 Palpatine, 7 Yoda, 8 Kylo, **11 Grievous**, **14 Maul**,
15 Rey … Dazu `EnumBoolAsset` (z. B. `Hero.Rey.EnumBool`: Source =
HeroCharacter.EnumGS, Value = 15) als Bedingung in Übergängen und
Chooser-Einträgen.

**Umgesetzt:** `fbzuordnung::LiesWurzeln` (AntRefs + Heldenwert aus
`WriteEnumerationGameState HeroCharacter.EnumGS` in Spezialisierung, Actor,
Kit der Figur) und `Quelle::FolgeFuerHeld` (an Helden-Auswählern nur
`ChoiceAssetList[held]`, Übergänge/Einträge mit unpassenden EnumBools
auslassen). **Animationsfenster:** Figur-Filter = Name im Clip **oder** vom
Spiel zugeordnet; die Statuszeile nennt, wie viele aus der Spielzuordnung
kommen, und den Heldenwert. Einmal je Figur berechnet (~1 s). **Labor Z5**
rechnet das für 14 Helden durch.

### Helden-Filter verfeinert (0.73.0)

**Z5 bei DH:** Lichtschwert-Helden gut getrennt — Vader 43 Clips (30 mit
eigenem Namen), Luke 42 (30), Rey 43 (20), Anakin 44 (19), Grievous 38 (25),
Maul 34 (21), Yoda 38 (20), Dooku 44 (19), Kylo 44 (17); Heldenwerte Grievous
11, Maul 14, Chewbacca 12, Iden 16. **Blaster-Helden** (Boba, Han, Chewbacca,
Iden) bekamen dagegen alle denselben gemeinsamen Waffen-Automaten (503–508
Clips, fast nur 1p) — dort entscheidet nicht der Heldenwert. Palpatine: keine
EBX gefunden (Pfade heißen „emperor").

**Jetzt:** `FolgeFuerHeld` merkt sich je Knoten, ob er über eine
**Heldenweiche** erreicht wurde (Heldenplatz im IndexChooser oder eine für den
Helden wahre EnumBool-Bedingung); nur diese Clips (`clipsHeld`) nimmt das
Fenster. Blaster-Helden fallen damit auf die Namensregel zurück, statt 503
fremde 1p-Clips zu bekommen. Alias „emperor" für Palpatine.

### Zuordnung: Stand und nächste Gruppe (0.74.0)

**Z5 (0.73.0) bei DH — Lichtschwert-Helden über Heldenweichen:** Vader 30
(alle mit eigenem Namen), Luke 29 (27), Rey 30 (20), Anakin 31 (19), Grievous
25 (25), Maul 21 (21), Yoda 19 (19), Dooku 31 (19), Kylo 31 (17), Palpatine
24 (10; jetzt gefunden, Wert 6). Der Rest sind die Vorgaben, die das Spiel für
Helden ohne eigenen Clip einsetzt. **Blaster-Helden** (Boba 3, Han 4,
Chewbacca 12, Iden 16): 0 über Heldenweichen — sie nutzen im Fenster die
Namensregel.

**Z6:** für Boba Fett, Stormtrooper, Klone und AT-ST die EBX-Pfade, die
geschriebenen Zustände (WriteEnumeration/Integer/BoolGameState) und die
Weichen im erreichten Graphen (welche Zustände die IndexChooser abfragen,
Listenlänge) — daraus der Filter für Blaster-Helden, Soldaten und Fahrzeuge.

### Zuordnung für Blaster-Figuren und Soldaten (0.75.0)

**Z6 bei DH:** Blaster-Figuren verzweigen **nach der Waffe**, nicht nach dem
Helden: im erreichten Graphen fragen 23 IndexChooser `WeaponType.EnumGS`
(Liste 9), 13 `SpecificWeapon.EnumGS` (86), je einer
`Game.Character.BodyType.EnumGS` (33) und `MP.QuickThrowType.EnumGS` (3).
Boba schreibt HeroCharacter 3 und viele eigene Bools (Jetpack, RocketFire,
Ping, BarrageFire); Stormtrooper schreiben `WeaponType` 0, 2, 7,
`Game.Character.IsHero.Bool` 0 und CharacterState-Werte 47–60; Klone liegen
unter `hero_weapon_special_clone_jumptrooper`. **AT-ST:** 48 EBX, aber **0
AntRefs** — Fahrzeuge hängen nicht am ANT-Graphen der Figuren
(`atst_masterskeleton`, `…_animset`, `…_ske` sind eigene Assets).

**Umgesetzt:** `LiesWurzeln` sammelt jetzt **alle** geschriebenen Zustände
(Held, Waffentyp, konkrete Waffe, Körpertyp …), `Quelle::FolgeFuerZustaende`
folgt dem Automaten mit allen Weichen gleichzeitig und nimmt an einem
IndexChooser nur die Plätze der Werte, die die Figur setzt. Auch die
Klassenprofile (#assault …) laufen darüber; ihre Suchwörter zeigen auf die
Soldaten-Actors (stormtrooper, rebelsoldier, clonetrooper …). Statuszeile:
„N from the game's own assignment". **Labor Z7** rechnet es je Figur durch.

### Zuordnung: Stand nach Z7 (0.76.0)

**Z7 bei DH:** Helden weiter sauber (Vader 30, Grievous 25 — alle mit eigenem
Namen). Blaster-Figuren: Han, Chewbacca, Iden je **29** Clips — aber alle in
**Ego-Perspektive** (`A_1p_Rifle_…`), weil ihr WeaponType 0 (Gewehr) nur die
1p-Waffenäste öffnet. Boba **0** (schreibt keinen WeaponType in seinen
eigenen EBX), Stormtrooper **0** von 10 erreichten (seine Actors liefern 67
Wurzeln, aber kaum Automaten-Einstiege), „cloneassault" 0 EBX.

**0.76.0:** `LiesWurzeln` sucht jetzt auch unter `gameplay/weapons/`,
`gameplay/kits/` (nicht nur kits/hero) und `gameplay/characters/` — dort
schreibt Boba seinen Waffentyp und dort liegen die Soldaten-Kits. Neu:
`ausEbx` (welcher EBX-Pfad wie viele Wurzeln lieferte) und in Z7 die Zahl der
1p-Clips getrennt, dazu die Klassenprofile #assault/#heavy/#officer/#droid.

### Zuordnung: Stand nach Z7 (0.77.0)

**Z7 (0.76.0) bei DH:** Helden unverändert gut (Vader 30, Grievous 25, **0 in
Ego-Perspektive**). Herkunft der Wurzeln: Helden bekommen ihre Automaten-
Einstiege aus `gameplay/characters/heroes/specializations/hero_…_<held>`
(Vader 66, Han 85, Iden 86). **Soldaten haben keine specializations** — ihr
`actor_weapon_stormtrooper` liefert 65 Wurzeln, erreicht aber nur 10–16 Clips;
der Rest ihrer Wurzeln sind Leichen-Prefabs aus der Spielwelt. **Han/Iden:**
29 Clips über die Waffenweiche, davon **28 in Ego-Perspektive** — WeaponType 0
öffnet nur die 1p-Äste. Boba: weiter 0 (schreibt keinen WeaponType).

**Z8:** rückwärts — die 3p-Bewegungsclips (`Rifle_Run…`, `Rifle_Stand…`)
liegen im Graphen von Han (452 Clips). Das Labor geht von ihnen nach oben bis
zum ersten IndexChooser und nennt die Zustandsgröße, die dort entscheidet.
Damit sollte der Einstieg für Soldaten und 3p-Bewegungen gefunden sein.

### Z8: die Wurzeln führen in die Ich-Ansicht (0.78.0)

**Z8 bei DH:** Die 3p-Clips im Graphen von Han hängen unter Knoten des
**Ich-Ansicht-Automaten**: `Rifle_WalkToStand` ← Chooser Entry ←
`1P.Legs.WalkFwdToStand.EC` ← Steering ← `WalkToStand.Node` ← … ←
**`.1P.Top.SF`**; `Rifle_StandIdle_Pose` hängt an `Soldier.NetworkProxy`.
Kein IndexChooser dazwischen — deshalb „keine Weiche". Die Wurzeln der Figuren
(auch der Helden mit Blaster) führen also nur in den 1P-Automaten; der
3p-Automat wird von keiner Figuren-EBX erreicht.

**Z9 (neu):** `Quelle::SucheAssets` durchsucht eine Bank nach Klasse und Name.
Das Labor listet alle `StateFlowControllerAsset` der gemeinsamen Bank
(`.1P.Top.SF`, `.HeroMelee.Top.SF`, …) und startet den Zustands-Filter direkt
von den 3p-/Soldaten-Kandidaten aus, mit den Zuständen von Soldat, Han und
Boba. Damit sollte der richtige Einstiegspunkt feststehen.

### Z9: nur ein Automat in der gemeinsamen Bank (0.79.0)

**Z9 bei DH (1,3 s):** In `sharedbundleanimation_common` steckt genau **ein**
StateFlowController: `LW.Humanoid.Top.SF` (Living World — die Statisten). Der
1P-Automat (`.1P.Top.SF`) und der Nahkampf-Automat (`.HeroMelee.Top.SF`)
liegen also in **anderen** Bänken; die Kandidatensuche lief ins Leere.

**Z10:** `SucheAssets` jetzt über **alle** Bänke — jeder StateFlowController
mit „Top.SF" im Namen, mit Bankangabe. Danach werden alle Kandidaten außer der
Ich-Ansicht mit den Zuständen von Soldat, Han und Boba durchgerechnet
(Clips gesamt, über eine Weiche, ladbar, Ego-Anteil, Beispiele).

### Gefunden: `.3P.Top.SF` — der Automat der sichtbaren Figur (0.80.0)

**Z10 bei DH (243 s Vollsuche):** 13 Automaten mit „Top.SF": `.1P.Top.SF`,
**`.3P.Top.SF`**, `.AI.Top.SF`, `.HeroMelee.Top.SF` (alle Bank
`coop_nt_mc85`), `.Droideka.Top.SF` (crait_02), `Droid.Top.SF` und
`Creature.Pillio.Top.SF` (rootlevel), `Droid.MP.Top.SF`, `Ewok.Top.SF`,
`FB.ProxyController.Top.SF` (tatooine_02), `Astromech.Top.SF` (scarif_02),
`LW.Creature.Top.SF`, `LW.Humanoid.Top.SF`.

**Von `.3P.Top.SF` aus greift der Zustands-Filter:** Soldat (#assault) **123
Clips, 0 in Ego-Perspektive** (A_HM_Rifle_…, A_B1_…, A_B2_…, Jetpack), Han
**47** (A_HM_Rifle_…, DualPistol, Jetpack). `.HeroMelee.Top.SF` gibt Han und
Boba je **26** (Emotes E_Han_…, E_Boba_… plus Luke/Rey-Vorgaben). Boba über
`.3P` weiter 0 — er schreibt keinen WeaponType.

**0.80.0:** `fbzuordnung::AutomatenWurzeln` sucht diese Automaten einmal
(gezielt in den bekannten Bänken, sonst überall) und gibt sie als zusätzliche
Wurzeln in den Filter — im Animationsfenster und im Labor (Z11). Damit
bekommen Soldaten und Blaster-Helden endlich ihre sichtbaren Bewegungen.
Offen: der Körpertyp wird noch nicht gesetzt, deshalb sind bei Soldaten auch
Droiden-Clips (B1/B2) dabei.

### Zuordnung fertig für Helden UND Soldaten (0.81.0)

**Z11 (0.80.0) bei DH — der Filter mit `.3P.Top.SF` als zusätzlicher Wurzel:**
Vader 32 Clips (30 mit eigenem Namen), Grievous 27 (25), Han 104, Iden 107,
Boba 28 (mit E_Boba_…), Assault 123, Heavy/Officer/Specialist je 87, Droid
141 — alle in Sekunden (Automatensuche einmal 21,5 s). Endlich die richtigen
Sachen: `A_HM_Rifle_Jump_…`, `Add_HM_Rifle_CrouchIdleLoop_01`,
`T_HM_DualPistol_RunToStand_Fwd_01`, `A_HM_Jetpack_…`.

**Zwei Schönheitsfehler behoben:** Über die eigenen Wurzeln kamen bei Han, Iden
und #droid noch Clips der **Ich-Ansicht** mit (je 28 bzw. 19), und über den
3p-Automaten alle **Körpertypen** (A_HM_ Mensch, A_B1_/A_B2_ Kampfdroiden),
weil die Figuren keinen Körpertyp schreiben. `fbzuordnung::PasstZumSkelett`
lässt zugeordnete Clips nur durch, wenn sie zum Skelett passen: nie „1p", und
Droiden-Clips nur bei Droiden-Figuren. Clips, die den Namen der Figur tragen,
bleiben unberührt.

### Zuordnung bestätigt, Fahrzeuge begonnen (0.82.0)

**Z11 (0.81.0) bei DH — alles sauber:** Vader 32/32, Grievous 27/27, Han 76,
Iden 79, Boba 28, Assault/Heavy/Officer/Specialist je 54, Droid 122 — **0 in
Ego-Perspektive**, Droiden-Clips nur noch bei Droiden. Damit ist die
Zuordnung für Figuren fertig. (Die vier Soldatenklassen bekommen dieselben
54 Clips: sie schreiben denselben WeaponType und unterscheiden sich erst über
die konkrete Waffe.)

**Fahrzeuge — Stufe 1:** Neues Modul `src/fbfahrzeug` baut die Fahrzeugliste
aus dem Index: Ordner und Art (`ground`, `air`, `capital`, `stationary`,
`pilots` …), Zahl der MeshSets, die Bundles für den Import, das
**Hauptskelett** (`…_masterskeleton`, sonst das kürzeste `…_ske`, ohne
Zerstörungsteile) und die **Animationssätze** (`…_animset`). Ausgabe über
`castool --fahrzeugliste`, in START.bat ergänzt. Damit steht fest, welche
Fahrzeuge ein Skelett und eigene Animationen haben — die Grundlage für den
Import (Stufe 2: Modell über `BaueFigur` mit dem Fahrzeug-Skelett, Stufe 3:
die Animationssätze im Animationsfenster).

### FAHRZEUGE.bat (0.83.0)

Wie ANIMLAB.bat, nur für die Fahrzeugliste: baut inkrementell nur `castool`
und ruft `--fahrzeugliste` auf. Ergebnis `fahrzeuge.log` (die Liste) und
`FAHRZEUGE.log` (Bau und Aufruf); die Summenzeile erscheint direkt im Fenster.
Braucht `index.fbidx` im selben Ordner — START.bat ist dafür nicht nötig.

### Fahrzeuge: Liste gemessen, Stufe 2 begonnen (0.84.0)

**`fahrzeuge.log` bei DH:** 99 Fahrzeuge — air 39, ground 21, stationary 14,
pilots 12, capital 11, corvette 1, spacebattles 1. **Nur 6 haben ein Skelett
im eigenen Ordner:** at-at (32 MeshSets), at-st (7), at_te (13), atrt (11),
droideka_01 (3), dwarfspiderdroid (6, dazu 2 Animationssätze). Alle anderen
(auch homingspiderdroid, spha_t, mtt, aat) stehen ohne da — entweder teilen
sie sich Skelett und Animationen mit einem anderen Ordner oder sie sind starr.

**0.84.0:** `fbfahrzeug` sucht Skelett und Animationssätze jetzt auch
**außerhalb** des Fahrzeugordners nach dem Namen (dicht geschrieben, damit
`at_te` auch `atte` trifft); in der Liste steht dann „(fremd)". Neu
`castool --fahrzeugliste --extrahiere <name> <datei>`: baut das Modell aus
**allen Bundles** des Fahrzeugordners mit seinem Hauptskelett und schreibt
eine `.fbmodel` (Teile werden zusammengefügt, Materialien umnummeriert).
FAHRZEUGE.bat baut testweise AT-ST und AT-RT.

### Fahrzeuge: Bundles richtig gefunden (0.85.0)

**0.84.0 bei DH:** Die Suche außerhalb des Ordners brachte nur einen Treffer
(`atst` → `at-st/atst_masterskeleton`), also 7 von 99 mit Skelett. Der
Modellbau schlug bei AT-ST und AT-RT komplett fehl: „kein Bundle mit
gameplay/vehicles/ground/atrt/…_mesh". Ursache: Bei Figuren ist der
MeshSet-Name zugleich der Bundlename, bei Fahrzeugen **nicht**.

**Behoben:** `fbfahrzeug` nimmt jetzt die Bundles aus dem Index-Eintrag des
MeshSets (`Eintrag::bundles`) statt dessen Namen. `--extrahiere` nennt vorab
Skelett und Bundlezahl. FAHRZEUGE.bat baut weiterhin AT-ST und AT-RT.

### Fahrzeuge: MeshSets statt Level-Bundles (0.86.0)

**0.85.0 bei DH:** Der Bau lief jetzt durch, aber mit dem falschen Inhalt: Die
Bundles der Fahrzeug-MeshSets sind **Level-Bundles** — AT-ST landete bei 34
Bundles wie `levels/mp/endor_01/fantasybattle` mit zusammen 204 Meshes und
**0 Bones**, AT-RT bei 28 Bundles mit **6978 Meshes** (alles, was auf den
Karten steht) und 53 Bones.

**0.86.0:** Neu `fbfigur::BaueMeshSet` — derselbe Kern wie `BaueFigur`, aber
nur für EIN MeshSet (Bundle wird über den Index gefunden, die MeshSet-Schleife
auf diesen Namen gefiltert). `fbfahrzeug` merkt sich die MeshSet-Namen, und
`--extrahiere` baut Teil für Teil daraus. Damit enthält das Modell nur noch
das Fahrzeug.

### AT-RT steht — AT-ST lag in zwei Ordnern (0.87.0)

**0.86.0 bei DH:** **AT-RT: 11 von 11 MeshSets, 53 Bones, 14 Meshes, 13,9 MB**
— das Skelett ist vollständig und sinnvoll (Reference, AITrajectory,
Trajectory, Hips, LeftHip, LeftUpLeg, LeftLegUpperPiston …). Der Fahrzeug-
Import funktioniert also. **AT-ST dagegen: 0 Bones, 2 Meshes** — in seinen
7 MeshSets stecken nur Kopf-Cluster, Geschosse und Zerstörungsteile. Der Rumpf
liegt im **zweiten Ordner** `ground/atst` (3 MeshSets), den die Liste getrennt
führte.

**0.87.0:** Ordner mit gleichem dichten Namen werden zusammengelegt
(`at-st` + `atst`, wie bei der Skelettsuche). Neu `IstFahrzeugteil`: Wrack,
Zerstörung, Despawn/Leftover, „donotuse", Geschosse und Detonatoren kommen
nicht ins Modell (hier an den acht echten Namen aus DHs Log geprüft, 0 Fehler).

### AT-RT sauber, AT-ST-Rumpf fehlt weiter (0.88.0)

**0.87.0 bei DH:** Ordner zusammengelegt (99 → **97** Fahrzeuge, at-st jetzt
mit **10** MeshSets), Filter greift: **AT-RT 8 von 11 MeshSets, 53 Bones,
9 Meshes, 4,5 MB** — ohne Wrack und „donotuse", das ist das fertige Modell.
**AT-ST:** von seinen 10 MeshSets bleiben nur zwei Kopf-Cluster übrig
(0 Bones, 1 Mesh) — auch die drei aus `ground/atst` fielen durch den Filter.
Der Rumpf liegt also nicht unter `gameplay/vehicles/`.

**0.88.0:** `fbfahrzeug` sucht MeshSets jetzt zusätzlich **außerhalb** von
`gameplay/vehicles/` nach dem dichten Fahrzeugnamen (wie schon Skelett und
Animationssätze), und `--extrahiere` protokolliert jedes übersprungene MeshSet
mit Namen — damit ist im nächsten Log zu sehen, wie die AT-ST-Teile heißen und
warum sie aussortiert wurden.

### Der AT-ST hat gar kein geskinntes Mesh (0.89.0)

**0.88.0 bei DH:** Die Suche außerhalb fand 47 MeshSets mit „atst" im Namen —
aber **keins mit Bones**: Kulissen (`objects/props/objectsets/sullust/atst_parts/…`
Torso, Beine, Kopf, Geschütze einzeln), Kinofassungen
(`cinematics/objects/atst/atst_static_cine_body_mesh`), Trümmer, Wrackteile,
Fußabdruck-Decals, Raketen. Die einzige vollständige Fassung heißt
**`gameplay/vehicles/ground/atst/atst_static_donotuse_mesh`** — und wurde von
meinem Filter aussortiert. Beim AT-RT trägt dieselbe Art Datei sogar die 53
Bones: „donotuse" heißt nicht kaputt, sondern „nicht fürs Spiel"
(Requisiten-/Kulissenfassung).

**0.89.0:** `--extrahiere` baut in zwei Stufen — erst ohne diese Fassungen;
kommt dabei kein Teil mit Bones heraus, noch einmal mit ihnen. Beide Stufen
hier an den echten Namen aus DHs Log geprüft (12 Fälle, 0 Fehler).

### AT-ST: Rumpf da, Skelettsuche brach zu früh ab (0.90.0)

**0.89.0 bei DH:** Der zweite Versuch griff — das AT-ST-Modell ist jetzt 4,5 MB
mit Kopf (24 751 Vertices) und Rumpf (35 604), Materialien `Head_Mat` und
`Body_Mat`, aus
`s9_2/…/atst_scavenged_static_donotuse_mesh`. **Aber `bones=0`** — obwohl die
Meshes Bone-Verweise tragen (6 bzw. 19). Also fehlt nur das Skelett.

**Ursache:** `BaueIntern` nahm den **ersten** EBX, dessen Name den Suchtext
enthält, und brach dann ab (`break`) — beim AT-ST trifft der Name aber auch
Einträge ohne Bones (z. B. `…_animset`). Ergebnis: leeres Skelett.
**0.90.0:** Die Suche läuft weiter, bis ein Eintrag wirklich Bones liefert;
`_animset` fällt schon bei der Skelettwahl in `fbfahrzeug` raus; die Hinweise
je Teil („Skelett … nicht gefunden") stehen jetzt im Log. Das betrifft auch
Figuren — dort war der erste Treffer bisher immer richtig.

### `atst_masterskeleton` ist kein SkeletonAsset (0.91.0)

**0.90.0 bei DH:** Die Hinweise im Log machen es eindeutig — bei **jedem** der
22 AT-ST-Teile steht „Skelett gameplay/vehicles/ground/at-st/atst_masterskeleton
nicht gefunden". Es liegt also nicht an der Suchreihenfolge: Dieser Eintrag ist
gar kein `SkeletonAsset` (vermutlich ein Container, der auf das echte Skelett
verweist). Der AT-RT bleibt unverändert richtig (53 Bones, 9 Meshes).

**0.91.0:** Neu `fbfahrzeug::SucheSkelett` — öffnet jeden Kandidaten mit
„skeleton"/„_ske" im Pfad, dessen dichter Name das Fahrzeug trifft, und nimmt
den mit den **meisten Bones**. `--extrahiere` nutzt das Ergebnis (und
protokolliert den Wechsel), die Liste zeigt es für die Bodenfahrzeuge.

### AT-ST: kein Eintrag mit „skeleton"/„_ske" trägt Bones (0.92.0)

**0.91.0 bei DH:** `SucheSkelett` fand **keinen besseren** Kandidaten — kein
EBX mit „skeleton" oder „_ske" im Pfad und „atst" im Namen liefert Bones. Der
AT-RT bleibt richtig (53 Bones).

**0.92.0, zwei Ansätze:** (1) `LiesSkelett` nimmt ersatzweise nicht mehr das
**erste** Objekt mit `BoneNames`, sondern das mit den **meisten** —
Fahrzeug-EBX enthalten oft mehrere Objekte, davon eins mit einer kurzen
Teilliste. (2) Neu `castool --skelettsuche <name>`: listet jeden EBX mit
Skelett-/Rig-Bezug samt Objektzahl, Objekttypen, wie viele Objekte `BoneNames`
tragen (und wie viele Bones), und was `LiesSkelett` daraus macht. FAHRZEUGE.bat
ruft das für `atst` und `atrt` auf — der Vergleich mit dem funktionierenden
AT-RT zeigt, woran es liegt.

### Gefunden: das AT-ST-Skelett heißt `atst_ske01` (0.93.0)

**Die Diagnose bei DH ist eindeutig:**

| Eintrag | Typ | Bones |
|---|---|---|
| `gameplay/vehicles/ground/at-st/atst_masterskeleton` | **MasterSkeletonAsset** | **0** |
| `cinematics/objects/atst/atst_ske01` | SkeletonAsset | **32** |
| `…/destruction/atst_destruction_01_leftover_head_ske` | SkeletonAsset | 45 |
| `gameplay/vehicles/ground/atrt/atrt_ske` | SkeletonAsset | 53 |

`atst_masterskeleton` ist also nur ein **Behälter** ohne eigene Bones. Das
echte Skelett liegt unter `cinematics/` und endet auf **`_ske01`** — meine
Suche verlangte `_ske` am **Ende** des Pfades und ließ es durchfallen.

**0.93.0:** „_ske" zählt jetzt überall im Pfad, `MasterSkeletonAsset` gilt
nicht mehr als Vorzugstreffer (es hat ja keine Bones), und „rig" fiel als
Kennzeichen raus — es steckt auch in `…_leg_right_01_mesh`. Namensregel hier
an DHs sechs echten Pfaden geprüft, 0 Fehler.

### AT-ST mit Skelett — jetzt nur noch die eigenen Teile (0.94.0)

**0.93.0 bei DH: der AT-ST hat sein Skelett.** `cinematics/objects/atst/atst_ske01`,
**32 Bones** mit sinnvollen Namen (Reference, Trajectory, Hips, Head,
LeftHipHinge, LeftUpLegTwist, LeftFootRoll, LeftToe … RightToe, Connect,
LeftFutureFoot). 19 von 47 MeshSets, 23 Meshes, 15,3 MB.

**Aber es ist zu viel drin:** unter den 23 Meshes sind Kulissen
(`sul_prop_atst_torso/leg/head` — Sullust-Requisiten), Kinofassungen
(`atst_static_cine_body`), Effekte (`meshp_atst_debrisx9`, Fußabdruck-Decal),
ein Tatooine-Aufsteller und die **Endor-Bunkertür** `at_at_station_bunker_door`
(Namensverwechslung „atat" ↔ „at-st"). Zum Fahrzeug gehören nur sechs Teile.

**0.94.0:** Neu `IstEigenesTeil` — MeshSets aus `objects/props/`,
`cinematics/`, `fx/` und `levels/` fallen raus, und der dicht geschriebene
Fahrzeugname muss im Pfad vorkommen (so fällt „atat" bei „at-st" durch). An
DHs zehn echten Pfaden geprüft, 0 Fehler.

### Fahrzeuge: der Rumpf hängt am „donotuse" (0.95.0)

**0.94.0 bei DH:** Das Fremde ist raus — AT-ST nur noch 3 MeshSets, 4 Meshes,
1,1 MB. **Aber wieder ohne Rumpf:** übrig blieben Kopf-Cluster, eine
Cluster-Box und die First-Order-Außenhülle. Grund: Der zweite Versuch mit den
„static_donotuse"-Fassungen lief nur, wenn **gar keine** Bones gefunden wurden
— mit `atst_ske01` sind jetzt 32 da, also blieb er aus, und
`atst_static_donotuse_mesh` fiel wieder durch den Filter.

**0.95.0:** Die Entscheidung hängt jetzt am **Hauptmesh** statt an den Bones.
`HatHauptmesh` prüft, ob es ein `<fahrzeug>_mesh` gibt: AT-RT hat `atrt_mesh`
→ die statische Fassung wäre eine Dublette und bleibt draußen; AT-ST hat
keins → die statische Fassung ist die einzige vollständige und kommt mit. Das
Log sagt, welcher Fall vorliegt. An beiden echten MeshSet-Listen geprüft.

### AT-ST komplett — jetzt eine Fassung statt drei (0.96.0)

**0.95.0 bei DH: der Rumpf ist drin.** Das Log entscheidet richtig („kein
eigenes Hauptmesh" für AT-ST, „eigenes Hauptmesh vorhanden" für AT-RT), und
`atst_static_donotuse_mesh` liefert Kopf (24 751 Vertices) und Rumpf (35 604).
AT-RT unverändert 8 MeshSets, 53 Bones.

**Ein Rest bleibt:** Im AT-ST-Modell liegen **drei Fassungen** übereinander —
der normale, der First-Order-AT-ST (`s1/…/nt_fo_at_st/`) und der
ausgeschlachtete (`s9_2/…/atst/`), zusammen 11 Meshes und 14,8 MB. Die
Namenssuche findet sie alle, weil „atst" in jedem Pfad steckt.

**0.96.0:** Neu `IstKernTeil` — ein Teil zählt nur, wenn sein Pfad mit
`gameplay/vehicles/` beginnt **und** ein Ordnerteil das Fahrzeug selbst ist.
Damit bleiben die anderen Fassungen draußen (das Log nennt sie „andere
Fassung"). Findet sich kein solcher Kern, gilt wie bisher alles Eigene. An den
echten Pfaden beider Fahrzeuge geprüft.

### Fahrzeuge im Importfenster (0.97.0)

**0.96.0 bei DH:** AT-ST jetzt **3 MeshSets, 32 Bones, 5 Meshes, 5,1 MB** — nur
noch die eigene Fassung (Kopf-Cluster, Cluster-Box, Rumpf+Kopf aus
`atst_static_donotuse_mesh`); die First-Order- und die ausgeschlachtete Fassung
sind raus. AT-RT unverändert 8/53/9. Damit ist Stufe 2 fertig.

**Stufe 3:** Fahrzeuge stehen jetzt **im Importfenster**. Neue Art
`Art::Fahrzeug` und ein sechster Reiter **„Vehicles"** (Heroes · Light side ·
Dark side · Other · Vehicles · All); Einträge heißen `vehicle: ground/at-st`.
Der Import baut sie aus ihren MeshSets mit dem eigenen Skelett (dieselben
Regeln wie `--extrahiere`: eigener Ordner, kein Wrack, statische Fassung nur
ohne Hauptmesh) und lässt Waffen und Gesichtspose aus.

### Fenster breiter, Fahrzeugtexturen (0.98.0)

**Bei DH (0.97.0):** 97 Fahrzeuge im neuen Reiter, Import läuft — aber zwei
Sachen fehlten. **(1) Zu eng:** Sechs Reiter plus „1st person" passten nicht in
380 dlu; „1st person" lag auf „All". Jetzt **440 dlu breit**, alle Reiter
gleich breit (66 dlu), „1st person" rechts in der Suchzeile, Liste und
Fußzeile mitgewachsen. **(2) Keine Texturen bei Fahrzeugen:** `m.quelle` stand
auf dem Anzeigenamen („vehicle: ground/at-st"), also fand
`fbmaterial::Loese` kein Bundle. Jetzt bleibt das **Bundle des ersten
gebauten Teils** die Quelle — darüber greift dieselbe MeshVariationDatabase
wie bei Figuren; ein Hinweis nennt Fahrzeug, Teilzahl und Bundle.

### Fahrzeugtexturen: jedes Teil hat sein eigenes Bundle (0.99.0)

**0.98.0 bei DH:** Der Transporter (`vehicle: air/aalstormtroopertransport`,
6 MeshSets) importiert sauber, und die Materialzuordnung greift jetzt — aber
nur für das **erste** Teil: die sieben Materialien von `…_base_mesh` stehen
„ueber guid" mit ihren Texturen (`t_aalstormtroopertransport_01_cs`,
`…_nma`), die 30 der übrigen Teile (`inflight`, `landinggear`, `mesh`, `ramp`)
„ueber NICHTS".

**Ursache:** `Loese` liest nur die EBX **eines** Bundles. Fahrzeugteile liegen
aber in verschiedenen Level-Bundles, und die MeshVariationDatabase eines Teils
steht nur in dessen eigenem. **0.99.0:** Das Modell merkt sich in
`weitereBundles` jedes Bundle, aus dem ein Teil kam; `Loese` liest sie alle.

### Animationen auf Fahrzeugen und Teilszenen (1.00.0)

**Bei DH:** In einer Szene mit einer Frontend-Figur („gunner", Quelle
`levels/frontend/collection`) wurden 130 Clips entpackt, dann:
„No clip of the selection fits this skeleton". Ursache: `Passt()` verlangte,
dass **80 % der Clip-Spuren** im Szenenskelett vorkommen. Das stimmt für
Figuren (248 Bones), scheitert aber bei Fahrzeugen und Teilszenen — die
gemeinsamen Clips haben über hundert Spuren, ein AT-TE oder eine
Frontend-Figur nur wenige Dutzend Bones.

**1.00.0:** Ein Clip passt jetzt auch, wenn er **die meisten Bones der Szene**
bedient (≥ 60 %, mindestens 4) — dann läuft, was da ist. Die alte Regel gilt
weiter. Schlägt es doch fehl, nennt die Meldung die beste Trefferzahl, die
Spurenzahl des Clips und die Bones der Szene. Regel hier an sechs Fällen
geprüft (Figur, Fahrzeug, Teilszene, Grenzfälle).

### Fahrzeugtexturen, zweiter Anlauf (1.01.0)

**1.00.0 bei DH:** Die Materialzuordnung greift jetzt beim ersten Teil —
`…_base_mesh` bekommt seine sieben Materialien „ueber guid", sechs Texturen
werden geschrieben (2048×2048, BC7), OpenPBR baut 7 von 7. **Aber:** von
28 Meshes haben nur 6 Texturen; `inflight`, `landinggear`, `mesh` und `ramp`
stehen weiter „ueber NICHTS" (MVDB-Einträge 604, MeshAssets 644).

**Ursache:** `weitereBundles` blieb leer — alle sechs Teile kamen aus
**demselben** Level-Bundle (`jakku_01/fantasybattle`), die
MeshVariationDatabase der übrigen fünf steht aber in **anderen** Bundles.
**1.01.0:** `Loese` nimmt jetzt zusätzlich die Bundles **jedes einzelnen
Meshes** dazu (der Index kennt sie zu jedem MeshSet). Die Protokollzeile nennt
die Zahl der gelesenen Bundles.

### Fahrzeugtexturen: Ordnerweg statt MVDB (1.02.0)

Zwei Anläufe über die MeshVariationDatabase haben die übrigen Teile nicht
erreicht. **1.02.0 macht zweierlei:**

**Diagnose:** Bleibt ein Material ohne Zuordnung, steht jetzt in der
Protokollzeile, woran es lag — ob das MeshAsset unter den gelesenen EBX war,
ob der Index es überhaupt kennt und in wie vielen Bundles es liegt. Dazu die
Zahl der gelesenen Bundles in der Summenzeile.

**Ordnerweg (wie bei Waffen):** Fehlt die MVDB, holt `Loese` die Texturen aus
dem Ordner des Meshes — `t_…_cs` als Farbe, `t_…_nma`/`_nam`/`_nm`/`_n` als
Normalen. Genau so heißen DHs Transporter-Texturen
(`t_aalstormtroopertransport_01_cs`, `…_01_nma`). Die Regel ist hier an diesen
Namen geprüft: die beiden Farbtexturen des Ordners werden gefunden, ein
Unterordner und ein fremdes Fahrzeug bleiben draußen. Im Protokoll erscheinen
solche Materialien als „ueber ordner".

### Fahrzeugtexturen: richtiger Satz je Material (1.03.0)

**1.02.0 bei DH: die Texturen kommen an.** 26 von 28 Meshes haben welche
(vorher 6), „ueber NICHTS" steht **nirgends** mehr — 8 über die MVDB, 20 über
den Ordnerweg; 12 Texturen geschrieben, 14 OpenPBR-Materialien gebaut. Die
Diagnose zeigt auch, dass die zusätzlichen Bundles wirken (31 gelesen,
MVDB-Einträge 604 → 1 778).

**Aber der Ordnerweg nahm immer den ERSTEN Satz:** Der Ordner enthält
`_01_`, `_02_` und `_03_` (je `_cs` und `_nma`), und jedes Material bekam
`…_01_`. Deshalb sieht das Schiff falsch aus, obwohl Texturen da sind. Der
Materialname sagt aber, welcher Satz gemeint ist
(`z_m_lowpoly_mapped_textureset2` → `_02_`). **1.03.0** wertet das aus; an
DHs echten Namen geprüft (4 Fälle, 0 Fehler). Außerdem zeigt die Fahrzeugliste
jetzt die Zahl der Texturen statt „0 textures".

## BUILD.bat (1.04.0)

Baut **nur das Plugin** und installiert es — ohne Index, Messungen und
Gegenproben (dafür bleibt START.bat). Inkrementell: eingerichtete
`build_<Jahr>`-Ordner werden weiterverwendet, CMake läuft nur beim ersten Mal.
Mit einem Jahrgang als Parameter nur diesen bauen, z. B. `BUILD.bat 2027` —
das ist der schnelle Weg beim Ausprobieren. Protokoll in `BUILD.log`; bei einem
Fehler werden die Fehlerzeilen gezeigt und eine alte `.dlu` weggeräumt, damit
Max nicht eine Fassung lädt, die nicht mehr zum Quelltext passt.

### Boba Fett: Vorgaben und fehlender Waffentyp (1.05.0)

**DH:** Boba Fetts Liste stimmt nicht — viele Jetpack-Animationen fehlen, und
er bekommt **Luke-Clips**.

**Erklärung 1 (behoben):** An einem IndexChooser mit 47 Plätzen steht auf den
meisten die **Vorgabe** (Luke bzw. Rey); nur Helden mit eigener Animation haben
dort ihren Clip. Boba hat keine eigenen Nahkampf-Clips — auf seinem Platz 3
steht also Lukes Clip, und der Filter rechnete ihn Boba zu. **1.05.0** erkennt
den Mehrheitswert einer Auswahlliste als Vorgabe und rechnet ihn der Figur
nicht mehr zu (der Ast wird weiter verfolgt, nur nicht mehr zugeschrieben).
An einer nachgebildeten Liste geprüft: eigene Clips von Luke, Vader und
Grievous bleiben, Bobas Vorgabe fällt weg.

**Erklärung 2 (offen):** Bobas 3p-Clips (Jetpack, Blaster) hängen im
`.3P.Top.SF` hinter **WeaponType** — und Boba schreibt in seinen EBX **keinen
Waffentyp** (nur HeroCharacter 3 und eigene Bools wie Jetpack, RocketFire).
Deshalb öffnet dieser Ast für ihn nicht. Z11 zeigt jetzt je Figur, **welche
Weichen abgefragt werden, zu denen sie keinen Wert schreibt** — damit lässt
sich entscheiden, ob man solche Äste für Helden ohne Waffentyp ganz öffnet.

### Laborprobe zu 1.05.0 und der Rest bei Boba (1.06.0)

**Z11 mit 1.05.0 bei DH — die Helden sind unberührt:** Vader 30 Clips (30 mit
eigenem Namen), Grievous 25 (25). Die Vorgaben-Erkennung hat also nichts
weggenommen, was wirklich ihnen gehört. **Boba: von 28 auf 7** — vier eigene
Emotes (`E_Boba_01_Price`, `_02_HitThem`, `_03_Escape`, `_04_Cargo`), dazu drei
Reste (`T_Luke_RunFwdToStop_01`, `P_Luke_StandIdle_01`,
`L_JediM_Stand_Idle_Blocking_01`). **1.06.0** wirft Clips weg, die den Namen
eines **anderen** Helden tragen (an neun echten Fällen geprüft); der generische
`JediM`-Clip bleibt.

**Und die Erklärung für die fehlenden Jetpack-Clips steht schwarz auf weiß:**
Die neue Zeile „ohne eigenen Wert, aber abgefragt" nennt für Boba
**`WeaponType.EnumGS` (91×)**. Han und Iden schreiben diesen Wert und bekommen
darüber genau die vermissten Clips (`A_HM_Jetpack_FWD1_Soft_Exit`,
`A_HM_Rifle_Jump_…`, `T_HM_DualPistol_…`); Boba schreibt ihn nicht, also bleibt
der Ast zu. Vader fehlt derselbe Wert (68×) — bei ihm zu Recht, er hat keinen
Blaster. Die Äste für Helden ohne Waffentyp pauschal zu öffnen, würde deshalb
Vader Gewehrclips geben; das wäre nur für Boba richtig und bleibt eine
Entscheidung für DH.

### Z12: den Waffentyp über die Verweise finden (1.07.0)

**1.06.0 bei DH:** Boba behält nur noch seine vier eigenen Emotes und einen
generischen Jedi-Clip; Han verliert zwei fremde Luke-Clips (36 → 34); Vader
(30/30) und Grievous (25/25) unverändert.

**Der richtige Weg für die fehlenden Clips:** Nicht raten, wer „wie im Spiel"
welche Äste bekommt, sondern den **Verweisen** folgen. Bisher suchte
`LiesWurzeln` EBX nur über **Namensteile** im Pfad — Bobas Blaster und sein
Jetpack heißen aber nicht nach ihm. **Z12** baut deshalb eine Tabelle
Datei-GUID → EBX über alle EBX und läuft von Kit, Spezialisierung und Actor der
Figur über die **Importe** weiter zu Waffen und Gadgets; dort wird nach
`WriteEnumerationGameState` gesucht. Damit ist zu sehen, **welche Datei Bobas
WeaponType setzt** — und der Filter kann ihn wie im Spiel verwenden statt ihn
zu erfinden. Zum Vergleich laufen Han (schreibt ihn) und Vader (hat keinen)
mit.

### Z12, zweiter Anlauf (1.08.0)

**1.07.0 bei DH:** Die GUID-Tabelle steht (155 117 EBX in 39,8 s), aber der
Lauf über die Verweise lief ins Leere — Boba 0 Zustände, Han nur zwei aus einer
Darstellungsdatei. Grund: Die Schranke von 400 Dateien war nach lauter Meshes,
Texturen und Level-Kram erschöpft, bevor Waffen und Gadgets an die Reihe kamen.

**1.08.0:** Verfolgt werden nur noch Dateien, die überhaupt Zustände setzen
können (Waffen, Gadgets, Fähigkeiten, Kits, Figuren, Blueprints); Meshes,
Texturen, Sounds, Effekte, Level und UI fallen weg. Schranke 4 000 statt 400.
Dazu zwei Angaben, die die Frage auch ohne den Lauf beantworten können: die
**Namen der Werte** von `WeaponType.Enum`, `SpecificWeapon.Enum` und
`Game.Character.BodyType.Enum` — und eine Stichprobe, **welche Waffen- und
Kit-Dateien den Waffentyp setzen** (mit Pfad und Wert). Daraus ergibt sich das
Muster, nach dem auch Bobas Blaster seinen Wert bekommt.

### Gefunden: Bobas Waffentyp stand hinter einem Tippfehler (1.09.0)

**Z12 mit 1.08.0 bei DH** (706 s, 563 EBX über die Verweise): Boba Fett setzt
`WeaponType.EnumGS = 0` — und zwar in
**`characters/npc/characters/actor_weapon_bobbafett`**, mit **zwei b**. Ein
Tippfehler im Spiel selbst. Deshalb fand ihn die Namenssuche nie; Han hat
dieselbe Datei korrekt geschrieben (`actor_weapon_hansolo`, ebenfalls
WeaponType 0), Vader schreibt erwartungsgemäß keinen.

**1.09.0:** Ein Pfadwort zählt jetzt auch dann zur Figur, wenn es sich um
**höchstens einen Buchstaben** unterscheidet (erst ab sechs Zeichen, damit
kurze Namen wie `atrt`/`atst` streng bleiben). An neun echten Fällen geprüft.
Damit öffnet sich für Boba der `.3P.Top.SF`-Ast, und er bekommt dieselben
geteilten Bewegungen wie Han — Jetpack, Sprünge, Blaster — zusätzlich zu seinen
eigenen Emotes.

### AT-TE: keine Animationen, Materialien fraglich (1.10.0)

**Bei DH:** Der AT-TE importiert (Beine, Rumpf, Kanonen sind da), aber das
Animationsfenster lädt nichts. Das Log verrät den Grund: „Figur in der Szene:
**left** (aus den Knotennamen geschaetzt)", Rig „frontend" — Fahrzeuge trugen
keinen Vermerk, also riet das Fenster aus den Knotennamen und lag falsch.
Auffällig außerdem: Im Modell stecken `at_te_gunner_mesh…` mit Figurmaterialien
(`M_Gloves_06`, `M_Helmet`) — der Schütze kommt als Teil mit, und die
Rumpfmeshes tragen Behelfsnamen (`aa1`, `bb1` …).

**1.10.0, zwei Dinge:** (1) Fahrzeuge tragen jetzt ihren Namen als Vermerk
(`vehicle: ground/at_te` → `at_te`), damit das Fenster die Szene richtig
erkennt; an drei Fällen geprüft. (2) **Z13** im Labor beantwortet die
eigentliche Frage ohne Namensregel: Es nimmt die **Knochennamen des
Fahrzeugskeletts** und hält sie gegen die **Kanalnamen aller Clips** — was sich
zur Hälfte deckt, gehört dazu. Läuft für AT-TE, AT-RT, AT-ST und den
Zwergen-Spinnendroiden.

### Fahrzeuge HABEN eigene Animationen (1.11.0)

**Z13 bei DH — der Beweis:** Der AT-TE hat seine eigenen Clips, gefunden allein
über die Knochennamen: `C_ATTE_Stand_Walk_FWD`, `C_ATTE_Stand_Turn_InPlace_Left`,
`T_ATTE_Stand_Walk_Fwd_To_Stand_Idle_LeftFoot_02`, `A_ATTE_BigFireStance_Exit_01`,
`Add_ATTE_BarrelRecoil` und drei Geonosis-Kinoclips — in den Bänken
`collection`, `gamemodes` und `outro_team1`.

**Zwei Fehler dabei, beide behoben:** (1) **Laufzeit** — 2 400 s je Fahrzeug,
weil `Kanalnamen` für jeden Clip neu durch alle Rigs suchte; jetzt über
denselben Zwischenspeicher wie `Entpacke`. (2) **Zu viele Treffer beim AT-RT**
(8 860): Sein Skelett heißt Hips, LeftUpLeg, Spine — wie das Menschenskelett,
also galt jeder Figurenclip als Treffer. Gezählt werden jetzt nur Knochen, die
walrus_humanmale **nicht** hat, und jeder Knochen nur einmal (statt `.q` und
`.t` doppelt: „381 von 129"). Regel an nachgebildeten Namen geprüft.

### Die eigentliche Bremse gefunden (1.12.0)

Der Zwischenspeicher aus 1.11.0 reichte nicht — DH musste erneut abbrechen.
**Die Ursache lag tiefer:** `NamenFuer` baute für **jede** Kanalkette und
**jedes** Rig die Menge der DofIds neu auf (`std::set` aus dem Vektor). Bei
~22 000 Clips, vielen verschiedenen Ketten und Hunderten Rigs sind das
Hunderte Millionen Vergleiche.

**1.12.0:** Die DofId-Menge jedes Rigs wird **einmal** gebaut und behalten; die
Suche bricht ab, sobald ein Rig alle DofIds abdeckt. Dazu drei Bremsen im
Labor: Z13 meldet alle 2 000 Clips seinen Fortschritt und hört nach 240 s je
Fahrzeug auf, und Z12 (10 Minuten, Zweck erfüllt) läuft nur noch mit `--alles`.

### Fahrzeug-Animationen im Fenster (1.13.0)

**1.12.0 bei DH: das Tempo stimmt** — der ganze Lauf 203 s statt Stunden, Z13
je Fahrzeug 31–41 s. **Und das Ergebnis ist sauber:**

* **AT-TE: 44 Clips, alle mit 121 von 121 eigenen Bones** —
  `C_ATTE_Stand_Walk_FWD_Turning_Left`, `C_ATTE_Stand_Turn_InPlace_Left/Right`,
  `T_ATTE_Idle_To_Unoccupied_01`, `A_ATTE_BigFireStance_Loop_01`,
  `A_ATTE_Fall_3metres_02` (Bank `collection`).
* **AT-RT: 32 Clips** mit 32 von 34 Bones — `A_ATRT_Death_Walk_FWD_01`,
  `PAdd_ATRT_Stand_Idle_01`, `PAdd_ATRT_Aim_Right90_Down`, `L_ATRT_Falling_01`
  (Bank `sharedbundleanimation_common`).
* Zwergen-Spinnendroide: 0 — seine Clips liegen in seinem eigenen Animationssatz,
  nicht im Clipverzeichnis.
* AT-ST: „kein Skelett gefunden" — in `SucheSkelett` stand noch die alte Regel
  („_ske" nur am Ende), die Fahrzeugliste zählt seit 0.93.0 schon „_ske"
  überall. **Behoben.**

**Eingebaut:** `fbzuordnung::ClipsFuerSkelett` bildet die Clipliste eines
Fahrzeugs aus seinen **eigenen** Bones (ohne die menschlichen), mit Zeitgrenze;
das Animationsfenster nutzt sie, sobald ein Fahrzeug in der Szene steht.

## BERICHT.bat: bekommt wirklich jeder Animationen? (1.14.0)

Eine Zeile **je Figur** und **je Fahrzeug**: wie viele Clips sie bekommen, auf
welchem Weg (Name im Clip / Zuordnung des Spiels), mit Beispielen — und am Ende
die Liste derer, die **leer** ausgehen. Genau die sehen wir uns dann an.

Gerechnet wird mit denselben Wegen wie im Animationsfenster: Figuren über
`LiesWurzeln` + `FolgeFuerZustaende` (gefiltert mit `PasstZumSkelett`),
Fahrzeuge über die eigenen Knochen ihres Skeletts. Damit das in Minuten statt
Stunden läuft, werden die **Kanalnamen aller Clips einmal** eingesammelt (je
Kanalkette, nicht je Clip) und danach nur noch verglichen — die Lehre aus den
Läufen, die DH zweimal abbrechen musste. Aufruf:
`castool --bericht <spielordner>`, Ergebnis `bericht.log`.

### Erster Bericht und was er zeigte (1.15.0)

**1.14.0 bei DH (185 s):** 97 Fahrzeuge und 64 Figurenschlüssel.
**Fahrzeuge:** 5 mit Animationen — AT-AT **54**, AT-ST **69**, AT-TE **44**,
AT-RT **32**, Droideka **53**; die übrigen 92 haben kein eigenes Skelett (starr).
**Figuren:** 31 von 64 mit Clips (Luke 330, Yoda 176, Leia 159, Obi-Wan 152,
Lando 135, Rey 16 — alle 16 aus der Spielzuordnung).

**Zwei Lücken im Bericht selbst, beide behoben:** (1) Einträge ohne
„characters" im Pfad (die vielen `blueprintbundle_sp_kit_…`) fielen ganz raus —
jetzt zählt ersatzweise der letzte Pfadteil. (2) Die 33 leeren Zeilen waren
fast alle Soldaten (`l_assault_newera`, `d_heavy_preq` …): Das Fenster bildet
sie auf ihr Klassenprofil ab, der Bericht tat das nicht. **1.15.0** bildet sie
genauso ab (an elf echten Schlüsseln geprüft), nennt das Profil in der Zeile,
nimmt die Profile selbst mit auf und rechnet je Profil nur einmal.

### Bericht über ALLE Einträge (1.16.0)

**1.15.0 bei DH (119 s):** 352 Zeilen statt 161 — 97 Fahrzeuge und 255
Figurenschlüssel. **136 Figuren mit Clips** (vorher 31): Die Soldaten bekommen
über ihr Profil je 33 (`a1_m1end_ds02_officertutorial` → #officer), Droiden 78.
Fahrzeuge unverändert 5 von 97 (die anderen sind starr).

**Die verbliebenen 119 Leerzeilen sind zum großen Teil gar keine Figuren:**
Level- und Kulissen-Bundles (`a3_m2pil_ds02_s0400_gp`, `art_exterior` mit 779
MeshSets, `collection`, `coop_nt_mc85`). **Aber 40 davon sind echte Figuren**,
die nur anders heißen: `blueprintbundle_sp_kit_hansolo`,
`…_kit_iden_imperial_nohelmet`, `…_buddy_shriv_resistance` — Han, Iden und
Shriv haben ihre Clips längst, nur der Schlüssel passte nicht.

**1.16.0** löst solche Bundlenamen auf (Vorsilben `blueprintbundle_sp_kit_`,
`…_buddy_`, Anhängsel wie `_imperial_nohelmet`, `_gunholstered`) und rechnet
mit dem echten Namen; die Zeile zeigt ihn. An sieben echten Namen geprüft.

### Stand des Berichts (1.17.0)

**1.16.0 bei DH (136 s): 157 Figuren mit Clips** (vorher 136). Die aufgelösten
Bundlenamen wirken — Shriv **617**, Hask **334**, Paldora **201**; Han, Iden,
Luke, Leia und Kylo bekommen über ihre Kit-Bundles dieselben Clips wie zuvor.

**Von den 98 Leerzeilen sind die meisten keine Figuren** (Level- und
Kulissenpakete wie `art_exterior`, `collection`, Missionsabschnitte) oder
Raumschiffe ohne Skelett (`millenniumfalcon`, `xwing_iden`, `tiefighter…`) —
die sind im Spiel starr, da gibt es nichts zuzuordnen.

**Eine echte Lücke blieb: Del und Zay.** Ihre Namen haben nur drei Buchstaben
und fielen durch die Schranke „mindestens vier Zeichen". **1.17.0** lässt drei
Zeichen zu, dafür nur am Wortanfang und mit Wortgrenze — so trifft „del" auf
`EoR_Del_Victory_01`, aber nicht auf „Model" oder „Delta". An sieben echten
Clipnamen geprüft. Offen bleibt `alderaanhonorguard` (3 MeshSets): eine
Nebenfigur, die im Spiel offenbar keine eigenen Clips hat.

### Der Bericht ist sauber (1.18.0)

**1.17.0 bei DH (122 s): 165 Figuren mit Clips.** Del und Zay bekommen ihre
**668** — die Wortgrenze war die Lösung. Von den 90 Leerzeilen sind fast alle
**keine Figuren**: `deathstar02_01` (599 MeshSets), `fosd_serverroom` (672),
`gamemodes` (706), Kamino, Naboo, Felucia, die Heldenmodi, die Abspannpakete.
Echte Figuren ohne Clips: nur noch **alderaanhonorguard** und **isbagent**;
dazu die Raumschiffe ohne Skelett (im Spiel starr).

**1.18.0, zwei Ergänzungen:** (1) Level-, Spielmodus- und Kulissenpakete werden
als **KULISSE** gekennzeichnet und aus der Lückenliste genommen (Faustregeln:
bekannte Wörter, Planeten-/Abschnittsmuster wie `felucia_01`, `a3_m2pil_…`,
und alles mit über 300 MeshSets). (2) Bei zusammengesetzten Namen zählt auch
der **hintere Wortteil** ab sechs Zeichen — `alderaanhonorguard` findet so
`EoR_HonorGuard_Victory_01`. Beide Regeln an 13 echten Namen geprüft.

### Der Bericht ist sauber — zwei Fehler beim AT-TE (1.19.0)

**1.18.0 bei DH: 181 Figuren mit Clips, 60 Pakete als KULISSE erkannt, nur noch
14 Leerzeilen** — davon sind 11 ebenfalls keine Figuren (`mc85_logic`,
`skirmish`, `teamdeathmatch`, `venator_logic`, `world_content` …). Echte
Nebenfiguren ohne Clips: `alderaanhonorguard`, `isbagent`, `creatures`.

**Im Test in Max zeigten sich zwei Fehler:** (1) Das Animationsfenster hielt die
AT-TE-Szene weiter für die Figur „gunner" und meldete „Best match: 5 of 80 clip
tracks, scene has 129 bones". Ursache: Der Vermerk landet über `m.quelle` in der
`.fbmodel` — die trägt aber das **Bundle**, weil die Materialzuordnung es
braucht. **1.19.0:** neues Feld `figurVermerk`; die Quelle wird erst **nach**
der Materialzuordnung darauf gesetzt (`vehicle: ground/at_te`).
(2) Die Fahrzeugliste zeigte beim AT-TE „0 textures": gezählt wurde nur im
eigenen Ordner. Jetzt zählen auch Texturen außerhalb — aber nur bei ganzen
Wortteilen, sonst steckt „atte" auch in `sc-atte-rgun`. An fünf echten Pfaden
geprüft.

### AT-TE: Texturen da, Besatzung raus (1.20.0)

**1.19.0 bei DH: die Texturen stimmen** — die Liste zeigt für den AT-TE jetzt
128 MeshSets **mit** Texturen, und das Modell sieht in Max richtig aus.

**Das Animationsfenster meldet aber weiter „gunner".** Das Log erklärt es:
„Figur in der Szene: gunner (aus den Knotennamen geschaetzt), **Quelle
win32/levels/frontend/collection**" — in der Szene lagen noch die Layer
`frontend Bones`/`frontend Meshes` aus einem früheren Import. Die Erkennung
nimmt den obersten Bone einer **anderen** Figur. Dazu kommt: Der AT-TE bringt
`at_te_gunner_mesh` mit — eine eigene Figur mit Figurmaterialien
(`M_Gloves_06`, `M_Helmet`), im Bild als kleiner Klon neben dem Läufer.

**1.20.0:** Besatzungsteile (`gunner`, `pilot`, `driver`, `crew`, `passenger`)
kommen nicht mehr ins Fahrzeugmodell — an fünf echten Namen geprüft. Das Log
nennt jetzt außerdem, **von welchem Bone** der Vermerk stammt, damit so ein
Fall sofort sichtbar ist. Für den Test gilt: **neue Szene**, nur das Fahrzeug
importieren.

### Drei Befunde vom AT-TE-Test (1.21.0)

**1.20.0 bei DH:** Der AT-TE kommt jetzt ohne Schützen und mit Texturen in Max —
aber die Textur sitzt sichtbar falsch, die Figurenauswahl enthält **kein**
Fahrzeug (nur Helden, Soldatenklassen, `gunner (54)`), und das Import-Log war
wieder leer.

**Drei Ursachen, drei Änderungen:**

1. **Fahrzeuge fehlten in der Auswahl.** Die Liste wird aus Clip*namen* gebaut —
   Fahrzeug-Clips tragen aber keinen Figurennamen, ihre Zuordnung läuft über die
   Knochen. **1.21.0** hängt alle Fahrzeuge mit Skelett an die Liste an;
   die Clipzahl wird beim Auswählen gerechnet.
2. **Das Protokoll wurde bei jeder Aktion neu angelegt.** Wer nach dem Import das
   Animationsfenster öffnete, verlor die Material- und Texturzeilen — deshalb kam
   hier mehrfach eine leere Datei an. **Jetzt wird angehängt**, nur beim ersten
   Mal je Max-Sitzung frisch begonnen.
3. **UV-Verdacht.** Neu `UVBEREICH` je Mesh: wie viele UV-Werte es gibt und in
   welchem Bereich sie liegen (u/v von … bis …), dazu die Zahl der Werte im
   zweiten UV-Satz. Damit ist zu sehen, ob die UV-Karte fehlt, außerhalb 0..1
   liegt oder ob das Fahrzeug den zweiten Satz braucht.

### Das Log zeigt die Ursache (1.22.0)

Mit dem vollständigen Protokoll aus 1.21.0 ist der AT-TE-Fehler eindeutig:

```
MATERIAL Zuordnung gescheitert: Bundle nicht gefunden: vehicle: ground/at_te
Material  0 M_Gloves_06   Farbe t_l_assault_preq_01_body_cs.png ...
```

**Ursachenkette:** In 1.19.0 setzte ich den Fahrzeugvermerk als `m.quelle` —
aber **vor** der Materialzuordnung. Die suchte danach ein Bundle namens
„vehicle: ground/at_te", fand keins und brach ab. Weil dabei **kein neuer
Beipackzettel** geschrieben wurde, las Max die `.material.txt` eines
**früheren** Imports — daher die Soldatentexturen (`M_Gloves_06`,
`t_l_assault_preq_01_body_cs`) auf dem Läufer. Es war also nie die UV-Karte:
die `UVBEREICH`-Zeilen zeigen saubere Werte (u/v zwischen 0,000 und 1,000, alle
sieben Meshes mit uv0 **und** uv1).

**1.22.0:** Der Vermerk wird erst **nach** `Loese` und `SchreibeTexturen`
gesetzt; scheitert die Zuordnung, wird die alte `.material.txt` gelöscht, damit
nie wieder Reste eines anderen Imports verwendet werden.

### Ein Unterstrich (1.23.0)

**1.22.0 bei DH: die Zuordnung sitzt.** Alle sieben AT-TE-Materialien stehen
„ueber guid" mit den richtigen Texturen (`t_at_te_aa_cs`, `…_aa_nma`, …), elf
Texturen geschrieben, 0 fehlgeschlagen. **In Max kamen trotzdem nur `detail_emis`
und seine Farbe an** — die anderen sechs zeigten „Farbe - Normal -".

**Der Unterschied steht im Log:** `detail_emis` hat den Parameter `CS=…`, die
anderen `_BaseColor=…` und `_Normal=…` — **mit führendem Unterstrich**.
`fbbeipack::ArtVon` kannte nur „basecolor"/„normal"/„cs"/„nm", also galten sechs
von sieben als „sonst" und landeten in keinem Materialkanal. **1.23.0** schneidet
führende Unterstriche ab; an sieben echten Parameternamen geprüft.

### Fahrzeugclips im Fenster: „at_te (0)" (1.24.0)

In der Auswahl stand `at_te (0)`, und die Liste zeigte weiter alle 21 981 Clips.
**Zwei Gründe:** Die Zahl neben dem Namen wurde für Fahrzeuge als **feste 0**
eingetragen (ihre Clips hängen an den Knochen, nicht am Namen). Und beim
Auswählen lief `ClipsFuerSkelett` **auf dem Fensterfaden** über alle 22 000
Clips — das dauert und lässt die alte Liste stehen.

**1.24.0:** Die Knochennamen aller Clips werden **einmal** eingesammelt und
behalten; danach ist die Clipliste eines Fahrzeugs eine Schnittmenge und sofort
da. In der Auswahl steht `(?)`, solange nichts gerechnet ist, und danach die
echte Zahl. Das Protokoll bekommt eine Zeile je Fahrzeug: Skelett, Zahl der
eigenen Bones, gefundene Clips, Dauer.

### AT-TE steht texturiert in Max (1.25.0)

**1.24.0 bei DH: die Texturen sitzen.** Alle sieben Materialien mit Farbe,
Normalen und Glanz (`t_at_te_aa_cs`, `…_aa_nma_n`, `…_cs_gloss` …), 10 Texturen
neu geschrieben, 7 von 7 OpenPBR-Materialien gebaut — der Läufer ist im Viewport
vollständig bemalt. Auch die Fahrzeuge stehen jetzt in der Figurenauswahl
(`at-at`, `at-st`, `at te`, `atrt`, `droideka 01`, `homingspiderdroid`, `spha t`).

**Das „(?)" ist Absicht** — es steht für „noch nicht gerechnet" und wird beim
Auswählen zur echten Zahl. **1.25.0** macht das sichtbar: Sanduhr und Hinweis
während des einmaligen Sammelns (~30 s), danach wird der Eintrag neu
beschriftet (`at te  (44)`), ohne die Auswahl zu verlieren.

### UV: zweiter Satz als Map-Kanal 2 (1.26.0)

**DH:** Beim AT-TE sitzen die Texturen nur an den **Füßen** richtig, an Kabine
und Rumpf nicht. Die Zuordnung stimmt aber (`aa1`→`t_at_te_aa_*`, `cc1`→`…_cc_*`),
und beide UV-Sätze sind gleich groß.

**1.26.0:** Der **zweite UV-Satz** (`TexCoord1`) kommt als **Map-Kanal 2** mit
in die Szene. Damit lässt sich in Max am Bitmap unter Coordinates der Kanal von
1 auf 2 stellen und direkt vergleichen — ohne neuen Import. Dazu misst das
Protokoll jetzt, **wie viele Vertices sich zwischen den Sätzen unterscheiden**;
sind es null, liegt die Ursache woanders und wir brauchen es gar nicht erst zu
probieren. Die SDK-Attrappe kennt die Map-Kanal-Aufrufe jetzt ebenfalls.

### Fahrzeuge nutzen den ZWEITEN UV-Satz (1.27.0)

**DH hat es in Max nachgestellt:** Mit **Map-Kanal 2** sitzt die Bemalung des
AT-TE überall richtig — Kabine, Rumpf, Kanone —, mit Kanal 1 nur an den Füßen.
Das Protokoll passt dazu: alle sieben Meshes haben **beide** Sätze, und sie
unterscheiden sich fast überall (11 291 von 11 291 Punkten bei `aa1`, 28 679 von
29 447 bei `cc1`).

**1.27.0:** Bei Fahrzeugen kommt `TexCoord1` in den **Hauptkanal**, `TexCoord0`
nach Kanal 2 (zum Vergleichen). Figuren bleiben unverändert. Das Protokoll
schreibt bei Fahrzeugen „(Fahrzeug: uv1 ist Hauptkanal)" dazu.

**Dazu:** Steht beim Öffnen des Animationsfensters ein Fahrzeug in der Szene,
werden seine Clips schon im **Ladefaden** gerechnet — dann steht die Zahl
sofort statt „(?)", und beim Auswählen hängt nichts.

### Fahrzeug-Animationen laufen — aber gegen das falsche Skelett (1.28.0)

**Bei DH (1.26.0):** Die AT-TE-Clips kommen an — „44 Clips, 0 übersprungen,
15 588 Keys, Bereich 0..5430" —, aber der Läufer **kippt um**, und es bewegen
sich nur **8 von 129 Bones**.

**Die Ursache steht im Log:** `RuheFuer` sucht die Ruhelage nur unter
`characters/…_ske`. Fahrzeuge liegen unter `gameplay/vehicles/`, also fiel der
AT-TE auf **walrus_humanmale** zurück — seine additiven Clips wurden gegen ein
Menschenskelett gerechnet. **1.28.0** holt die Ruhelage für Fahrzeuge aus ihrem
**eigenen** Skelett (`fbfahrzeug::SucheSkelett`) und schreibt ins Protokoll,
welches genommen wurde und mit wie vielen Bones.

**Dazu eine Messzeile:** Beim Anwenden steht jetzt, wie viele Spuren der erste
Clip hat, wie viele davon einen Knoten in der Szene finden und welche nicht —
damit die „8 Bones" nachvollziehbar sind, falls es an den Namen liegt.

### Ein Vermerk, der nie in der Datei landete (1.29.0)

**Das Log von 1.28.0 erklärt alle vier Beobachtungen auf einmal.** Es steht dort
`Quelle: win32/levels/frontend/collection` — **nicht** `vehicle: ground/at_te`.
Der Fahrzeugvermerk wurde seit 1.22.0 erst **nach** der Materialzuordnung
gesetzt, die `.fbmodel` wird aber **davor** geschrieben. Also stand in der Datei
weiter das Bundle, und Max wusste nicht, dass es ein Fahrzeug ist:

* UV blieb auf `uv0` statt `uv1` (der Fahrzeugzweig prüft `source`),
* die Szene wurde als „keine Figur erkannt" geführt → `at te` blieb bei „(?)",
* und die Ruhelage kam wieder vom **Menschenskelett** → die Startpose kippt und
  die Clips sitzen falsch.

**1.29.0:** Beim Schreiben der `.fbmodel` steht der Vermerk drin, danach wird
für die Materialzuordnung wieder das Bundle gesetzt. Nebenbefund aus dem Log:
`ANIM Spuren des ersten Clips: 126, davon in der Szene 126` — die Namen passen
also vollständig; die „8 Bones" sind nur die **bewegten** Spuren.

### UV sitzt — jetzt die Ruhelage (1.30.0)

**1.29.0 bei DH: der Vermerk ist da.** `Quelle: vehicle: ground/at_te`, die
UVBEREICH-Zeilen melden „(Fahrzeug: uv1 ist Hauptkanal)", die Layer heißen
`vehicle: ground …` — **Texturen und UV stimmen ohne Handgriff**.

**Die Animationen nicht, und das Log sagt warum:** „ANIM Figur in der Szene:
**left** (aus den Knotennamen geschaetzt)". Die Erkennung liest den Vermerk nur
am obersten Bone der **Auswahl** oder an einem Knoten namens `Reference` — beim
AT-TE gibt es keinen, und ohne Auswahl blieb nur das Raten. Damit war die
Zielfigur „left", also kam die Ruhelage **wieder vom Menschenskelett** (die
Fahrzeug-Ruhelage aus 1.28.0 wird nur für eine erkannte Fahrzeugfigur geholt),
und „at te" blieb bei „(?)".

**1.30.0:** Der Vermerk wird in der **ganzen Szene** gesucht (bis 4 000 Knoten,
erster Treffer mit gültigem Schlüssel gewinnt); das Protokoll nennt den Knoten.

### Der Rigname war „vehicle: ground" (1.31.0)

**1.30.0 bei DH:** Die Szene wird erkannt — „ANIM Figur in der Szene: **at_te**
(Vermerk am Bone Reference)" —, die 44 Clips sind da, alle 126 Spuren treffen
einen Knoten. **Trotzdem kippt die Pose.**

**Was im Log FEHLT, ist der Beweis:** keine Zeile „ANIM Ruhelage fuer at_te:
Fahrzeugskelett …". Der Grund steht zwei Zeilen weiter oben in der Oberfläche:
das Rig heißt **„vehicle: ground"**. `LayerBasis` schnitt aus
`vehicle: ground/at_te` den **vorletzten** Teil heraus (die Regel für
Figurenpfade), und dieser Name landete als Rig-Vermerk in der Szene. Beim
Anwenden fragt `ZielFigur` das **Rig** — also „vehicle: ground" —, dazu gibt es
kein Fahrzeug, und die Ruhelage kam wieder vom Menschenskelett.

**1.31.0:** `LayerBasis` liefert für Fahrzeuge den Namen (`at_te`); Layer und
Rig heißen entsprechend. Zusätzlich verträgt `RuheFuer` auch die lange Form.
An vier echten Quellen geprüft.

### Messzeile für die Startpose (1.32.0)

**1.31.0 bei DH:** Die Layer heißen jetzt `at_te Bones` / `at_te Meshes` — der
Rigname stimmt also. **Die Pose kippt weiter.**

Damit ist klar: Es liegt nicht mehr an Erkennung, Rig oder Ruhelagensuche,
sondern am **Rechnen** selbst. **1.32.0** misst das: Beim Anwenden steht jetzt
im Protokoll, wie weit der **erste Key** von der **Ruhelage** abweicht —
Mittelwert und größter Wert in Grad, mit Knochennamen, dazu ob der Clip
**additiv** ist und welcher Codec. Bei einem Steh- oder Laufclip sind wenige
Grad zu erwarten; große Werte zeigen, ob additiv und absolut vertauscht sind,
ein Vorzeichen kippt oder die Ruhelage doch nicht passt.

### Die Messzeile zeigt es: fünf Spuren statt 126 (1.33.0)

**1.32.0 bei DH:**

```
ANIM Abweichung erster Key zur Ruhelage: Mittel 72,1 Grad, groesste 91,5 Grad
     bei Head (5 Spuren, additiv nein, Codec RAW)
```

**Fünf Spuren.** Verglichen werden konnten nur die Namen, die auch in der
Ruhelage stehen — und das waren menschliche wie `Head`. Die Ruhelage kam also
**immer noch** vom Menschenskelett, obwohl Szene, Layer und Rig „at_te" heißen.

**Die letzte Stelle:** `FigurAusQuelle` bildet die Figur des Rigs und sucht dazu
`characters/` im Pfad. Bei `vehicle: ground/at_te` gibt es das nicht → die
Rig-Figur war **leer**, `ZielFigur` reichte sie weiter, und `RuheFuer("")` fiel
auf walrus_humanmale zurück. **1.33.0** behandelt den Fahrzeugfall dort ebenso
(an vier echten Quellen geprüft). Damit sollten endlich alle 126 Spuren gegen
die Fahrzeug-Ruhelage laufen.

### AT-TE läuft — AT-AT ohne Skin (1.34.0)

**1.33.0 bei DH: der AT-TE ist fertig.** Ruhelage aus `at_te_ske` mit 129 Bones,
**126 von 126** Spuren verglichen, Abweichung erster Key **15,9 Grad** (vorher
72), **129 Bones und 384 592 Keys** statt 8/15 588 — alle 44 Clips auf der
Zeitleiste, keiner übersprungen.

**Beim AT-AT fehlt der Skin,** und das Log sagt warum: Gebaut wurden drei
Fassungen — `at-at_static_donotuse_mesh`, `at-at_landmark_mesh` und
`vehicle_ground_at-at_sp_mesh` —, und bei **allen 18 Abschnitten** steht „keine
Gewichte in der Datei, uebersprungen". Es sind also durchweg **starre**
Fassungen; die geskinnte fehlt in der Auswahl.

**1.34.0:** `landmark` (die Kulissenfassung für die Ferne) fällt raus, und der
Import protokolliert **jeden Anwärter** mit Grund („gebaut", „andere Fassung",
„Wrack/Besatzung/statisch"). Damit ist im nächsten Log zu sehen, ob es für den
AT-AT überhaupt ein geskinntes Mesh gibt und welcher Filter es verwirft.

### Der AT-AT ist gar nicht geskinnt (1.35.0)

**1.34.0 bei DH:** `landmark` ist raus (12 Meshes statt 18), Skelett, Texturen
und Animationen stimmen — **54 Clips, 113 Bones, 231 375 Keys, Abweichung
8,5 Grad**. Aber **alle zwölf** Abschnitte melden weiter „keine Gewichte in der
Datei": weder `at-at_static_donotuse_mesh` noch `vehicle_ground_at-at_sp_mesh`
sind geskinnt. Der AT-AT wird im Spiel also aus **starren Teilen** gebaut, die
an Bones hängen — wie beim AT-ST.

**1.35.0:** Hat ein Teil keine Gewichte, aber **genau einen** Bone-Verweis,
wird sein Knoten an diesen Bone **gehängt** statt übersprungen; das Protokoll
nennt den Bone, die Zusammenfassung die Zahl. Dazu neu die Zeile `STROEME` je
Mesh: welche Vertexströme wirklich da sind (pos, normal, uv0, uv1, boneidx,
bonewgt …) und wie viele Bone-Verweise — damit ist belegt, ob Gewichte fehlen
oder nur ungelesen bleiben.

### Gefunden: Knochenindex ohne Gewichtsstrom (1.36.0)

**Die neue STROEME-Zeile beantwortet es:**

```
STROEME 0 at-at_..._Head_01  pos normal tangent uv0 uv1 boneidx, boneRefs 18, weight 0 Werte
```

**`boneidx` ist da, `bonewgt` nicht.** Jeder Vertex gehört also zu **genau
einem** Bone; das Gewicht ist dann 1,0 und wird gar nicht erst gespeichert —
eine starre Bindung, wie sie für Metallteile üblich ist. Mein Skin verlangte
aber **beide** Ströme und übersprang deshalb alle zwölf Abschnitte. (Der
Notbehelf aus 1.35.0 griff nur bei `Chassi` — dem einzigen Abschnitt mit genau
einem Bone-Verweis.)

**1.36.0:** Fehlt der Gewichtsstrom, bekommt der erste Einfluss **1,0** und die
übrigen 0; gebunden wird über den vorhandenen Knochenindex. Das Protokoll sagt
es je Mesh. An der Gewichtssumme nachgerechnet. Damit sollte der AT-AT einen
vollständigen Skin über alle 113 Bones bekommen.

### Skin da, aber nur ein Drittel gewichtet (1.37.0)

**1.36.0 bei DH:** Die starre Bindung greift — **12 von 12 Meshes** bekommen
einen Skin-Modifier. Aber nur **81 673 von 231 783** Vertices sind gewichtet,
und zwei Abschnitte melden „0 Bones, NICHT MOEGLICH" (Chassi, Leg). Beim Kopf
sind es 9 670 von 22 852 bei genau **einem** Bone — obwohl die Datei **18**
boneRefs nennt.

**Verdacht, messbar gemacht:** Viele Werte im Knochenindex liegen offenbar
**außerhalb** der boneRefs-Tabelle und fielen deshalb weg. **1.37.0** misst je
Mesh die Spannweite der Indizes, wie viele außerhalb liegen und ob die Plätze
1–3 überhaupt belegt sind — und nimmt einen Wert außerhalb der Tabelle
ersatzweise als **Skelettindex** (nur bei starrer Bindung). Das nächste Log
zeigt, ob das die fehlenden zwei Drittel erklärt.

### Alle Vertices gewichtet — und der AT-AT kam doppelt (1.38.0)

**1.37.0 bei DH: der Skin ist vollständig.** `231 783 von 231 783 Vertices
gewichtet, 0 ohne Gewicht` (vorher 81 673). Die Messzeilen belegen die
Kodierung: Platz 0 reicht z. B. **0..59**, die Tabelle hat aber nur **18**
Einträge — die Werte sind also **Skelettindizes**, keine Tabellenplätze. Beim
Chassis steht überall die **1** bei genau einem boneRef; deshalb blieb es vorher
ganz ohne Gewichte.

**Was noch stört: zwei komplette Läufer übereinander.** Weil der AT-AT kein
`<name>_mesh` hat, kamen sowohl `at-at_static_donotuse_mesh` als auch
`vehicle_ground_at-at_sp_mesh` mit — 12 Meshes, doppelte Geometrie, Z-Fighting.
**1.38.0:** Gibt es mehrere vollständige Fassungen, gewinnt die **Spielfassung**
(ohne „donotuse"); die statische kommt nur, wenn es keine andere gibt.

### AT-AT einfach importiert — zwei Befunde offen (1.39.0)

**1.38.0 bei DH:** Nur noch die Spielfassung — **6 Meshes, 115 449 Vertices**,
alle gewichtet, 6 von 6 Materialien mit Textur, 54 Clips mit 231 375 Keys.

**DH sieht aber:** der **Kopf folgt nicht**, und der **Rumpf ist gedreht**.
Passend dazu steht im Log der Ausreißer **180,0 Grad bei `Hips`** (Mittel 8,5).

**1.39.0 misst beides statt zu raten:**
* je Mesh die **Bones, an denen es hängt** (die sechs häufigsten mit
  Vertexzahl) — beim Kopf sind 21 993 von 21 993 Vertices gewichtet, also
  hängen sie vermutlich an den falschen;
* die **drei größten Ausreißer** mit Ruhe- und Key-Quaternion in Zahlen —
  genau 180 Grad sieht nach gekippter Achse aus, nicht nach Bewegung.

### Mein Rückfall aus 1.37.0 war falsch (1.40.0)

**Das Protokoll aus 1.39.0 zeigt es unmissverständlich:**

```
Skin 0 …Head_01  haengt vor allem an:  Reference (9669)  RightBackLowLeg (1710)  LeftBackLegWheelPin (1710) …
Skin 2 …Chassi   haengt vor allem an:  AITrajectory (22259)
```

Der **Kopf** hängt an **Hinterbein-Knochen**. Die Annahme aus 1.37.0 — ein Wert
außerhalb der Tabelle sei der Skelettindex — ist damit **widerlegt**; sie hat
115 449 Vertices gebunden, aber überwiegend falsch. **In 1.40.0 ist sie
zurückgenommen** (solche Vertices bleiben wieder ungebunden, statt falsch zu
hängen).

**Die Werte sind Tabellenplätze in einer anderen Kodierung.** Ein fester Faktor
scheidet aus: Höchstwert gegen Tabellengröße ergibt Kopf 66/16, Bauch 54/17,
Füße 44/19, Decals 49/9 — kein gemeinsamer Teiler. **1.40.0** protokolliert
deshalb die **Rohwerte der ersten sechs Vertices** (alle vier Plätze) und die
**boneRefs-Tabelle** selbst. Daran lässt sich die Kodierung ablesen, statt sie
zu raten.

**Zum gedrehten Rumpf:** `Hips` Ruhe q (0; 0,707; 0; 0,707), Key q
(0,006; **−0,707**; 0,006; 0,707) — dieselbe Achse mit umgekehrtem Vorzeichen,
also exakt 180°. Das ist ein echter Datenwert, kein Rundungsfehler; es klärt
sich, sobald die Bindung stimmt.

### Gelöst: der AT-AT ist ein Composite-Mesh (1.41.0)

**Die Rohwerte aus 1.40.0 und eine neue Messsonde (`castool --meshprobe`)
zeigen es eindeutig:** `vehicle_ground_at-at_sp_mesh` hat `meshTypeId 2` —
es ist ein **Composite-MeshSet** mit 67 starren Teilen, kein geskinntes. Die
„BoneIndices“ im Vertex sind **Teilnummern** (0..66), und die boneList der
Section ist keine Umsetztabelle, sondern nur die Liste der darin vorkommenden
Teile (Kopf: `0 26 27 52 53 55..66` — genau die Werte im Vertex). Frostys
FBX-Exporter behandelt Composite ebenso: Gewicht 1 auf Teil `boneIndices[0]`
(FBXExporter.cs, CadeEvs/FrostyToolsuite 1.0.7). Deshalb passte keine der
Deutungen aus 1.36–1.40.

**Welcher Bone ein Teil bewegt, steht im Fahrzeug-Blueprint**, nicht im
MeshSet: `MultiBodyPhysicsComponentData.Parts[i].TransformNode` zeigt auf ein
`PartComponentData`, das als Kind unter einem `BoneComponentData` hängt. Die
Weltlage jeder `BoneComponentData`-Kette liegt bei **109 von 117 exakt**
(0,0 mm, 0,00°) auf einem Bone der ModelPose; die übrigen sind Zwischenglieder
der Halsringe (0,14–0,55 m, 0°). Das Blueprint wird über den Mesh-Import
gefunden (`IMPORT Mesh <Datei-GUID des Mesh-EBX>`). Die Vertices liegen im
Modellraum; die Teillagen sind nur Drehpunkte (am AT-ST nachgemessen).

**1.41.0, neues Modul `fbteile`:** bei Composite-MeshSets wird `boneRefs` zur
Tabelle *Teil → Bone*; die starre Bindung in Max greift unverändert. Ein
Kind-`BoneComponentData` bekommt nur einen Nachfahren des Eltern-Bones —
sonst hingen die Füße an den IK-Zielen `…FutureFoot`, die in der Ruhelage
deckungsgleich liegen. Ergebnis AT-AT: **67 Teile, 61 exakt, 6 Näherung,
0 ohne Bone**; die Zuordnung ist zeichengleich mit der unabhängigen Sonde
(`--teilbones`, ModelPose). `castool --bindprobe` rechnet die Bindung der
`.fbmodel` ohne Max nach: vorher 43 192 von 115 449 Vertices gebunden (Kopf an
`Reference`, Chassis ungebunden), jetzt 115 449 — Kopf an `Head`/`NeckRotate`/
Kanonen, Chassis an `Spine`, Füße an `…FootDamp`, Beine an den Bein-Bones.
**In Max 2025 bestätigt:** 115 449 von 115 449 gewichtet, Abweichung 0,000000.

**Der gedrehte Rumpf war eine Folge davon.** 1.37–1.40 banden das Chassis an
`AITrajectory` (Wert 1 als Skelettindex). Genau dieser Bone trägt in allen
absoluten AT-AT-Clips eine Gierdrehung von −90°, während `Hips` Identität ist;
in der Ruhelage ist es umgekehrt (gemessen mit `castool --wurzel` an rund
300 Clips). Die **Weltlage** von `Hips` ist in beiden Fällen gleich — die
„180 Grad bei Hips“ der alten Messzeile waren ein Vergleich lokaler Werte.
Die Messzeile vergleicht seit 1.41.0 in Weltlage und nennt den Clip.

**Der Rest der 90 Grad ist ein echter Datenwert.** Der erste Clip der
AT-AT-Liste ist `A1_M4VAR_DS02_S1300_NIS__Char_ATAT__BODY`, eine
Kampagnen-Zwischensequenz: dort trägt `Hips` zusätzlich +90°
(q −0,006; 0,707; −0,006; 0,707) — genau der Key aus DHs 1.39-Log. In dieser
Szene steht der AT-AT also wirklich quer. Gemessen im automatischen Test in
Max 2025: Bild 20–60 (dieser Clip) 90,0° zur Bindepose, alle Gameplay-Clips
danach 0,4–15°; die Lage jedes Probe-Vertex relativ zu seinem Bone bleibt über
alle 54 Clips gleich (größte Drift 0,0004 Einheiten).

**Nebenbei behoben:** Das Figurenfenster überschrieb bei Fahrzeugen die
gesammelten Hinweise (`FAHRZEUGTEIL`-Zeilen) mit denen des ersten Teils und
verwarf die Hinweise aller weiteren Teile; jetzt kommen alle ins Protokoll.

**Offen: AT-ST.** Seine einzige vollständige Fassung
(`atst_static_donotuse_mesh`, Composite, 25 Teile) hat ein Blueprint ohne
Bones; das Spiel-Blueprint (`ground_vehicle_at-st`, 26 Teile) trägt kein
Mesh (kommt über eine Variante). Seine Teile bleiben deshalb ungebunden
(starr in der Ruhelage) — vorher hingen sie an falschen Bones.

**Neue Werkzeuge in castool:** `--meshprobe` (Deutungen der BoneIndices und
Abstand zum Bone), `--ebxfeld` (Felder nach Namen in EBX), `--teilbones`
(Teil → Bone aus dem Blueprint), `--bindprobe` (Bindung einer `.fbmodel`),
`--wurzel` (erster Key der Wurzelkette gegen die Ruhelage), `--ebxguid`
(Datei-GUID je EBX).

### Rückwärts laufen: die Laufrichtung steckt in AITrajectory (1.42.0)

**DH:** Bei Yoda, Obi-Wan und Vader liefen manche Clips falsch, vor allem
„Walk rückwärts“. **Stichprobe mit `castool --clipcheck`** (alle Clips der
drei, jede Spur auf Sprünge, Normen, NaN geprüft): alle RAW, sauber
dekodiert. Der Fehler lag in der **Wurzelkette**:

`AITrajectory` ist die Bewegungsrichtung für KI und Steuerung — keine
sichtbare Drehung; das Spiel setzt sie selbst. In **84 % der absoluten
Clips** (2 778 von 3 324, gemessen an 4 016) steht sie konstant auf −90°. Bei
Vader und Yoda immer. **Obi-Wans Richtungsclips tragen dort ihre
Laufrichtung** (Walk_Bwd +90°, Left 180°, Right 0°, 45er ±45°/±135°), und
seine Verschiebung zeigt in jedem davon nach +Z — der ganze Clip ist um die
Hochachse gedreht. Weil `Hips` unter `AITrajectory` hängt, drehte Max den
Körper mit: rückwärts schaute Obi-Wan nach hinten, seitwärts zur Seite.

**1.42.0 (`fbanim::NormiereLaufrichtung`):** Ist `AITrajectory` in einem
absoluten Clip konstant und ungleich −90°, wird die ganze Lage (Drehung **und**
Verschiebung) um die Differenz gedreht. Animierte Spuren (Turn-Clips) und
additive Clips bleiben unberührt; das Protokoll nennt jeden Clip. Probe:
Obi-Wan rückwärts −Z 3,71 m (Vader −3,80), links +X 3,60 m (Vader +4,28).
**In Max 2025 geprüft** (automatischer Test, Laufclips von Obi-Wan 20,
Vader 20, Yoda 10): alle schauen nach vorn, alle bewegen sich in ihre
Richtung — vorwärts −Y, rückwärts +Y, links +X, rechts −X, Diagonalen passend.
Vader und Yoda waren schon richtig; bei ihnen ändert sich nichts.

**Yodas vier „kanalNN“-Spuren** (31 von 50 Clips) sind verwaist: ihre DofIds
stehen nur in ChannelToDofAssets, in keinem Rig des Spiels
(`castool --suchewert`) — auch das Spiel kann sie keinem Knochen geben. Die
Namenskette sucht fehlende DofIds jetzt trotzdem in allen Rigs (nur wenn sich
die Rigs über den Namen einig sind).

**Nebenbefund Iden:** Die Ruhelagensuche traf für „iden“ das Skelett der
Trident-Drohne (`tr-iden-t`) — Idens Körperknochen hätten keine Keys
bekommen. Jetzt gilt die Fahrzeug-Ruhelage nur für Fahrzeuge der
Fahrzeugliste, und eine Figur braucht einen eigenen Ordner im Pfad
(`…/yoda/…`). Grievous, Bossk und Yoda behalten ihr Skelett
(`castool --skeletttest`).

**AT-AT mit diesem Stand erneut in Max 2025 geprüft:** 115 449 von 115 449
Vertices gewichtet, Drift relativ zum Bone ≤ 0,0004, 54 Clips.

### Gesamttest in Max 2025: Befunde und Korrekturen (1.43.0)

Ein automatischer Lauf importiert jedes Modell der Figurenliste (Reiter All,
ohne Ich-Ansicht: 111 Helden, 97 Fahrzeuge, 101 Dunkle Seite, 872 Helle
Seite, 414 Sonstige) in Max 2025, lädt mit „Load all to timeline“ alle Clips
der vorgewählten Figur und misst in MAXScript: Skin gegen Mesh in der
Bindepose, Materialien, je Sequenz Längen der Körperknochen, Sprünge je Bild
der Hauptknochen, Hülle des verformten Meshes, Laufrichtung. Derselbe
Animationssatz (Figur + Clipzahl) wird einmal vollständig geprüft, danach je
Modell mit den Laufclips als Stichprobe. Die ersten Modelle brachten drei
Fehler:

**1. Prozedurale Bones (Bit 0x8000) blieben ohne Gewicht.** Beim ARC Trooper
trugen 8 730 von 45 907 Vertices nur Bone-Indizes ab 32768 (Schulterpolster,
Rücken- und Oberschenkeltaschen, Kopfschmuck); bei seinen Skins war ein Teil
ganz ohne Gewicht (0 von 4 492). Diese Indizes zeigen nicht in die boneList,
sondern auf Bones, die das Spiel zur Laufzeit anhängt
(`renderboneexpressions/verlet_singlebone`) — das Walrus-Skelett hat 248
Bones, die boneList 253. In Max standen die Teile in der Bindepose still,
während der Körper lief. **1.43.0:** jeder prozedurale Bone wird auf den
echten Bone umgelegt, an dem er hängt — aus den Vertices, die ihn mit echten
Bones mischen (größtes Gewicht), sonst der nächste Körperknochen zur Mitte
seiner Vertices. Das Protokoll nennt jede Umlegung (`PROZEDURAL …`). Im Spiel
schwingen die Teile zusätzlich; in Max gehen sie starr mit dem Elternknochen.

**2. Soldatenprofile luden Droiden- und Ich-Ansicht-Clips.** „Soldier:
Assault (rifle)“ fand seine Clips über das Wort „rifle“ — das steht auch in
`B1_Rifle_…`, `B2_Rifle_…` und `1p_Rifle_…`. Auf dem Menschenskelett: Kopf um
262 % gestreckt (B1), Schulter −56 % (B2). **1.43.0:** für Profile gilt
dieselbe Skelettregel wie für die Zuordnung des Spiels
(`fbzuordnung::PasstZumSkelett`), und die Regel erkennt Droiden-Clips jetzt
auch am Namensanfang (`B1_…`).

**3. Keine Vorauswahl bei Figuren ohne Klassenwort.** ARC Trooper (Ordner
`arctrooper`) bekam im Animationsfenster „All characters“ — „Load all“ hätte
21 981 Clips geladen. **1.43.0:** trägt der Figurname kein Klassenwort,
wählt das Fenster das Profil, dessen Klassenwort in den Mesh-Namen der Szene
am häufigsten vorkommt (`l_assault_preq_01_…` → Assault). Ohne jedes
Klassenwort (Alderaan-Ehrengarde) bleibt es bei „All characters“.

**4. Das Figurenfenster las den Index bei jedem Öffnen neu.** Spielkataloge
und Index (97 MB) kamen bei jedem Öffnen wieder von der Platte — gemessen
8,4 bis 11,1 s, auch wenn nur die nächste Figur importiert werden sollte.
**1.43.0:** Spiel, Index und Figurenliste bleiben nach dem Schließen im
Speicher der Max-Sitzung (wie die Animationsdaten seit 0.38.0); ab dem
zweiten Öffnen steht die Liste sofort da („index already in memory“, im
Protokoll `INDEX aus dem Speicher dieser Max-Sitzung`). Ein anderer
Spielordner lädt neu.

**5. Skytrooper bekam das Skelett des B2.** Die Skelettsuche ging vom
Figurenordner eine Stufe hoch — beim Skytrooper (`characters/hero/skytrooper_01/`)
ist das `characters/hero/`, und dort stand als erstes `b2_01_ske` (96 statt 248
Bones, 12 662 Vertices ohne gültigen Bone; in der Bindepose unauffällig, beim
ersten Clip zerrissen). **1.43.0:** hoch nur, wenn der Ordner noch eine Figur
ist (`characters/hero/<name>/`).

**6. Fremde Grundhaltung verbog Han Solos Zähne (Endor).** Die BasePose des
Handmodells einer anderen Figur (`del_act2_02_hands`) brachte 80 Einträge für
Gesichtsknochen mit — Hans Zähne standen 1,9 m aus dem Mund. **1.43.0:** eine
BasePose setzt nur Bones, die das Mesh ihres eigenen Objekts benutzt (samt deren
Vorfahren); das Protokoll nennt die verworfenen (`nicht vom eigenen Mesh benutzt`).

**7. Mesh ohne Gewichte blieb stehen (Boba Fetts Umhang).** Im Spiel
Stoffsimulation, in der Datei keine Gewichte — in Max blieb der Umhang in der
Bindepose, während Boba lief. **1.43.0:** starr an den nächsten Rumpfknochen
(Hüfte, Wirbelsäule, Hals, Schultern), Protokoll `OHNE GEWICHTE`.

**8. B2 bekam das Menschenprofil „Heavy“.** Die Materialien des B2 heißen
`M_D_Heavy_Preq_01_…`; die Profilwahl aus 1.43.0 (Punkt 3) nahm das Klassenwort —
959 von 1 191 Clips passten nicht aufs Droidenskelett. **1.43.0:** Hinweise auf
einen Droiden (`b1_`, `b2_`, `droid`) haben Vorrang.

**9. Ich-Ansicht-Clips bei Helden.** Heldenclips kommen über den Namen, auch
`Iden_A3_Idle_1P_Aim_Idle` — auf der Außenansicht kleiner Finger um 3 759 %
gestreckt. **1.43.0:** Clips mit dem Namensteil `1P` nur, wenn eine Ich-Ansicht in
der Szene steht.

**10. Alle zwölf Piloten scheiterten** („kein Teil des Fahrzeugs konnte gebaut
werden“): Der Besatzungsfilter warf jedes MeshSet mit „pilot“ im Namen heraus —
in der Kategorie `pilots` ist der Pilot aber das Modell. **1.43.0:** dort gilt der
Filter nicht, und ein Pilot ohne eigenes Skelett bekommt Walrus.

**11. Separatisten-Droiden auf dem Menschenskelett.** Officer, Heavy und
Specialist der Separatisten tragen den B1-Körper aus
`characters/dark/d_assault_preq/d_assault_preq_01/`; ihr eigener Ordner hat kein
`*_ske`. Sie bekamen Walrus — beim Officer flogen im ersten Clip die Teile
auseinander, der Heavy blieb in der T-Pose. **1.43.0:** dritte Stufe der
Skelettsuche im Ordner der Figurmeshes (nur Figuren-Bundles, nicht Level-Bundles;
betrifft 16 Figuren, alle B1-Varianten). Die Suche liegt jetzt in
`fbauswahl::SkelettFuer`; das Animationsfenster nimmt für die Figur in der Szene
dieselbe Regel für die Ruhelage (`castool --skeletttest --alle` zählt die Herkunft).

**12. Droidenprofil.** Separatisten-Klassen (`d_*_preq`) bekamen das
Menschenprofil ihres Klassenworts („assault“ vor „droid“); das Droidenprofil
nahm Menschenclips mit „droid“ im Namen (`AI_Trooper_DroidShock`,
`C_FlyingDroids_…`) und mischte B1 und B2 (B1-Clips auf dem B2: Wirbelsäule
+169 %). **1.43.0:** `d_*_preq`, `b1…`, `b2…` → Droidenprofil; darin nur Clips
mit dem Namensteil B1 oder B2, und je nach Figur nur B1 oder nur B2
(B2 394 Clips, B1 719). `B1OfficerCapture` (ohne Unterstrich) gilt jetzt auch
als Droidenclip. Nachprüfung: B2, B1-Assault (auch Geonosis), Officer, Heavy,
Specialist — 0 Befunde (vorher bis 79 je Modell).

**13. Fahrzeuge bekamen Soldatenprofile, und das Laden hing.** Vulture-Droide
und Droid-Trifighter („droid“ im Mesh-Namen), MC80 („heavy“), Droiden-
Schlachtschiff („droid“ im Figurnamen). Das Plugin brach ab („kein Clip passt“),
schrieb aber nichts ins Protokoll. **1.43.0:** bei Fahrzeugen nur der genaue
Eintrag, keine Profilwahl über Wörter; jeder Abbruch steht als
`ANIM Folge abgebrochen: …` im Protokoll.

**14. Skalierungskanäle in VBR.** Grievous trägt 315 `.s`-Kanäle; der VBR-Decoder
zählte sie als Float. **1.43.0:** als Vector3 wie beim RAW-Decoder. Die 17 nicht
entpackbaren VBR-Clips (14 Grievous, Finn, Heavy-Profil) bleiben — alle haben eine
Palette über 256 Werte, und ihre Konstanten-Karte liegt an anderer Stelle
(Diagnose: `SWBF2_VBR_ROH=<datei>` schreibt die Rohdaten).

**15. BX-Kommandodroide bekam fremde Clips.** Seine 33 eigenen Clips heißen
`A_BX_…`, `C_BX_…`, `EoR_BX_Victory_01` — der Figurname `bxcommanddroid` traf sie
nicht, er bekam das Droidenprofil (B1-Clips: Wirbelsäule +154 %). **1.43.0:**
Kurzform „…commanddroid“ → „bx“. Nachprüfung: 33 Clips, 0 Befunde, größte
Längenänderung 1 %.

**16. VBR mit großer Palette: still falsche Werte.** Messung über 1 698 VBR-Clips:
alle 1 688 mit Palette ≤ 256 entpacken; von 10 mit Palette > 256 nur 2 richtig.
Bei den übrigen liegen Paletten-Indizes hinter der Palette — bisher wurde so ein
Wert still zu 0 (`0f151470b16c6ca8` „ging“, ab Index 943 falsch). Alle scheiternden
tragen zusätzlich Versatzkurven; keine Anordnung der bekannten Bereiche erklärt
ihre Karte, und auch der Frosty-Fork (Virjoinga, `VbrDecompressor.cs`) liest
genau den Aufbau, den wir lesen, und wertet die Karte nicht aus. **1.43.0:** solche
Clips gelten ehrlich als „nicht entpackbar“ (mit Zahl und Stelle der Indizes).
Diagnose: `SWBF2_VBR_STAT=1` (Palette und Ergebnis je Clip),
`SWBF2_VBR_ROH=<datei>` (Rohdaten mit allen Feldern).

**Messartefakte, kein Fehler:** Der AT-AT-Clip `C_ATAT_Stand_Walk_BWD` läuft
vorwärts — alle AT-AT-Laufclips bewegen `AITrajectory` nach +Z (Rohdaten).
Fahrzeuge ohne Skelett (Airspeeder, Schiffe) haben ungebundene Teile in der
Ruhelage — ohne Knochen und Clips richtig.

**Absturz beim Setzen der Keys (Max selbst):** Zweimal mit drei parallelen
Instanzen, nie mit einer. Speicherabzug: Zugriffsverletzung in
`MaxGraphicsObjects` während `StdControl::SetValue` → `NotifyDependents`; der
Wiederholungslauf desselben Modells lief sauber. Beobachtet, nicht geändert
(`DisableRefMsgs` rät Autodesk Plugins ab).

**Offen:** 14 von 181 Grievous-Clips und je einer bei Finn und im Heavy-Profil
(VBR) sind nicht entpackbar („Kanalarten nicht bestimmbar“, „VBR-Block zu kurz“).

**Testschalter für parallele Läufe (1.43.0):** Die Umgebungsvariablen
`SWBF2IMPORT_ABLAGE` (statt `%LOCALAPPDATA%\SWBF2Import`) und
`SWBF2IMPORT_PROTOKOLL` (statt `Downloads\Import swbf2.log`) geben jeder
Max-Instanz eigene Dateien — der Gesamttest fährt damit drei Max-2025-Instanzen
gleichzeitig. Ohne die Variablen ändert sich nichts (`src/swbf2import_ablage.h`).

**Kein Fehler, aber gemessen:** Schnelle Handdreher in Lichtschwert-Angriffen
(Anakin `A_Anakin_Ability1_GuardBreak_Full`: 135,8° zwischen zwei Bildern an
`LeftHand`) stehen genauso in den Rohdaten (`castool --clipcheck`: 135,8° bei
Key 27). `A_HM_Rifle_StandToSprintFwd_01` läuft schon in den Rohdaten nach −Z
(`AITrajectory.t` bis −6,08), die übrigen Vorwärtsclips nach +Z — der
DCT-Decoder ist es nicht (DCT- und RAW-Vorwärtsclips beide +Z).

## Was noch nicht drin ist

- Stufe 4 **Texturen und Materialien** (Messung läuft, siehe oben), Stufe 5
  **Animation**.
- **Skelett je Figur.** Das Fenster nimmt fest `Walrus_HumanMale` (bewiesen an
  Anakin, fbtools und castool tun dasselbe). Für Figuren mit anderem Rig ist
  das falsch; das Protokoll sammelt je Figur die Kandidaten.
- Vbr-Bildblöcke, Material → Textur, Händigkeit, Bildrate (siehe Übergabe).

## Geschichte: Stufe 0

**Stufe 0 lieferte `fbdump.exe`, ein Prüfwerkzeug.** Es gibt
noch keine `.dlu`, kein MAXScript, keinen Menüeintrag, und der Bau rührt das
Max-SDK bewusst nicht an. Wer `START.bat` laufen lässt, bekommt die Bestätigung,
dass der C++-Leser die Dumps genau wie die Python-Referenz liest — mehr nicht,
und mehr soll es an dieser Stelle auch nicht sein.

Der Grund ist derselbe wie beim XFBIN-Importer: erst den Parser gegen eine
Referenz beweisen, dann die SDK-Schicht darauf setzen. Ein falsches Mesh in Max
hat sonst zwei mögliche Ursachen statt einer.

**Ab Stufe 1 entsteht das eigentliche Plugin**: `SWBF2Import.dlu`, ein Eintrag
im Import-Dialog, dieselben Quellen wie in `fbdump`. `START.bat` sagt schon
jetzt, ob das Max-SDK auf dem Rechner liegt.

## Stufe 0 — der Leser (fertig)

    START.bat

Das ist alles. Ein Doppelklick, und die Datei

1. sucht CMake (PATH, sonst das von Visual Studio mitgelieferte),
2. baut `fbdump.exe`,
3. sucht den **fbtools-Ordner selbst** — daneben, eine oder zwei Ebenen höher,
   in Downloads, und auch einen Ordner tiefer, weil entpackte ZIPs oft
   `fbtools-0.45.0\fbtools-0.45.0` ergeben,
4. schickt jede `.fbmodel` und `.fbanim` von dort durch den C++-Leser und
   vergleicht das Ergebnis mit der Python-Referenz.

Das Fenster bleibt am Ende immer offen, alles läuft zusätzlich in `START.log`.
Geht der Bau schief, stehen die letzten 30 Protokollzeilen im Fenster.

`fbdump` liest eine `.fbmodel` oder `.fbanim` und schreibt genau die Textfassung,
die auch `fb_model.dump_text` und `fb_animdump.dump_text` erzeugen. Sind beide
zeichengleich, liest C++ dasselbe wie Python.

**Bereits geprüft, alles an echten Spieldaten:**

- zwölf Animationen aus einem Lauf: **262.281 Zeilen, 0 Abweichungen**; dazu
  Wert für Wert gegen die JSON: 912.776 Werte, 0 Abweichungen
- Anakins Modelldump: 5.414 Zeilen, 0 Abweichungen
- ein Modell mit **acht Bone-Einflüssen** je Vertex (49.008 Vertices,
  85.709 Dreiecke): 134.998 Zeilen, 0 Abweichungen
- 50 Zufallsdateien (25 Modelle, 25 Animationen) mit wechselnden Spalten,
  LODs, Kanalarten, mit und ohne Skinning: **0 Abweichungen**
- gebaut mit `-Wall -Wextra` ohne eine Warnung

Der Leser nimmt Formatfassung 1 und 2. Fassung 2 kann zusätzlich `boneIdx2`
und `boneWgt2` führen: Sections mit `bonesPerVertex = 8` haben zwei Sätze zu je
vier Einflüssen, und erst über beide summieren sich die Gewichte auf 1.

Der Leser liest nie über das Dateiende hinaus: jeder Zugriff prüft die Länge,
und passt etwas nicht, kommt eine Meldung statt Müll.

**64 Bit und Threads:** gebaut wird ausschließlich x64 — CMake bricht bei einem
32-Bit-Bau ab, weil eine 32-Bit-DLL in Max gar nicht lädt. Die Laufzeitbibliothek
steht in jeder Konfiguration auf `/MD` (mehrfädige DLL), auch in Debug; alles
andere führt beim Speicher zwischen Max und Plugin zum Absturz. Eigene Threads
gibt es keine, und das ist gemessen: der größte Dump, den wir haben (49.008
Vertices, 85.709 Dreiecke, 4,9 MB) läuft samt Textausgabe in **0,14 Sekunden**
durch — `fbdump` schreibt die Zeit jetzt mit aus. Warum das Plugin trotzdem
keine Arbeitsthreads bekommt, steht in `CODING.md`.

**Zeilenenden:** die Textfassung ist als reines `\n` festgelegt. Unter Windows
steht `stdout` im Textmodus, dort würde jedes `\n` beim Schreiben zu `\r\n` —
und dann weicht *jede* Zeile ab, obwohl kein einziger Wert falsch ist.
`fbdumptool.cpp` schaltet `stdout` deshalb auf Binärmodus, und der Vergleich
vereinheitlicht zusätzlich die Zeilenenden und sagt es dazu, wenn nur die
unterschiedlich waren.

---

## Werkzeugkette

**Gemessen:** Visual Studio 2026 mit MSVC 14.50 baut alle zwölf Jahrgänge, und
die `.dlu` lädt in Max 2027. Autodesks Blog vom Juli 2026 verlangt dagegen
v14.38 und das Windows-10-SDK — gepinnt wird erst, wenn Max eine `.dlu`
ablehnt. Die Angaben von Autodesk zum Nachschlagen:

| Teil | Was 3ds Max 2027 verlangt | Was VS 2026 mitbringt |
|---|---|---|
| MSVC-Buildtools | **v14.38 (17.8)** aus der v143-Familie | v14.44 |
| Windows-SDK | **10.0.19041.0** (Windows 10) | 10.0.26100.x (Windows 11) |
| C++-Standard | C++20 | — |
| .NET / Qt | .NET 10 / Qt 6.8.3 | — |

Die v14.38-Buildtools stehen im VS-Installer unter *Einzelne Komponenten* in der
Gruppe **Out of Support** („MSVC v143 – VS 2022 C++ x64/x86 build tools
(v14.38-17.8)"). Das Windows-10-SDK 10.0.19041.0 ist dort **nicht** enthalten
und muss getrennt installiert werden. Der Name des Plattformtoolsets bleibt in
beiden IDEs „v143" — entscheidend ist die Unterversion darunter.

Für CMake heißt das:

    cmake -S . -B build -G "Visual Studio 18 2026" -A x64 ^
          -T version=14.38 -DCMAKE_SYSTEM_VERSION=10.0.19041.0

Für Stufe 0 (nur der Leser, ohne Max-SDK) ist das egal, da baut jeder Compiler.

## Stufe 1 bis 5 — das Plugin (1 und 2 laufen)

Dieselben Quellen (`src/fbdump.cpp`) landen später in der `.dlu`, damit Plugin
und Prüfwerkzeug garantiert dasselbe lesen.

| Stufe | Inhalt | Gegenprobe |
|---|---|---|
| 1 | Skelett in Max anlegen (Bones, Hierarchie, Ruhelage) | Bone-Dump aus Max gegen die Python-Referenz |
| 2 | Meshes (Positionen, Normalen, UVs, Dreiecke, Materialien) | Mesh-Dump gegen die Referenz |
| 3 | Skinning über `ISkinImportData`, nur benutzte Bones, Gewichte normalisiert | Gewichtssumme je Vertex, Bone-Zuordnung |
| 4 | Texturen und Materialien | DDS byteweise, Viewport-Anzeige |
| 5 | Animation: Keys je Kanal, Sequenzmodus wie bei AnimMerge | Kurven-Dump gegen die Referenz |

### Was noch zu klären ist

- **Achsen.** Der Dump bleibt im Spielraum (Meter, Y nach oben), Max hat Z oben.
  Frostys eigener FBX-Export dreht um Yaw −90° und schreibt 30 fps. Das ist ein
  Anhaltspunkt, kein Beweis — die Umrechnung wird an einer einzigen Stelle im
  Plugin gemacht und am fertigen Rig nachgemessen.
- **Bildrate.** Steht im `ClipControllerAsset` (`FPS`, `TickOffset`, `TimeScale`).
  Solange sie nicht mitgeliefert wird, steht im `.fbanim` `fps = 0`.
- **Bone-Indizes.** Ob sie lokal (in die Bone-Liste der Section) oder global
  (ins Skelett) zeigen, wird beim Schreiben **gemessen** und im Protokoll
  ausgegeben, nicht angenommen.
- **Material zu Textur.** Die Zuordnung steht noch nicht fest; die Texturliste
  im Dump ist deshalb leer und wird gefüllt, sobald sie gemessen ist.

---

## Fehler, die beim Bauen des Plugins vermieden werden (aus BAF, XFBIN, EG3D)

- `AnimateOn` nicht vergessen — `Control::SetValue` legt sonst still den
  konstanten Wert an, ohne Fehlermeldung. RAII-Wächter ist Pflicht
- Quaternionen bei `CTRL_ABSOLUTE` **nicht** konjugieren, aber das Vorzeichen
  fortlaufend halten (`DotProd < 0` über alle vier Komponenten)
- `ClearKeys` muss rekursiv absteigen — ein Position_XYZ hält selbst keine Keys
- `Create()` muss `new` liefern, kein statisches Objekt (sonst stürzt
  SceneImport beim Öffnen des Dialogs ab)
- Knoten-Handle ist nicht AnimHandle
- Ab Max 2025 liefert `Interface::GetDir` eine `MSTR` als Wert — benannt halten
  und `.data()` benutzen
- `ExecuteMAXScriptScript` hat ab Max 2022 eine andere Signatur
  (`MAX_RELEASE >= 24000`)
- Animationsbereich niemals mit Länge null setzen
- `PackageContents.xml`: `CompanyDetails` und `UpgradeCode` sind Pflicht; beim
  Umbenennen den UpgradeCode ändern **und** die alte Installation entfernen
- Versionsnummer in jede Meldung schreiben

---

## Aufbau

    START.bat               der einzige Einstieg: bauen, pruefen, installieren
    INSTALLIERE.bat         Paket bauen und installieren (von START.bat gerufen)
    CODING.md               Regeln fuer den Code: Max-SDK, C++, Oberflaeche, Sicherheit
    src/fb*.h/.cpp          die Containerschicht (Spiel, Index, MeshSet, EBX, GD, Figur)
    src/fbdatei.*           Dateizugriff: Unicode-Pfade, 64-Bit-Versatz, alles geprueft
    src/fbauswahl.*         die Figurenliste (castool UND Fenster)
    src/swbf2import_*.cpp   das Max-Plugin: Import, DLL, Figurenfenster
    src/swbf2import.rc      Dialogvorlage des Fensters
    scripts/                MacroScript und Menueskripte (bis 2024 / ab 2025)
    package/SWBF2Import/    PackageContents.xml
    tools/castool.cpp       Container, Index, Figuren, Extraktion, --ablage
    tools/fbdumptool.cpp    Dump-Leser, meshtool.cpp  MeshSet/EBX/GD/Umschreiben
    tools/fenster_probe.cpp das Fenster ohne Max (Probelauf)
    tools/VERGLEICHE*.py    die Gegenproben
    tools/PRUEFE_FENSTER.sh Fenster mit MinGW bauen
    tools/sdkstub/          SDK-Attrappe und PRUEFE_QUELLTEXT.sh (Vorabpruefung)
    vendor/                 lz4, miniz, zstd (nur Entpacken)
