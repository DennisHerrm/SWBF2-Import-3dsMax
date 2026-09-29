# Regeln für den Code in diesem Projekt

Kurz und konkret. Alles hier ist entweder aus der offiziellen Autodesk-Doku
(*Best Practices*, *Memory Management*, *Common Problems and Solutions*), aus
den C++ Core Guidelines, oder aus Fehlern, die uns in BAF Toolkit, XFBIN Import
und EG3D Import je einen halben Tag gekostet haben.

---

## 1. Was das Max-SDK vorschreibt

Das sind keine Stilfragen. Wer sich nicht daran hält, bekommt Abstürze, die
schwer zu finden sind, weil sie erst Minuten später auftreten.

**Speicher**

- Klassen und Structs, die mit Max geteilt werden, erben (direkt oder indirekt)
  von `MaxHeapOperators`. Fast jede SDK-Klasse tut das schon, es reicht also
  meist, von einer SDK-Klasse abzuleiten.
- **Kein** eigenes `operator new`, kein Placement-New.
- Heap-Objekte, die nicht von `MaxHeapOperators` erben, mit `MAX_malloc()` und
  Verwandten aus `maxheapdirect.h` anlegen, nicht mit `malloc`.
- Debug- und Release-Laufzeitbibliothek verwalten **getrennte Heaps**. Was im
  einen angelegt wurde, darf nicht im anderen freigegeben werden. Deshalb baut
  man Plugins in der Hybrid-Konfiguration und stellt in Debug die
  Laufzeitbibliothek auf *Multithreaded DLL*.

**Zeiger und Typen**

- `dynamic_cast` statt `static_cast`, und das Ergebnis **immer** auf `nullptr`
  prüfen.
- Schnittstellenzeiger, die Max übergibt, sind oft nur für die Dauer des Aufrufs
  gültig (`IObjParams` in `BeginEditParams`, `INode` in `Display`). Nicht
  aufheben.
- `Interface::ReleaseViewport()` nach jedem `ViewExp`, sonst leckt es.
- In `Tab<>` nur einfache Werttypen oder Zeiger, keine Klassen, die selbst
  Speicher verwalten.

**Sonstiges aus der Doku**

- Keine ungefangenen Ausnahmen aus einem Plugin herauslassen.
- `DbgAssert()` statt `assert()`.
- `TSTR` statt Zeichenpuffer auf dem Stack.
- Dateipfade **nie** als Zeichenkette speichern, sondern als
  `MaxSDK::AssetManagement::AssetUser`.
- `ClassDesc2` statt `ClassDesc`.
- Klassen-IDs mit `gencid.exe` erzeugen, niemals aus einem Beispiel abschreiben.
- `LibVersion()` gehört in die `.def`-Datei, sonst lädt Max die DLL nicht.
- `banned.h` / `3dsmax_banned.h` kennen: markiert unsichere Windows-Funktionen.

## 2. Die Fallen aus unseren eigenen Plugins

- **`AnimateOn` nicht vergessen.** `Control::SetValue` legt nur im
  Animationsmodus einen Key an, sonst setzt es still den konstanten Wert — ohne
  Fehlermeldung. RAII-Wächter (SuspendAnimate + AnimateOn) ist Pflicht.
- **Quaternionen bei `CTRL_ABSOLUTE` nicht konjugieren**, aber das Vorzeichen
  fortlaufend halten (`DotProd < 0` über alle vier Komponenten), sonst nimmt die
  Interpolation den langen Weg.
- **`ClearKeys` muss rekursiv absteigen.** Ein Position_XYZ hält selbst keine
  Keys, sondern drei Float-Controller darunter.
- **`Create()` muss `new` liefern**, kein statisches Objekt. Sonst stürzt
  SceneImport schon beim Öffnen des Dialogs ab.
- **Knoten-Handle ist nicht AnimHandle.** Beides Zahlen, der Compiler schweigt.
- **Ab Max 2025** liefert `Interface::GetDir` eine `MSTR` als Wert; benannt
  halten und `.data()` benutzen.
- **`ExecuteMAXScriptScript`** hat ab Max 2022 eine andere Signatur
  (`MAX_RELEASE >= 24000`).
- **Animationsbereich nie mit Länge null** setzen — SEH-Ausnahme, die kein
  `catch` fängt.
- **Keine Windows-Profil-API im Plugin-Modul:** Ab Max 2019 bringt das SDK
  (`Util/IniUtil.h`) eine gleichnamige `MaxSDK::Util::GetPrivateProfileString(W)`
  („should replace every occurrence of the standard WIN32 implementation").
  Der Aufruf war in 0.47.0 unqualifiziert UND in 0.48.0 mit `::` mehrdeutig
  (C2668, neun Jahrgänge) — `::` hilft also NICHT. Seit 0.48.1 liest das
  Plugin die INI selbst (`LiesIniWert`, UTF-16LE mit BOM oder ASCII/UTF-8).
  Die Attrappe hat ab MAX_RELEASE 21000 zwei Überladungen (long gegen
  unsigned int), zwischen denen jeder Aufruf mehrdeutig ist — Gegenprobe:
  unqualifiziert und mit `::` fallen beide durch. Lehre: ein Fix, der nur in
  der Attrappe grün ist, war in 0.48.0 falsch nachgebildet (using-Direktive
  statt Deklaration).
- **Kein lokales `ok`:** MAXScripts `value.h` legt ein globales `ok` an;
  jedes lokale `ok` im Plugin gibt C4459 (0.45.0: 47 Warnungen). Die
  Attrappe hat das globale `ok`, die Vorabprüfung übersetzt das Plugin mit
  `-Wshadow -Werror=shadow`.
- **`INode::SetNodeTM` bis Max 2021 mit NICHT-konstanter Referenz**
  (`Matrix3&`): 0.43.0 brach mit C2664 in sechs Jahrgängen ab, ab 2022 lief
  es. Die Attrappe bildet beide Stände ab; Gegenprobe: eine `const Matrix3`
  fällt bei MAX_RELEASE < 24000 durch.
- **Materialien:** ohne `SetActiveTexmap` und `MTL_TEX_DISPLAY_ENABLED` zeigt der
  Viewport nichts. Transparenzmap nur setzen, wenn die Textur wirklich einen
  Alphakanal hat, dann mit `SetAlphaSource(ALPHA_FILE)`.
- **Versionsnummer in jede Meldung schreiben.** Sonst sucht man Fehler, die in
  einer alten `.dlu` stecken.

## 3. 64 Bit und Threads

**64 Bit.** 3ds Max gibt es nur als 64-Bit-Anwendung; eine 32-Bit-DLL lädt gar
nicht erst. Der Bau erzwingt das (`-A x64`), und CMake bricht bei einem
32-Bit-Bau mit einer Meldung ab.

**Laufzeitbibliothek.** Bei MSVC ist „multithreaded" keine Wahl: eine
einfädige Laufzeit gibt es seit Visual Studio 2005 nicht mehr. Die Wahl ist
`/MD` (mehrfädige DLL) gegen `/MT` (statisch) und Release gegen Debug. Für
Max-Plugins gilt: **`/MD` in jeder Konfiguration, auch in Debug.** Debug- und
Release-Laufzeit verwalten getrennte Heaps, und was in dem einen angelegt und im
anderen freigegeben wird, stürzt ab. Genau dafür gibt es in den SDK-Beispielen
die Konfiguration *Hybrid*: Debuginfos, aber `/MD`. Steht so in `CMakeLists.txt`.

**Was der Bau zwingend braucht** (übernommen aus XFBIN Import, das denselben
Versionsbereich schon baut):

- **`/Zp8`** — das Struct-Packing des Max-SDK. Stimmt es nicht, liest das Plugin
  die SDK-Strukturen falsch aus, und zwar ohne jede Fehlermeldung.
- Bibliotheken aus `<sdk>/lib/x64/Release`: `core.lib maxutil.lib bmm.lib
  Geom.lib mesh.lib Maxscrpt.lib MNMath.lib Paramblk2.lib comctl32.lib`
- Defines `_UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX`, dazu `/EHsc /bigobj
  /utf-8 /Zc:__cplusplus`
- SDK-Header als `SYSTEM` einbinden und `/external:W0`, sonst ertrinkt `/W4` in
  fremden Warnungen
- `/permissive-` nicht nach Jahreszahl entscheiden, sondern **ausprobieren**:
  einmal `#include <max.h>` damit übersetzen und das Ergebnis im Cache halten.
  Ältere SDKs scheitern selbst daran (strictStrings, rvalueCast)
- Die Endung ist `.dlu`, kein Präfix. Pro Max-Jahrgang eine eigene Fassung —
  eine `.dlu` aus 2024 in 2026 zu laden endet im Absturz, nicht in einer Meldung
- C++17 statt 20, dann übersetzt derselbe Quelltext auch gegen ältere SDK-Header

**Eigene Threads: fast nie.** Die Autodesk-Doku ist deutlich — *3ds Max ist
nicht thread-safe, keine Funktion darf gleichzeitig aus mehreren Threads
gerufen werden.* Das Referenzsystem und die Knotenauswertung sind ausdrücklich
einfädig. Thread-sicher schreiben muss man nur Methoden, die der Renderer
selbst nebenläufig aufruft (`Texmap::EvalColor`, `Mtl::Shade`,
`Atmospheric::Shade`) — für einen Importer trifft das nicht zu.

Was erlaubt ist, steht ebenfalls in der Doku: Ein Importer darf die Arbeit in
**einem einzigen** Arbeitsthread erledigen, während der Hauptthread den
Fortschrittsdialog zeigt. Mehrere Arbeitsthreads gleichzeitig sind es nicht.
Zum Prüfen gibt es `MaxSDK::ThreadingDebuggingTools::IsInMainThread()` und
`IsExclusivelyInMainOrWorkerThread()`; um Code zurück in den Hauptthread zu
bringen, `IMainThreadTaskManager` mit `MainThreadTask`.

**Für unser Plugin heißt das konkret:**

- Das Lesen der `.fbmodel` und `.fbanim` ist reine Rechnerei ohne SDK und dürfte
  beliebig verteilt werden — **lohnt sich aber nicht.** Gemessen am größten
  Modell, das wir haben (49.008 Vertices, 85.709 Dreiecke, 4,9 MB): der ganze
  Durchlauf inklusive Textausgabe braucht **0,15 Sekunden**, und das reine Lesen
  ist nur ein Bruchteil davon. Threads würden hier mehr Fehlerquellen bringen
  als Zeit sparen.
- Alles, was Knoten, Bones, Meshes, Modifikatoren oder Keys anlegt, läuft im
  Hauptthread. Punkt.

## 4. Koordinaten und Matrizen in Max

Nachgeschlagen, nicht geraten:

- **3ds Max ist rechtshändig, Z nach oben, Y in den Bildschirm.** Steht so in der
  SDK-Doku zum Konvertierungsmanager und in der Beispiel-`UserCoord` (X rechts,
  Y in, Z oben).
- **`Matrix3` ist 4×3 und arbeitet mit ZEILENVEKTOREN.** Zeile 0 ist die X-Achse,
  Zeile 1 die Y-Achse, Zeile 2 die Z-Achse, Zeile 3 die Position. Die Doku sagt
  ausdrücklich: „In 3ds Max, all vectors are assumed to be row vectors" — und
  dass `Matrix*Vector` und `Vector*Matrix` mit dem `*`-Operator dasselbe liefern,
  nämlich den mit der Matrix transformierten Zeilenvektor.
- **Kette:** `nodeTM = localTM * parentTM`, also Kind mal Elternteil.
- **`SetNodeTM(0, tm)` setzt die WELTmatrix**, nicht die lokale.
- **Bones anlegen** geht so (steht genau so in den Autodesk-Beispielen):
  `Object* b = (Object*)ip->CreateInstance(GEOMOBJECT_CLASS_ID, BONE_OBJ_CLASSID);`
  dann `INode* n = ip->CreateObjectNode(b);`, `n->SetName(...)`,
  `n->SetNodeTM(0, tm)`, und die Hierarchie über `AttachChild`.
- **Y-oben nach Z-oben** ist eine Drehung um X: `(x, y, z) → (x, −z, y)`.
  Der Ogre-Importer für Max macht es Zeile für Zeile genauso
  (`position.y = -v.z; position.z = v.y;`), und Ogre ist ebenfalls Y-oben.
  Die Determinante bleibt +1, die Händigkeit also erhalten.
- **Achtung Händigkeit:** Der alte Exporter-Trick „y und z tauschen UND Zeile 1
  mit Zeile 2 tauschen" ist etwas anderes — das ist eine Spiegelung
  (Determinante −1) und gilt für linkshändige Ziele wie DirectX. Wer das für
  zwei rechtshändige Systeme benutzt, bekommt ein seitenverkehrtes Modell.

## 5. Zeichenketten im Max-SDK

**3ds Max ist seit 2013 durchgehend Unicode.** `MCHAR` ist `wchar_t`, `MSTR` ist
`WStr`. Der Konstruktor aus `char*` wurde damals **abgeschafft** — deshalb
scheitert `MSTR x(std::string.c_str())` mit `C2665` bei `WStr::WStr` und `C2440`
bei `const char*` nach `MSTR`, und zwar bei jeder Max-Fassung gleich.

Die Doku nennt den Ersatz beim Namen: `WStr::FromACP()`, **`WStr::FromUTF8()`**,
`WStr::FromCP()` oder `WStr::FromMCHAR()`. Unsere Dumps sind UTF-8, also
`FromUTF8`. Gibt es seit Max 2013, deckt also 2016 bis 2027 ab — eine Weiche
nach Jahrgang braucht es nicht.

Zwei Nebenregeln dazu:

- Das Ergebnis in einer **benannten** Variablen halten. `MSTR(...).data()` an
  einem temporären Objekt zeigt auf Speicher, den es beim nächsten Semikolon
  nicht mehr gibt; ab Max 2025 ist die Umwandlung für Rvalues außerdem
  ausdrücklich gelöscht.
- `_UNICODE` und `UNICODE` müssen definiert sein, sonst haben die eigenen
  Funktionen andere Signaturen als das SDK und es hagelt Linkerfehler.

## 6. Vorabprüfung ohne Max-SDK

Unter `tools/sdkstub/` liegt eine **Attrappe** des Max-SDK: dieselben Typen und
Signaturen, aber leer. Damit lässt sich der Plugin-Quelltext auf jedem Rechner
übersetzen — auch ohne Windows und ohne installiertes Max.

    tools/sdkstub/PRUEFE_QUELLTEXT.sh

Sie fängt Tippfehler, falsche Typen und falsche Argumentzahlen, **bevor** der
richtige Bau daran scheitert. Genau die zwei Fehler, die uns je eine Runde
gekostet haben — der fehlende Includepfad und `MSTR` aus `char*` — wären damit
sofort aufgefallen.

Was sie **nicht** leistet: sie beweist nicht, dass das Plugin in Max läuft. Der
echte Bau gegen das echte SDK bleibt die eigentliche Probe.

## 7. Umbenannte und entfernte SDK-Funktionen

Das Max-SDK benennt Dinge um und wirft sie irgendwann weg. Wer über zwölf
Jahrgänge baut, braucht dafür eine Weiche auf `MAX_RELEASE`
(2016 = 18000, 2017 = 19000, … 2027 = 29000).

Bisher aufgelaufen:

| Bis | Ab | Was |
|---|---|---|
| `GetMasterScale` | `GetSystemUnitScale` | 2022 als veraltet gemeldet, **ab 2023 ganz weg** → `C3861` |
| `MSTR` aus `char*` | `WStr::FromUTF8` | seit 2013 kein `char*`-Konstruktor mehr |
| `Matrix3(BOOL)` | `Matrix3` + `IdentityMatrix()` | ab 2025 als veraltet gemeldet |
| `ExecuteMAXScriptScript` alt | neue Signatur | ab 2022 (`MAX_RELEASE >= 24000`) |
| `Interface::GetDir` gab `const MCHAR*` | gibt `MSTR` als Wert | ab 2025, benannt halten und `.data()` |

**Die Lehre daraus:** die Vorabprüfung gegen die Attrappe muss **jeden Jahrgang**
übersetzen, nicht nur einen. `GetMasterScale` ist genau deshalb durchgerutscht —
2016 bis 2022 liefen sauber, 2023 bis 2027 fielen um. `tools/sdkstub/PRUEFE_QUELLTEXT.sh`
geht deshalb alle zwölf `MAX_RELEASE`-Werte durch.

## 8. Das Dezimalkomma

3ds Max stellt die C-Laufzeit auf die **Regionseinstellung des Anwenders**. Auf
einem deutschen Windows schreibt `printf("%.6f")` deshalb `1,000000` statt
`1.000000` — und jede Datei, die so entsteht, ist für ein Programm mit
Dezimalpunkt unlesbar. Genau daran ist unsere erste Bone-Gegenprobe gescheitert:
*„could not convert string to float: '1,000000'"*.

Das SDK hat dafür eine eigene Klasse in `winutil.h`:

    MaxLocaleHandler numericLocaleHelper(LC_NUMERIC, _M("C"));

Sie stellt die Zahlendarstellung für den aktuellen Gültigkeitsbereich um und
setzt sie im Destruktor zurück; die Doku nennt als Zweck ausdrücklich
*„locale independent data to or from a file"*. Wer sie nicht benutzen kann
(ältere SDK-Fassung), macht dasselbe zu Fuß mit `setlocale(LC_NUMERIC, "C")`
und stellt den gemerkten Wert wieder her.

Für die Anzeige in der Oberfläche gilt das Gegenteil: dort erwartet der
Anwender sein gewohntes Komma. Dafür gibt es `GetUserLocale()`, zum Schreiben in
Dateien `GetCNumericLocale()`.

**Regel für uns:** alles, was in eine Datei geht, wird mit erzwungenem
Dezimalpunkt geschrieben. Und die einlesende Seite akzeptiert zur Sicherheit
beides.

## 9. CMake: Sprachen einschalten

`project(Name LANGUAGES CXX)` schaltet **nur C++** ein. Liegen `.c`-Dateien im
Ziel — bei uns die drei Entpacker unter `vendor/` —, werden sie dann **still
übergangen**. Die Bibliothek entsteht trotzdem, enthält aber nur die
C++-Objekte, und erst der Binder meldet die fehlenden Symbole:

    error LNK2019: nicht aufgeloestes externes Symbol "LZ4_decompress_safe"
    error LNK2019: nicht aufgeloestes externes Symbol "mz_uncompress"
    error LNK2019: nicht aufgeloestes externes Symbol "ZSTD_decompress"

Richtig ist `project(Name LANGUAGES C CXX)`. Der Hinweis steht im Bauprotokoll
schon vorher: unter `fbcontainer.vcxproj` taucht nur `fbcas.cpp` auf, keine
einzige `.c`-Datei.

Zwei Sicherungen dagegen:

- Beim Einrichten prüfen, ob **jede** aufgeführte Quelle wirklich existiert.
  Ein unvollständig entpacktes ZIP fällt dann sofort auf, nicht erst beim Binden.
- **Die Vorabprüfung muss einen echten CMake-Bau enthalten.** Eine Attrappe
  prüft nur den Quelltext und sagt nichts darüber, ob das Bauwerk stimmt.
  `tools/sdkstub/PRUEFE_QUELLTEXT.sh` baut deshalb am Ende die Werkzeuge
  wirklich, mit demselben `CMakeLists.txt`.

## 10. Wohin ein Plugin installiert wird

Nachgeschlagen in der Autodesk-Doku „Packaging Plug-ins": 3ds Max durchsucht
genau zwei Orte, plus ab Max 2019 die Pfade in `ADSK_APPLICATION_PLUGINS`.

- `%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins\<name>` (also
  `C:\ProgramData\...`) — der **bevorzugte** Ort, für alle Benutzer, braucht
  Administratorrechte.
- `%APPDATA%\Autodesk\ApplicationPlugins\<name>` — der ausdrückliche
  Ausweichort ohne Adminrechte, nur für den angemeldeten Benutzer.

Die `PackageContents.xml` muss **direkt im ersten Unterordner** liegen. Eine
Ebene tiefer findet Max sie nicht — ein häufiger Fehler beim Entpacken von ZIPs,
die den Ordner im Ordner haben.

Zum Format selbst: 3ds Max braucht `UpgradeCode` (im Gegensatz zu anderen
Autodesk-Programmen), `ProductCode` ist optional. Je Max-Jahrgang ein eigener
`<Components>`-Block mit passenden `SeriesMin`/`SeriesMax`, weil jede `.dlu`
gegen ihr eigenes SDK gebaut ist.

## 11. Oberflächen

Allgemeine Regeln, an denen sich die Oberfläche misst (Nielsens zehn
Heuristiken, seit 1994 der Standard für so etwas). Die sechs, die bei einem
Werkzeug wie unserem wirklich zählen:

- **Zustand sichtbar machen.** Nach jedem Schritt eine Rückmeldung: was wurde
  gefunden, was wurde angelegt, wo liegt das Ergebnis. Stille ist die
  schlechteste Antwort.
- **Wiedererkennen statt erinnern.** Gefundene Dateien auflisten, nicht Pfade
  abtippen lassen. Zuletzt benutzten Ordner merken.
- **Fehler vermeiden statt melden.** Knöpfe grau lassen, solange die Bedingung
  nicht erfüllt ist. Vor dem Zugriff prüfen, ob es die Datei noch gibt.
- **Sprache des Anwenders.** „Figur", „Skelett", „Bones" — nicht `SceneImport`
  oder `Class_ID`.
- **Rückweg offen halten.** Alles, was die Szene ändert, gehört in `theHold`,
  damit Max' Rückgängig greift. Ein Schließen-Knopf, der wirklich schließt.
- **Wenig, aber richtig.** Nur zeigen, was das Werkzeug heute kann. Ein Knopf,
  der nichts tut, ist schlimmer als kein Knopf.

Dazu die Max-eigenen Regeln aus der SDK-Doku: Plugins bringen ihre Oberfläche
entweder im Command Panel, in Rollouts von Material- oder Renderdialogen oder in
freistehenden Fenstern unter. Wer die **Standardsteuerelemente** von Max
benutzt, sieht aus wie Max — das ist mehr wert als eigene Schönheit. Die
automatische Anordnung im Rollout setzt die Elemente untereinander und richtet
sie so aus, wie es die eingebauten Panels tun; davon abzuweichen lohnt selten.

**MAXScript-Falle:** Namen werden STRIKT VON OBEN NACH UNTEN aufgelöst — auch
für Controls, nicht nur für Funktionen. Also erst die Controls, dann die
Funktionen, dann die Ereignisse. Über Rolloutgrenzen hinweg vorher mit `local`
anmelden.

## 12. C++ allgemein

Maßstab sind die [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
(Stroustrup und Sutter). Die wichtigsten Punkte für dieses Projekt:

- **Besitz ist sichtbar.** Wer etwas freigibt, hat es angelegt. Roher `new`/`delete`
  nur dort, wo das SDK es verlangt; sonst `std::vector`, `std::string`,
  `unique_ptr`. In `fbdump.cpp` gibt es genau deshalb kein einziges `new`.
- **Jede Länge prüfen, bevor gelesen wird.** Der `Reader` in `fbdump.cpp` setzt
  bei jedem Fehlgriff ein Flag und liest nie über das Dateiende hinaus. Bei
  einem Format, das aus fremden Dateien kommt, ist das keine Kür.
- **Keine impliziten Umwandlungen bei Größen.** `size_t` gegen `int` ist die
  Quelle für Abstürze bei großen Meshes. Mit `-Wall -Wextra` bauen und die
  Warnungen ernst nehmen; unser Leser baut warnungsfrei.
- **Ein Zweck je Funktion, kurze Funktionen, sprechende Namen.** Kommentare
  erklären das *Warum*, nicht das *Was* — das steht schon im Code.
- **Nichts raten.** Wo das Format unklar ist, wird gemessen und die Messung im
  Kommentar festgehalten (siehe `lod_versatz` in `fb_meshset.py`).

## 13. Wenn das Ergebnis veröffentlicht wird

Kein Rechtsrat, nur was in der Szene üblich und was nachprüfbar ist:

- **`fb_dct.py` ist ein Port aus IceBloc (GPL-2).** Das ist im Kopfkommentar der
  Datei so vermerkt. Bei einer Veröffentlichung zieht das Konsequenzen nach
  sich: entweder das Ganze unter GPL-2 stellen, oder den DCT-Teil ohne Vorlage
  neu schreiben. Alles andere in fbtools ist selbst gemessen.
- **Keine Spieldaten mitliefern.** Werkzeug ja, Modelle, Texturen und
  Animationen aus dem Spiel nein. Das Werkzeug holt sie sich beim Nutzer aus der
  eigenen Installation — genau deshalb ist `START.bat` so gebaut.
- **EA liefert keine Modding-Werkzeuge** und Modding ist streng genommen nicht
  von den Nutzungsbedingungen gedeckt. Die übliche Linie der Szene: alles läuft
  offline, und niemand geht mit veränderten Daten in den Mehrspielermodus — dort
  hat es Bannwellen gegeben, auch bei rein kosmetischen Mods.
- **Auf Nexus gilt der Berechtigungsblock des Autors.** Fremde Arbeit nicht
  weiterverbreiten, immer nennen, wessen Vorarbeit man benutzt hat. Für uns
  heißt das mindestens: Frosty Toolsuite (Container, MeshSet, Texture, EBX) und
  IceBloc (GenericData, Raw/Frame/Dct).

---

*Die Abschnitte 14 bis 20 sind am 10.09.2026 für den Plugin-Schritt
nachgeschlagen worden (Figurenliste und Direktimport im Plugin). Die Quellen
stehen am Ende.*

## 14. Menüs: 2016–2024 gegen 2025–2027

**Bis 2024 (`menuMan`).** Menüs entstehen per MAXScript `menuMan` (in C++
`IMenuManager`) und landen in der Menüdatei des Anwenders. Autodesk empfiehlt,
ein **eigenes Hauptmenü** anzulegen, statt Einträge in fremde Menüs zu schieben.
Das Skript läuft als **post-start-up**, weil das Menüsystem dann schon steht —
genau so macht es Autodesks eigenes Paketbeispiel für diese Jahrgänge. Gegen
doppeltes Anlegen: `menuMan.registerMenuContext <id>` (Autodesks Beispiel) oder
`findMenu` (unser Skript). Beides geht.

**Ab 2025 (`CuiMenuManager`).** Komplett neu geschrieben, Transformationsprinzip,
`menuMan` ist abgelöst:

- Empfohlen sind **Menüdefinitionsdateien (.mnx)** aus dem Menu Editor,
  ausgeliefert unter `"menu parts"` in der PackageContents.xml. Erzeugt werden
  sie im Menu Editor im *Developer Mode* (dann sind nur Grundmenü und Vorlage
  geladen). Was eine `ParentId` aus lauter Nullen genau bedeutet, ist nicht
  belegt — deshalb nicht von Hand schreiben.
- Per Code **nur innerhalb** von `#cuiRegisterMenus` (C++:
  `NOTIFY_CUI_REGISTER_MENUS`). Außerhalb lassen sich Menüs lesen, aber nicht
  anlegen.
- Ein MAXScript, das diesen Callback anmeldet, muss als **pre-start-up** laufen.
  Startreihenfolge: Schritt 3 Pre-Start-Up-Skripte der Pakete → hier läuft
  `#cuiRegisterMenus` → Schritt 4 MacroScripts → Schritt 5 Startup-Ordner und
  **danach** die Post-Start-Up-Skripte. Ein Post-Start-Up-Skript meldet sich
  also zu spät an (so auch Larry Minton, Autodesk, im Forum). Der Callback kommt
  zusätzlich jedes Mal, wenn eine .mnx geladen wird — warum unser Menü trotz
  post-start-up erschien, ist damit **nicht** geklärt, nur möglich.
- In C++: in `LibInitialize` mit `RegisterNotification` anmelden, in
  `LibShutdown` abmelden. Das Plugin darf dann **nicht** verzögert geladen werden
  (`CanAutoDefer` liefert bei uns FALSE — passt).
- Ladereihenfolge: Grundmenü → Plugin-Transformationen (.mnx) → per API
  registrierte → Dateien des Anwenders.
- **GUIDs bleiben fest**, nie zur Laufzeit erzeugen — andere Anpassungen hängen
  daran. Jedes Paket hat eigene: `CreateSubMenu` legt an und liefert bei schon
  belegter GUID `undefined` (BAF 0.2.0).
- MacroScript-Aktion: Aktionstabelle **647394**, Kennung Makroname, Backtick,
  Kategorie; in der .mnx als ``647394-EAfrontImport_Open`EAfront Tool``.
- `LoadConfiguration(GetCurrentConfiguration())` baut **alle** Menüs neu — nur
  zum Entwickeln, nicht als Dauerlösung.
- Neu in 2026: `CuiMenu.CreateMacroScriptAction` (Makro direkt über Name und
  Kategorie) und die Umgebungsvariable `ADSK_3DSMAX_CUI_PRE_USER_CONFIG_<Jahr>`.

**Regel für uns:** 2016–2024 bleibt post-start-up mit `menuMan`. 2025–2027
läuft seit 0.33.0 als **pre-start-up** (eine .mnx wäre die Alternative). Probe: nach der Installation Max
ganz normal starten — das Menü muss da sein, ohne dass jemand den Menu Editor
öffnet.

## 15. Threads im Plugin

**Was Max erlaubt** (SDK, `MaxSDK::ThreadingDebuggingTools`): Animatable-Objekte
anlegen oder löschen und Hold-Operationen gehören normalerweise in den
Hauptthread. In kontrollierten Fällen dürfen sie in **einem einzigen**
Arbeitsthread laufen — das Beispiel der Doku ist wörtlich ein Importer, der im
Hauptthread einen Fortschrittsdialog zeigt und im Arbeitsthread importiert. Die
Aussage in Abschnitt 3 ist damit an der Quelle bestätigt.

**Was wir daraus machen:**

- Der Arbeitsthread rechnet nur, **ohne einen einzigen SDK-Aufruf**: Index bauen
  oder laden, Figur zusammensetzen. Knoten, Bones, Meshes, Modifikatoren, Keys
  und `theHold` bleiben im Hauptthread.
- Zurück in den Hauptthread über einen Timer: das C++-Fenster fragt den
  Arbeitsthread alle 80 ms per `WM_TIMER` ab (bei einem MAXScript-Fenster wäre
  es ein `timer`-Control) — keine Rückrufe aus dem Thread heraus.
  Alternativen laut SDK: ein Windows-Timer am Max-Hauptfenster oder
  `IMainThreadTaskManager` (in der 2023er-Doku belegt; ab welchem Jahrgang,
  nicht geprüft).
- Der Hauptthread wartet **nie** blockierend auf einen Arbeiter, der seinerseits
  den Hauptthread braucht — das ist ein Deadlock (ADN-Beispiel zu Threads in
  Max).
- **Lebensdauer:** ein `std::thread`, der noch läuft, darf nicht zerstört
  werden, sonst `std::terminate` — und Max ist weg. Dasselbe, wenn eine Ausnahme
  die Threadfunktion verlässt. Deshalb: **ein** Besitzerobjekt, `catch (...)`
  im Thread mit Fehlertext als Ergebnis, ein Abbruchflag, `join` beim Schließen
  und spätestens in `LibShutdown`. **Nie** `detach` (Core Guidelines CP.26).
  C++17 hat kein `std::jthread` — das Besitzerobjekt übernimmt dessen Rolle
  (CP.25).
- **Nie** in `DllMain` Threads starten oder auf sie warten — dort gilt der
  Loader Lock (Microsoft). Unser `DllMain` merkt sich nur `hInstance`; so
  bleibt es.
- Geteilt wird fast nichts: Fortschritt und Abbruch als `std::atomic`, das
  Ergebnis wird **einmal** unter einem `std::lock_guard` übergeben (CP.20, CP.2).
- **`std::mutex` und die Laufzeit des Wirts:** seit VS 2022 17.10 ist der
  Konstruktor `constexpr` und ruft die Laufzeit nicht mehr auf. Lädt der Wirt
  (Max) eine ältere `msvcp140.dll` als die, gegen die gebaut wurde, stürzt der
  erste `lock()` ab (`mtx_do_lock`). Abhilfe laut Microsoft:
  `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR` — so auch Firefox, OpenCPN und
  pybind11. Bei uns seit 0.33.1 für alle MSVC-Ziele gesetzt.
- `Interface::ProgressStart` ist **kein** Thread: die übergebene Funktion läuft
  synchron, währenddessen ist der größte Teil von Max stillgelegt, und im
  Erstellen- oder Ändern-Panel geht es gar nicht. Aus MAXScript:
  `progressStart`, `progressUpdate`, `getProgressCancel` (`allowCancel:` ab
  2023).

## 16. 64 Bit konkret

- Unter 64-Bit-Windows sind `int` **und `long`** 32 Bit breit, `size_t` und
  `ptrdiff_t` 64 Bit (Microsoft). Wer Größen oder Versätze durch `int` oder
  `long` schickt, schneidet ab.
- **Dateiversätze:** `fseek`/`ftell` rechnen mit `long` und sind damit ab 2 GiB
  falsch. Unter Windows `_fseeki64`/`_ftelli64`, sonst `fseeko`/`ftello` — und
  **jeden Rückgabewert prüfen**. Befund in 0.32.0: `fbgame.cpp` (`LiesRoh`)
  macht aus dem `uint32_t`-Versatz ein `long` und prüft `fseek` nicht; `ftell`
  als `long` in fbcas, fbindex und fbdump. **Behoben in 0.33.0:** alle Schichten
  gehen über `src/fbdatei` (`_fseeki64`/`fseeko`, UTF-8 → UTF-16 → `_wfopen`).
- `printf("%x")` sieht nur 32 Bit; für `size_t` gehört `%zu` her.
- Win32-Fensterdaten nur über `SetWindowLongPtr`/`GWLP_USERDATA`, Handles in
  zeigergroßen Typen (`AnimHandle`, `ULONG_PTR`) — Lehre aus BAF.
- **Pfade:** schmales `fopen` geht unter Windows über die ANSI-Codepage. Alle
  Schichten öffnen Dateien über **einen** Helfer, der UTF-8 nach UTF-16 wandelt
  und `_wfopen` nimmt (Vorbild `OpenBinaryRead` in BAF); unter Linux `fopen`.

## 17. Sicherheit: Parser für fremde Dateien

Die Parser lesen Daten von außen, und zwar im Max-Prozess. Eine kaputte Datei
(Patch, Mod, Datenträgerfehler) darf Max nie abstürzen lassen und nie Speicher
überschreiben — ein Absturz kostet den Anwender seine Szene.

**Im Code:**

- Jede Länge, jeder Versatz und jede Anzahl aus der Datei wird vor Gebrauch
  geprüft, und zwar überlaufsicher: `p <= n && k <= n - p` statt `p + k <= n`.
- Anzahlen aus der Datei begrenzen den Speicher: `reserve(anzahl)` nur, wenn
  `anzahl * elementgröße` in die restlichen Bytes passt.
- Keine Ausnahme verlässt das Plugin (SDK-Regel) — an jeder Übergabe an Max
  `try`/`catch`.

**Beim Bauen (MSVC):**

- `/W4`, `/sdl` (zusätzliche Sicherheitswarnungen, strengeres `/GS`) und
  `/guard:cf` (Control Flow Guard). `/DYNAMICBASE`, `/NXCOMPAT` und
  `/HIGHENTROPYVA` sind ohnehin Standard. `/sdl` wird wie `/permissive-` erst per
  Probeübersetzung gegen `<max.h>` eingeschaltet.
- `/Qspectre` (braucht eigene Bibliotheken) und `/CETCOMPAT` bringen einem
  Importer lokaler Dateien wenig — vorerst nicht.
- **Stand 0.33.0:** `/guard:cf` und die STL-Härtung sind für alle Ziele an,
  `/sdl` noch nicht (die fremden Entpacker liegen im selben Ziel).
- **STL-Härtung:** seit VS 2022 17.14 macht `_MSVC_STL_HARDENING=1` bestimmte
  undefinierte Zugriffe (etwa `operator[]` außerhalb der Grenzen) zu einem
  sofortigen `__fastfail`; `_MSVC_STL_DESTRUCTOR_TOMBSTONES=1` hilft gegen
  Zugriffe nach dem Freigeben. Beides projektweit definieren. `__fastfail`
  beendet allerdings **Max** sofort. Deshalb: in den Werkzeugen immer, im
  Plugin während der Entwicklung — vor einer Veröffentlichung neu entscheiden.
- `_ITERATOR_DEBUG_LEVEL` bricht die ABI: alle Teile gleich, in Release 0.
  Stufe 1 in Release rät Microsoft ausdrücklich ab.

**Beim Prüfen (Linux, dieselben Quellen):**

- OpenSSF-Satz für GCC/Clang: `-O2 -Wall -Wformat=2 -Wconversion
  -Wimplicit-fallthrough -D_FORTIFY_SOURCE=3 -D_GLIBCXX_ASSERTIONS
  -fstack-protector-strong -fstack-clash-protection`. Der Leitfaden deckt MSVC
  noch nicht ab.
- Alle Gegenproben zusätzlich mit `-fsanitize=address,undefined` — die
  Werkzeuge bauen ja schon unter Linux.
- **Fuzzing** mit libFuzzer: je Format ein eigenes Ziel (`cas.cat`, DbObject,
  Bundle, MeshSet, EBX, GD) — die Doku rät zu schmalen Zielen, eines je Format.
  Ein Ziel muss jede Eingabe vertragen, darf nie `exit` rufen und soll
  deterministisch sein. Startkorpus: die echten Testdateien.
- MSVC-ASan läuft seit VS 2022 17.7 auch in einer Plugin-DLL, deren Host nicht
  mit ASan gebaut ist — für eine gezielte Fehlersuche in Max, nicht als Standard.

## 18. Oberflächen: die Grundlagen in Zahlen

- **Antwortzeiten** (Nielsen): bis 0,1 s wirkt eine Reaktion sofort, bis 1 s
  bleibt der Gedankenfluss erhalten, ab 10 s braucht es eine Fortschrittsanzeige
  mit Prozent. Für uns: Index warm 4 s → Statuszeile mit Zahlen; wird es länger
  → Balken und Abbrechen. Ein Balken darf nie stehen bleiben oder zurückspringen.
- **Abstände** (Microsoft, in Dialogeinheiten): Rand 7, zwischen verwandten
  Elementen 4, zwischen unverwandten 7, Beschriftung zum Element 3,
  Standardhöhe 14. Das Verhältnis ist die Regel: Rand und Abstand zwischen
  Gruppen größer als innerhalb einer Gruppe. Lesefolge von links oben nach
  rechts unten, das Wichtigste oben links.
- **Kontrast** (WCAG 2.2 AA): Text 4,5:1, große Schrift und Bedienelemente 3:1.
  In Max keine Farben hart setzen — das Theme liefert sie.
- **Lange Listen:** eine Suche direkt in der Liste und die Trefferzahl dazu
  (Baymard: bei langen Listen ohne Suche geben Anwender auf). Kategorien
  vorhersehbar benennen, ohne Fachjargon.
- **Unsere Liste konkret** (ausgezählt in figuren.log, 1858 Einträge): Helden
  112, Hell 1166, Dunkel 159, Droiden 2, NPC 1, Sonstige (Level, Modi,
  Seasons) 418. Die 360 Egoperspektive-Bundles (`_bundle1p`) standardmäßig
  ausblenden; Anzeige etwa „112 von 1858".
- Eine hervorgehobene Hauptaktion, Knöpfe grau bis ihre Bedingung erfüllt ist,
  eine Statuszeile mit Plugin- und Skriptfassung (Lehre aus BAF 1.3.1).

## 19. Oberflächen in 3ds Max

**Welche Technik** für 2016–2027 aus einem Quelltext:

| Technik | läuft in | Theme und DPI | Aufwand |
|---|---|---|---|
| MAXScript-Fenster + C++-Kern (Function Publishing) | 2016–2027 | übernimmt Max' Theme, HiDPI ab 2017 | gering, erprobt in BAF und XFBIN |
| Win32-Dialog in C++ | 2016–2027 | Farben selbst über `GetCustSysColor`, Max-Steuerelemente (`ICustEdit`, `ISpinnerControl` …) je mit Release, DPI von Hand | hoch |
| Qt | erst ab 2017, drei Generationen (4.8.5, 5.x, 6.x) | gut | drei Codezweige, 2016 fehlt |
| .NET-Steuerelemente in MAXScript | alle, aber .NET Framework → .NET 8 (2026) → .NET 10 (2027) | mittel | erster Aufruf kostet Sekunden (JIT) |

**Entscheidung (DH, 10.09.2026): das Fenster in C++.** Umgesetzt in 0.33.0 als
modaler Win32-Dialog aus einer `.rc`-Vorlage (Maße in DLU, wächst mit Schrift
und Skalierung). Die Nachteile der Tabelle sind so gelöst: Farben kommen über
eine Brücke aus `GetCustSysColor`, Knöpfe, Liste und Fortschritt sind selbst
gezeichnet, Such- und Listenfeld haben feine Ränder in Max' Farben statt heller
Windows-Striche, im dunklen Theme wird die Titelleiste dunkel
(`DwmSetWindowAttribute`, Attribut 20, sonst 19). Die Kontraste werden gemessen
und protokolliert; reicht die Auswahlschrift nicht, gilt Schwarz oder Weiß.

**MAXScript-Regeln** (aus BAF und XFBIN, gelten weiter): Namen strikt von oben
nach unten — erst Controls, dann Funktionen, dann Ereignisse; `across:N` teilt
in N gleich breite Zellen; zwei Spalten nur über `subRollout`, weil absolute
Positionen nicht mitskalieren; über Rolloutgrenzen hinweg vorher mit `local`
anmelden.

**DPI:** ab 2017 kennt MAXScript `GetUIScaleFactor` und
`applyUIScaling`/`removeUIScaling`. 2016 und 2017 unterstützen 4K laut Autodesk
nicht vollständig — dort nur prüfen, dass nichts abgeschnitten wird.

**Icons:** das Paket setzt **beide** Pfade, `"light icon paths parts"` und
`"dark icon paths parts"`, auch bei gleichen Bildern. Namen `Name.png` oder
`Name_24.png`, `Name_30.png` usw.; Max skaliert nur herunter, also die größte
Stufe mitliefern.

**Warten und Fortschritt:** der MAXScript-`timer` fragt den Arbeitsthread ab; in
einem eigenen Fortschrittsdialog Windows-Nachrichten über
`MaxSDK::WindowsMessageFilter` filtern (SDK-Empfehlung). Schrift in eigenen
Dialogen über `Interface::GetAppHFont()`.

## 20. SDK-Grundlagen für den Plugin-Schritt

- `DllMain` tut fast nichts (`hInstance` merken, `DisableThreadLibraryCalls`).
  Initialisierung gehört in `LibInitialize`, Aufräumen in `LibShutdown`. Beide
  können **mehrfach** kommen — also idempotent schreiben. Liefert
  `LibInitialize` FALSE, entlädt Max die DLL wieder.
- **Der Name einer Core-Schnittstelle darf keinem Klassennamen gleichen**, auch
  nicht bis auf Leerzeichen: MAXScript macht aus `ClassName()` und
  `InternalName()` selbst globale Namen (SDK, „Class Descriptors"). In 0.33.0
  hieß die Schnittstelle `SWBF2Import`, die Importklasse „SWBF2 Import" — und
  die Schnittstelle war aus MAXScript nicht erreichbar. Ob das die Ursache war,
  misst die Diagnose im MacroScript; der neue Name `Swbf2Cpp` folgt `BafCpp`
  und `XfbinCpp`.
- **Widerlegt (0.33.1):** der Name war es nicht — auch `Swbf2Cpp` war nicht
  erreichbar, obwohl die `.dlu` geladen war. Seit 0.33.2 sucht das Menü das
  Interface zusätzlich über `getCoreInterfaces()`, und `LibInitialize` misst
  beim Laden, ob es angemeldet ist (und holt es sonst nach).
- **Eine Diagnose schreibt in ihre EIGENE Datei.** In 0.33.1 ging sie ins
  Import-Protokoll — und der nächste Import hat sie überschrieben.
- **Ein MacroScript, das ein Plugin ruft, misst beim Scheitern selbst**:
  Fehlertext (`getCurrentException`), `getCoreInterfaces()`,
  `importerPlugin.classes`, die geladenen Module über
  `System.Diagnostics.Process` (Pfad und Dateifassung) und
  `logsystem.getNetLogFileName()` — und schreibt es in eine Datei, die
  START.bat einsammelt.
- Eine **Core-Schnittstelle** (`FPStaticInterface`, Flag `FP_CORE`, eindeutige
  `Interface_ID`) meldet sich schon durch ihre Deskriptor-Instanz an; ein
  `RegisterCOREInterface` ist nicht nötig. Die drei Eintragungsstellen (enum,
  `FUNCTION_MAP`, Deskriptor) prüft `PRUEFE_API.py` aus BAF.
- Weitere bestätigte SDK-Regeln (*General Best Practices*): keine ungefangenen
  Ausnahmen; `TSTR` statt Stackpuffer; `DbgAssert`; übergebene
  Schnittstellenzeiger nur während des Aufrufs gültig; Aufräumcode ruft keine
  GUP (die werden zuerst entladen); `MaxHeapOperators` für alles, was über die
  DLL-Grenze geht.
- Das SDK hat STL an seinen Schnittstellen traditionell gemieden, wegen
  gemischter Compiler. Bei uns bleibt STL innerhalb der DLL; zu Max gehen nur
  SDK-Typen.
- **Jahrgänge:** 2025 neues Menüsystem (Abschnitt 14). 2026 .NET 8 für
  .NET-Plugins, kleinere Änderungen u. a. in ActionTable, Animatable, CustCont
  und im Menüsystem. 2027 .NET 10, DirectX 9 samt Headern entfernt, C++20,
  Qt 6.8.3, Deprecations u. a. in object.h und gfx.h. Laut den Versionsmakros
  ist jedes SDK mit dem Vorjahr inkompatibel — eine .dlu je Jahrgang bleibt.
- **Werkzeugkette 2027:** Autodesks Blog vom Juli 2026 verlangt MSVC v14.38 und
  das Windows-10-SDK 10.0.19041.0. Unsere Läufe mit 14.50 bauen und laden —
  gemessen schlägt gelesen; gepinnt wird erst, wenn Max eine .dlu ablehnt.

## 20a. Skinning (seit 0.34.0)

- **Modifikator auf den Knoten: `GetCOREInterface7()->AddModifier(*node, *mod)`.**
  So beschreibt es das SDK („Adding Modifiers to Objects"); Rückgabe ist
  `Interface7::ResCode` (`kRES_SUCCESS` = 0). Von Hand über
  `CreateDerivedObject` ginge es auch — aber `IDerivedObject` und
  `CreateDerivedObject` stehen in **`modstack.h`**, das `max.h` nicht einbindet
  (`inode.h` erklärt `IDerivedObject` nur vorwärts). 0.34.0 brach daran in
  allen zwölf Jahrgängen ab (C3861, C2027, C2664).
- Reihenfolge nach SDK: Skin anlegen (`CreateInstance(OSM_CLASS_ID,
  SKIN_CLASSID)`), per `AddModifier` auf den Knoten, **nur benutzte** Bones mit
  `AddBoneEx` anmelden, den Knoten **einmal auswerten** (`EvalWorldState` — erst
  dabei legt Skin die Daten für diesen Knoten an), dann `AddWeights` je Vertex.
- Bones und Mesh stehen dabei in der Ruhelage; die merkt sich Skin als Bindung.
- Jedes Gewicht wird über `ISkinContextData` **zurückgelesen** und gegen die
  Datei gehalten (normiert, je Bone zusammengefasst).
- Gemessen an Anakin (echte Datei): 40 823 Vertices, **alle** Gewichtssummen
  1,0000, keiner ohne Gewicht, 5 785 Vertices mit mehr als vier Einflüssen
  (bis 1 606 im Kopf, 2 905 im Körper) — der zweite Einflusssatz ist Pflicht.
  Größter Skelettindex 237 von 248, `boneRefs` überall die Identität.

## 20a2. Composite-MeshSets (seit 1.41.0)

- **Erst den MeshSet-Typ ansehen, dann die BoneIndices deuten.** `meshTypeId`
  0 = starr, 1 = geskinnt, 2 = Composite. Nur bei 1 sind die Indizes Plätze in
  der boneList der Section. Bei 2 sind es **Teilnummern**, und die boneList
  nennt nur die vorkommenden Teile (Frosty FBXExporter.cs: Composite →
  Gewicht 1 auf Teil `boneIndices[0]`). 1.36–1.40 haben das drei Runden lang
  geraten.
- Teil → Bone steht im Fahrzeug-Blueprint (`MultiBodyPhysicsComponentData.Parts`
  → `PartComponentData` → Eltern-`BoneComponentData`), gefunden über
  `IMPORT Mesh <Datei-GUID>`. Code: `src/fbteile.*`.
- Beim Zuordnen per Lage **die Hierarchie mitnehmen**: IK-Ziele
  (`…FutureFoot`) liegen in der Ruhelage deckungsgleich auf echten Bones.
- Drehungen **in Weltlage** vergleichen, nie lokal: Skelett und Clips dürfen
  dieselbe Drehung verschieden auf die Kette verteilen (AT-AT: `Hips` gegen
  `AITrajectory`).
- Messwerkzeuge: `castool --meshprobe`, `--teilbones`, `--bindprobe`, `--wurzel`.
- **Bone-Index mit Bit 0x8000 = prozeduraler Bone (seit 1.43.0).** Nicht als
  Platz in der boneList lesen (dann fällt er hinter das Ende und der Vertex
  bleibt ohne Gewicht). `LoeseProzeduraleBones` in `fbfigur.cpp` legt ihn auf
  den echten Elternknochen um. Die Nummer hinter dem Bit gilt je Mesh, nicht
  je Figur (ARC Trooper: derselbe Wert 0 = Schulterpolster im Körper, Gürtel
  im Rüstungsmesh).
- Die Zeile „hängt vor allem an“ zählte bis 1.42 Platz 0, auch mit Gewicht
  0 (ARC Trooper: Füllwert 145 = RightArm). Seit 1.43.0 der Bone mit dem
  größten Gewicht je Vertex.

- **Ablage- und Protokollpfad nur über `swbf2import_ablage.h` (seit 1.43.0).**
  Kein `_wgetenv(L"LOCALAPPDATA")` mehr an Einzelstellen — sonst greift der
  Testschalter `SWBF2IMPORT_ABLAGE`/`SWBF2IMPORT_PROTOKOLL` dort nicht, und zwei
  parallele Max-Instanzen schreiben wieder in dieselben Dateien.
- **Skelettwahl nur über `fbauswahl::SkelettFuer` (seit 1.43.0).** Import und
  Animationsfenster (Ruhelage der Figur in der Szene) benutzen dieselbe Regel:
  `*_ske` im Figurenordner, im Heldenordner (nur wenn das noch ein Figurenordner
  ist, `characters/<art>/<name>/`), dann im Ordner der Figurmeshes (nur
  Figuren-Bundles, nie `characters/heads/`). Prüfen mit
  `castool --skeletttest <spiel> --alle` (Zählung je Herkunft).
- **Profilwahl im Animationsfenster:** exakter Figurname vor Profil;
  Separatisten (`d_*_preq`), `b1…`, `b2…` → Droidenprofil vor dem Klassenwort;
  im Droidenprofil nur Clips mit dem Namensteil B1/B2, je nach Figur nur eines
  davon; Profilwahl über Mesh-Namen nur für Figuren, **nie für Fahrzeuge**
  (Vermerk `vehicle: …`). Clips der Ich-Ansicht (Namensteil `1P`) nur bei einer
  Ich-Ansicht in der Szene. Namensteile immer als ganze Teile zwischen `_`
  vergleichen, nicht als Teilstring (`droidshock` ist kein Droidenclip).
- **Jeder Abbruch von „Load all“ steht im Protokoll** (`ANIM Folge abgebrochen: …`) —
  der Testtreiber und DH erkennen daran, dass nichts passiert ist.
- **VBR: Paletten-Index hinter der Palette = Fehler, nicht 0.** Große Paletten
  (> 256) mit Versatzkurven sind im Aufbau noch unbekannt; lieber „nicht
  entpackbar“ als still falsche Konstanten. Diagnose: `SWBF2_VBR_STAT=1`,
  `SWBF2_VBR_ROH=<datei>`.
- **Fahrzeugkategorie `pilots`:** dort ist die Besatzung das Modell
  (`IstFahrzeugteil(..., besatzungErlaubt)`), Skelett Walrus.
- **Teure Daten sitzungsweit halten:** Animationsdaten (`g_daten`, seit 0.38.0)
  und Spiel/Index/Figurenliste des Figurenfensters (`g_vorrat`, seit 1.43.0)
  leben bis Max endet; nur der Hauptthread übergibt sie beim Öffnen und
  Schließen.

## 20b. Materialien und Texturen (seit 0.35.0)

- **Standardmaterial:** `NewDefaultStdMat()`, Bitmap über `NewDefaultBitmapTex()`
  und `SetMapName` (stdmat.h). Kanalnummern IMMER über
  `StdMat2::StdIDToChannel(ID_DI / ID_BU)` — Shader-Materialien ordnen ihre
  Kanäle selbst (SDK, „Texture Map Indices").
- **Normal Bump:** `CreateInstance(TEXMAP_CLASS_ID, Class_ID(0x243e22c6,
  0x63f6a014))` (imtl.h: GNORMAL_CLASS_ID), Block `gnormal_params` = 0,
  Parameter `gn_map_normal` = 2 — die Normalen-Bitmap mit **Gamma 1,0**
  (`BitmapInfo::SetCustomGamma(1.0f)` + `BMM_CUSTOM_GAMMA`, `SetBitmapInfo`).
  Bump-Menge im Material 1,0.
- **Viewport:** `SetMtlFlag(MTL_TEX_DISPLAY_ENABLED)` und
  `Interface::ActivateTexture(tex, mtl)`.
- **Compact Material Editor:** `Interface::PutMtlToMtlEditor(mtl, slot)` —
  mit Slot 0..23 ersetzt es ohne Rückfrage. Modus über
  `GetCOREInterface13()->SetMtlDlgMode(0)` (mtlDlgMode_Basic = 0), öffnen mit
  `OpenMtlDlg(0)`. Nie umschalten, wenn der Slate-Editor offen ist (das SDK
  nennt zwei Editoren zugleich instabil).
- **Texturen dekodieren** im Arbeitsthread (reines Rechnen, kein SDK), die
  Materialien im Hauptthread.

## 20c. Gemessen: das Kerninterface meldet sich NICHT selbst an

`start.log` aus DHs Max 2027 (0.34.1): „Kerninterface Swbf2Cpp beim Laden: NICHT
angemeldet — nachgeholt mit RegisterCOREInterface: ok". Der statische
Deskriptor mit `FP_CORE` hat sich also, anders als die SDK-Doku sagt, nicht von
selbst angemeldet — das war die Ursache von 0.33.0/0.33.1. Der Nachtrag in
`LibInitialize` bleibt deshalb dauerhaft drin.

## 20d. Animationen in Max (0.38.0, Recherche 10.09.2026 + Messung)

**Gemessen (0.37.1, outro_team1, erster Key gegen die Ruhelage):** Quaternion
in der Reihenfolge x,y,z,w; die Zeilen der lokalen Lage (right, up, forward)
sind die **Spalten** von R(q) — Median 8,3 bzw. 18,4 Grad, alle anderen
Lesarten 36 bis 177 Grad. Verschiebungen lokal in Metern (Median-Abstand zur
Ruhelage 0,0000 m). `TrimmedDuration` ist NICHT die Dauer in Sekunden (gleich
EndFrame) — die Bildrate kommt aus dem ClipControllerAsset.

**Lokal bleibt lokal:** Der Import rechnet `Welt_max = Welt_spiel * A` (plus
Maßstab auf der Verschiebung). Damit ist die lokale Lage jedes Kindes gleich
der des Spiels, nur die Verschiebung mal Maßstab; allein ein oberster Bone
(Eltern = Szenenwurzel) bekommt `* A`.

**Regeln aus der SDK-Doku:**
- Zeit: 4800 Ticks je Sekunde; `SetFrameRate` stellt die Ticks je Bild um.
- Quat und AngAxis der API folgen der **Linke-Hand-Regel**, die Oberfläche der
  rechten. Nie Komponenten kopieren — über die Matrix gehen, `Quat(Matrix3)`.
- Keyframe-Controller speichern Drehkeys **relativ zum vorigen Key**. Wer über
  `IKeyControl` schreibt, muss das selbst umrechnen. Wir nehmen
  `Control::SetValue(t, &q, 1, CTRL_ABSOLUTE)` im Animationsmodus.
- Controller zuweisen bei ausgesetztem Animationsmodus (`SuspendAnimate`),
  Keys mit `AnimateOn`/`AnimateOff` (RAII), am Ende `ResumeAnimate` — das
  stellt Auto Key des Benutzers wieder her.
- Euler XYZ ist laut Doku nicht so glatt wie Quaternionen; für abgetastete
  Spieldaten Linear-Controller (Slerp zwischen Keys, keine Überschwinger).
  Neue Controller ersetzen die alten → jeder Clip löscht den vorigen ganz.
- Vorzeichen der Quaternion fortlaufend halten (nach `Quat(Matrix3)`).
- Bones ohne Kanal bekommen die Ruhelage aus dem Skelett (LocalPose).
- **Folge in der Zeitleiste** (0.39.0) wie der XFBIN-Sequenzmodus: Bindepose
  bei 0, Abstand vor jedem Clip, Ruhe-Keys an beiden Enden für jeden Bone, den
  der Clip nicht bewegt (sonst läuft der vorige Clip über den Abstand in den
  nächsten). Notizspur `animations` auf der **Szenenwurzel** (`rootNode`, dort
  suchen die WC3-Exporter), zwei Keys je Sequenz. Die Notizspur entsteht per
  MAXScript aus C++ (`ExecuteMAXScriptScript`, ab Max 2022 mit
  `MAXScript::ScriptSource` an zweiter Stelle — dieselbe Weiche wie XFBIN).

## 20e. Oberflächen in Max (Recherche 10.09.2026)

- **Wege:** Max bietet für neue Oberflächen Qt (`QmaxMainWindow`,
  `QmaxDockWidget`, `QmaxToolBar` im SDK), dazu die klassischen
  Custom Controls. Wir bleiben bei Win32 mit eigener Zeichnung: das läuft in
  allen Jahrgängen 2016–2027 ohne Qt-Werkzeuge (moc, Qt-Pfade je Jahrgang)
  und ist mit MinGW ohne Max prüfbar. Qt ist der Weg für ein andockbares
  Fenster, falls DH das will.
- **Regeln (swbf2import_ui.h):** Farben nur aus dem Theme (`GetCustSysColor`),
  Töne daraus gemischt statt fest; Schrift gegen Grund mindestens 4,5:1
  (WCAG 2.2), gedämpfte Töne gerade noch 4,5:1; Hauptknopf und eingeschaltete
  Umschalter in der Auswahlfarbe; keine Systemrahmen (hell im dunklen Theme),
  stattdessen 1-Pixel-Kanten in einem Mischton; dunkle Titelleiste
  (`DWMWA_USE_IMMERSIVE_DARK_MODE` 20, sonst 19) und dunkle Bildlaufleisten
  (`SetWindowTheme` "DarkMode_Explorer"/"DarkMode_CFD", ältere Windows
  ignorieren es); Umschalter als Knöpfe statt Häkchen; ein Wort für eine
  Figur ist ein Auswahlfeld, kein Häkchen (DH).
- **Große Listen virtuell** (`LBS_NODATA` + `LB_SETCOUNT`): gezeichnet wird
  nur, was sichtbar ist — 82 229 Clips ohne Grenze.

## 21. Das Fenster ohne Max prüfen

- **Die Fensterdatei kennt kein SDK.** Alles Max-Eigene kommt über
  `FensterBruecke` (`swbf2import_fenster.h`, bewusst ohne Windows- und
  SDK-Typen). Die Max-Schicht hängt es in `OeffneFigurenFenster` an.
- **MinGW statt Attrappe:** die SDK-Attrappe bringt eigene Windows-Typen mit und
  kann reines Win32 nicht prüfen. `tools/PRUEFE_FENSTER.sh` übersetzt das
  Fenster und die ganze Containerschicht mit MinGW gegen echte Windows-Header.
  Erster Fund, gleich beim ersten Lauf: `std::max(4, (r.bottom - r.top) / 4)`
  — `RECT` hat `LONG`-Felder, `std::max(int, long)` übersetzt nicht. MSVC hätte
  denselben Fehler gemeldet, erst bei DH.
- **Probelauf:** `fenster_probe.exe <figuren.txt> [suche] [hell]` zeigt das echte
  Fenster mit einer castool-Figurenliste; unter Linux mit Wine und Xvfb
  fotografierbar (`docs/fenster_probe.png`).
- **`LBS_NODATA`** für die Liste: ab rund tausend Einträgen empfiehlt Windows die
  datenlose Liste; gezeichnet wird aus dem eigenen Vektor, ein Filterwechsel ist
  nur `LB_SETCOUNT`. `WM_MEASUREITEM` kommt dabei **vor** `WM_INITDIALOG` —
  die Höhe wird ohne Fensterzustand aus der Dialogschrift berechnet.
- **Protokoll geschachtelt:** das Fenster öffnet `Import swbf2.log` für die
  ganze Sitzung, jeder Import darin noch einmal. Ohne Zähler hätte der erste
  Import die Datei geschlossen und alles Weitere wäre still verloren gegangen.
- **Sanitizer an echten Dateien** (`PRUEFE_QUELLTEXT.sh`, Teil 4): castool
  `--ablage` lädt den echten 100-MB-Index unter ASan und UBSan, die Figurenliste
  muss zeichengleich mit einer alten `figuren.txt` sein; fbdump liest eine echte
  `.fbmodel`.

## 22. Die Attrappe muss die Kopfaufteilung des SDK nachbilden

Die Attrappe prüft nur, was sie nicht fälschlich hergibt. 0.34.0 bot
`IDerivedObject` und `CreateDerivedObject` schon über `max.h` an — im echten SDK
stehen sie in `modstack.h`. Deshalb lief die Vorabprüfung grün, und erst DHs
MSVC meldete den Fehler. Seit 0.34.1 steht in der Attrappe von `max.h` nur die
Vorwärtserklärung (wie in `inode.h`), die Klasse selbst in einer eigenen
Attrappe `modstack.h`. **Gegenprobe:** der Code aus 0.34.0 ergibt gegen die
neue Attrappe genau DHs drei Fehler, mit `#include <modstack.h>` keinen.
Regel: jede neue SDK-Funktion erst im echten Kopf nachschlagen (Doku:
„#include …") und in die Attrappe DESSELBEN Kopfes eintragen.

## Quellen zu 14–20

Menüs

- Menu System (2025): https://help.autodesk.com/cloudhelp/2025/ENU/MAXDEV-Developer/files/3ds_max_sdk_features/user_interface/menu_system.html
- Menu Migration Guide: https://help.autodesk.com/cloudhelp/2025/ENU/MAXDEV-Developer/files/3ds_max_sdk_features/user_interface/menu_system/menu_migration_guide.html
- Startup Scripts (2025): https://help.autodesk.com/cloudhelp/2025/ENU/MAXScript-Help/files/MAXScript-Introduction/General-MAXScript-Topics/GUID-615D14FB-0F2D-4801-B381-1128C4128C70.html
- Forum, Larry Minton: https://forums.autodesk.com/t5/3ds-max-programming-forum/3ds-max-2025-the-menu-system/td-p/12792503
- Menü-Callbacks: https://help.autodesk.com/cloudhelp/2025/ENU/MAXScript-Help/files/MAXScript-Tools-and-Interaction/Change-Handlers-and-Callbacks/General-Event-Callback-Mechanism/GUID-41DFAF71-EFAB-4A15-A04C-05437726E3C5.html
- Example Plug-in Package (2025): https://help.autodesk.com/cloudhelp/2025/ENU/MAXDEV-Developer/files/writing_plug-ins/plugin_package/packagexml_example.html
- Example Plug-in Package (2024, menuMan): https://help.autodesk.com/cloudhelp/2024/ENU/Max-Developer-Help/writing_plug-ins/plugin_package/packagexml_example.html
- MAXScript Menu System (2027): https://help.autodesk.com/cloudhelp/2027/ENU/MAXScript-Help/files/MAXScript-Tools-and-Interaction/Interacting-with-the-3ds-Max/GUID-FF48D0EC-6669-4EC7-AB43-E9998A14A198.html
- IMenuBarContext (2016): https://help.autodesk.com/cloudhelp/2016/ENU/Max-SDK/files/GUID-F0264858-13EE-4D97-8B90-0D4E9DEB1E8B.htm
- MAXScript-Neuerungen 2026: https://help.autodesk.com/cloudhelp/2026/JPN/MAXScript-Help/files/What-is-New-in-MAXScript/What-s-New-in-MAXScript-in-3ds.html

Threads, DLL, SDK

- ThreadingDebuggingTools: https://help.autodesk.com/cloudhelp/2026/ENU/MAXDEV-CPP-API-REF/namespace_max_s_d_k_1_1_threading_debugging_tools.html
- Tips and Tricks (Hauptthread): https://help.autodesk.com/cloudhelp/2024/ENU/Max-Developer-Help/3ds_max_sdk_features/tips_and_tricks.html
- IMainThreadTaskManager: https://help.autodesk.com/cloudhelp/2023/ENU/Max-Developer-Help/cpp_ref/class_i_main_thread_task_manager.html
- ADN, Threads in Max: https://github.com/ADN-DevTech/3dsMax-Python-HowTos/blob/master/src/packages/mxthread/README.md
- Interface/ProgressStart: https://help.autodesk.com/cloudhelp/2025/ENU/MAXDEV-CPP-API-REF/class_interface.html
- MAXScript Progress Bar: https://help.autodesk.com/cloudhelp/2023/ENU/MAXScript-Help/files/MAXScript-Tools-and-Interaction/Interacting-with-the-3ds-Max/Status-Bar/GUID-5C069EAA-E2EB-4F7B-B6AC-DECD51121348.html
- LibInitialize/LibShutdown: https://help.autodesk.com/cloudhelp/2022/ENU/Max-Developer-Help/cpp_ref/group___optional_plugin_dll_function.html
- Required DLL Functions: https://help.autodesk.com/cloudhelp/2016/ENU/Max-SDK/files/GUID-608B1265-B25E-47BB-A49B-5AF2A27F71CC.htm
- Core Interfaces: https://help.autodesk.com/cloudhelp/2016/ENU/Max-SDK/cpp_ref/group___core_interface_management.html
- General Best Practices (2025): https://help.autodesk.com/cloudhelp/2025/ENU/MAXDEV-Developer/files/best_practices/general_best_practices.html
- Memory Management: https://help.autodesk.com/cloudhelp/2022/ENU/Max-Developer-Help/best_practices/memory_management.html
- STL im SDK: https://help.autodesk.com/cloudhelp/2022/ENU/Max-Developer-Help/3ds_max_sdk_features/rendering/new_rendering_api.html
- DLL Best Practices (Microsoft): https://learn.microsoft.com/windows/win32/dlls/dynamic-link-library-best-practices
- constexpr-Mutex und ältere msvcp140.dll: https://github.com/microsoft/STL/issues/4978 · https://github.com/pybind/pybind11/issues/6053
- Klassennamen in MAXScript: https://help.autodesk.com/cloudhelp/2022/ENU/Max-Developer-Help/writing_plug-ins/plug-in_basics/class_descriptors.html
- Max.log: https://help.autodesk.com/cloudhelp/2026/ENU/3DSMax-Customizing/files/GUID-C2AA7758-D2E6-45FB-835D-E8524A7F672F.htm
- modstack.h (IDerivedObject, CreateDerivedObject): https://help.autodesk.com/cloudhelp/2026/ENU/MAXDEV-CPP-API-REF/modstack_8h.html
- Modifikatoren hinzufügen: https://help.autodesk.com/cloudhelp/2018/ENU/Max-Developer-Help/developer/3ds_max_sdk_features/modeling/modifiers/adding_modifiers_to_objects.html
- Interface7::ResCode: https://help.autodesk.com/cloudhelp/2026/ENU/MAXDEV-CPP-API-REF/class_interface7.html
- GetCOREInterface7: https://help.autodesk.com/cloudhelp/2026/ENU/MAXDEV-CPP-API-REF/_get_c_o_r_e_interface_8h.html
- ISkinImportData: https://help.autodesk.com/cloudhelp/2027/ENU/MAXDEV-CPP-API-REF/class_i_skin_import_data.html
- Texture Map Indices (ID_DI, ID_BU, StdIDToChannel): https://help.autodesk.com/cloudhelp/2026/ENU/MAXDEV-CPP-API-REF/group___material___texture_map___i_ds.html
- imtl.h (GNORMAL_CLASS_ID, Normal_Param_IDs): https://help.autodesk.com/cloudhelp/2026/ENU/MAXDEV-CPP-API-REF/imtl_8h.html
- Normal Bump in C++ (Forum): https://forums.autodesk.com/t5/3ds-max-programming/set-normal-bump-texture/td-p/10403466
- MtlDlgMode: https://help.autodesk.com/cloudhelp/2027/ENU/MAXDEV-CPP-API-REF/group__mtl_dlg_mode.html
- MatEditor.mode (#basic = Compact): https://help.autodesk.com/cloudhelp/2018/ENU/MAXScript-Help/files/GUID-354B1DA2-6573-40FA-AE1E-5AF191E0056C.htm
- bcdec: https://github.com/iOrange/bcdec
- std::terminate: https://cppreference.net/cpp/error/terminate.html
- C++ Core Guidelines (CP.*): https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines

64 Bit und Sicherheit

- 64-Bit-Migration (Microsoft): https://learn.microsoft.com/en-us/cpp/build/common-visual-cpp-64-bit-migration-issues
- Security Best Practices for C++: https://learn.microsoft.com/en-us/cpp/security/security-best-practices-for-cpp?view=msvc-170
- Security Features in MSVC: https://devblogs.microsoft.com/cppblog/security-features-in-microsoft-visual-c/
- MSVC-Schalterübersicht: https://airbus-seclab.github.io/c-compiler-security/msvc_compilation.html
- STL-Härtung (VS 2022 17.14): https://learn.microsoft.com/en-us/cpp/overview/cpp-conformance-improvements
- STL Hardening (Wiki): https://github.com/microsoft/STL/wiki/STL-Hardening
- OpenSSF Compiler Options Hardening Guide: https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html
- MSVC-ASan für Plugins: https://devblogs.microsoft.com/cppblog/msvc-address-sanitizer-one-dll-for-all-runtime-configurations
- libFuzzer: https://llvm.org/docs/LibFuzzer.html

Oberflächen

- Antwortzeiten (Nielsen): https://www.nngroup.com/articles/response-times-3-important-limits/
- Fortschrittsanzeigen (Nielsen): https://www.uxtigers.com/post/progress-indicators
- Abstände in DLU (Microsoft): https://learn.microsoft.com/en-us/previous-versions/ms997619(v=msdn.10)
- Kontrast (WCAG 2.2): https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html
- Suche in langen Listen (Baymard): https://baymard.com/blog/allow-search-for-long-filter-options-lists
- MAXScript HiDPI (2017): https://help.autodesk.com/cloudhelp/2017/ENU/MAXScript-Help/files/GUID-88FE9C40-FDC0-4858-9675-493A9546DF04.htm
- 4K bis Max 2017: https://www.autodesk.com/support/technical/article/caas/sfdcarticles/sfdcarticles/Compatibility-for-4K-resolution-monitors-or-higher-with-3ds-Max.html
- DotNet in MAXScript: https://help.autodesk.com/cloudhelp/2023/ENU/MAXScript-Help/files/Interaction-with-Other/GUID-779FD7AC-953D-4567-B2A8-60B1D8695B95.html
- Timer-Control: https://help.autodesk.com/cloudhelp/2018/ENU/MAXScript-Help/files/GUID-9A4F0A09-BB42-4EED-95CC-6B3D06939640.htm
- Icons anpassen: https://help.autodesk.com/cloudhelp/2017/ENU/3DSMax/files/GUID-4DAB2887-8436-4AAF-8081-81C32A3DEDA2.htm
- Farben (icolorman.h): https://help.autodesk.com/cloudhelp/2027/ENU/MAXDEV-CPP-API-REF/icolorman_8h.html
- Custom Controls: https://help.autodesk.com/cloudhelp/2016/ENU/Max-SDK/files/GUID-EC7DAFCD-91D3-4EFA-9BC4-98D32123FB3B.htm

Jahrgänge und Werkzeugkette

- SDK Requirements (2025): https://help.autodesk.com/cloudhelp/2025/ENU/MAXDEV-Developer/files/about_the_3ds_max_sdk/sdk_requirements.html
- SDK Requirements (2022): https://help.autodesk.com/cloudhelp/2022/ENU/Max-Developer-Help/about_the_3ds_max_sdk/sdk_requirements.html
- Versionsmakros (2027): https://help.autodesk.com/cloudhelp/2027/ENU/MAXDEV-CPP-API-REF/group___version_macros.html
- What's new 2026: https://blog.autodesk.io/whats-new-in-3ds-max-2026/
- What's new 2027: https://blog.autodesk.io/whats-new-in-3ds-max-2027/
- VS 2026 und Max 2027: https://blog.autodesk.io/setting-up-3ds-max-2027-sdk-development-with-visual-studio-2026/
