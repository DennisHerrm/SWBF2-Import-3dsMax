// ============================================================
//  SWBF2 Import - Plugin-Kopf
//
//  Skelett und Meshes aus einer .fbmodel - entweder ueber
//  Datei -> Importieren oder ueber das Figurenfenster, das die
//  Figur direkt aus dem Spiel holt (Menue EAfront Tool).
//  Skinning, Texturen und Animation folgen.
//
//  Der Leser (src/fbdump.h) ist derselbe, den auch fbdump.exe
//  benutzt - so kann nie auseinanderlaufen, was das Plugin und
//  was das Pruefwerkzeug sieht.
// ============================================================
#pragma once

#include "fbdump.h"
#include "swbf2import_fenster.h"

#include <string>

// Max-SDK. Die Reihenfolge ist Absicht: max.h zuerst, danach die
// Header, die aeltere SDKs nicht selbst mitziehen.
#include <max.h>
#include <iparamb2.h>
#include <impexp.h>
#include <object.h>
#include <istdplug.h>

#define SWBF2IMPORT_VERSION     1400
#define SWBF2IMPORT_VERSION_STR  _T("1.43.0")

// Zwei eigene Klassen-IDs. Im Ernstfall gehoeren die aus
// gencid.exe; diese hier sind einmalig gezogen und werden nicht
// mehr angefasst, damit installierte Fassungen sich nicht
// gegenseitig verdraengen.
#define SWBF2IMPORT_CLASS_ID        Class_ID(0x5b2e14a7, 0x31c96f42)
#define SWBF2IMPORT_SCENE_CLASS_ID  Class_ID(0x5b2e14a8, 0x31c96f43)

namespace swbf2 {

// ------------------------------------------------------------
//  Achsen
//
//  Das Spiel rechnet in Metern mit Y nach oben, Max hat Z oben.
//  Umgerechnet wird an GENAU DIESER Stelle und nirgends sonst.
//
//      Spiel (x, y, z)  ->  Max (x, -z, y)
//
//  In Max' Zeilenvektor-Schreibweise heisst das: die Weltmatrix
//  wird von rechts mit A multipliziert.
//
//  Das ist die uebliche Umrechnung fuer Y-oben nach Z-oben und
//  deckt sich mit der Richtung, die Frostys eigener FBX-Export
//  nimmt. BEWIESEN ist sie damit nicht - das zeigt erst das
//  fertige Rig in Max. Deshalb steht sie hier allein und laesst
//  sich in einer Zeile aendern.
// ------------------------------------------------------------
Matrix3 AchsenMatrix();

// Aus den zwoelf Zahlen der Ruhelage (right, up, forward, trans)
// eine Max-Matrix bauen.
Matrix3 RuheAlsMatrix(const float rest[12]);

// ------------------------------------------------------------
//  Das Skelett anlegen.
//
//  Liefert die Zahl der erzeugten Bones. `bericht` bekommt eine
//  kurze Zusammenfassung fuer den Anwender.
// ------------------------------------------------------------
int BaueSkelett(Interface* ip, const fb::Model& modell, std::wstring& bericht);

// ------------------------------------------------------------
//  Stufe 2: die Meshes.
//
//  Je Section ein Knoten mit Geometrie, UV und Material-Kennung.
//  Skinning ist Stufe 3, Texturen Stufe 4.
// ------------------------------------------------------------
int BaueMeshes(Interface* ip, const fb::Model& modell, float mass,
               std::wstring& bericht);

// Gegenprobe fuer die Meshes: Name, Zahlen, Huellquader.
bool SchreibeMeshDump(const fb::Model& modell, float mass,
                      const std::wstring& zieldatei);

// ------------------------------------------------------------
//  Gegenprobe: was Max tatsaechlich angelegt hat, Zeile fuer
//  Zeile in eine Textdatei. tools\BONEPROBE.py rechnet dasselbe
//  aus der .fbmodel aus und vergleicht.
//
//  Ohne diese Datei waere "sieht richtig aus" die einzige
//  Pruefung, und die hat noch nie einen Vorzeichenfehler
//  gefunden.
// ------------------------------------------------------------
bool SchreibeBoneDump(Interface* ip, const fb::Model& modell,
                      const std::wstring& zieldatei);

// Der eigentliche Import, von SceneImport::DoImport gerufen - und vom
// Figurenfenster, das jede Figur erst als .fbmodel ablegt und dann genau
// diese Datei importiert. `berichtAus` bekommt die Zusammenfassung.
int ImportiereDatei(const MCHAR* pfad, BOOL ohneRueckfragen, std::wstring* berichtAus = nullptr);

// Das Figurenfenster: Spielordner, Figurenliste, direkter Import.
// 1 = mindestens eine Figur importiert, 0 = ohne, -1 = Fenster ging nicht auf.
// `startOrdner` (leer = gemerkter Ordner) kommt vom Importer, wenn jemand
// ueber Datei -> Importieren eine layout.toc des Spiels waehlt.
int OeffneFigurenFenster(const std::wstring& startOrdner = std::wstring());

// Das Animationsfenster (0.38.0): Clips waehlen und einzeln aufs Skelett legen.
int OeffneAnimFenster();

// Der Eingang von Datei -> Importieren: .fbmodel wird importiert, eine .toc
// aus dem Spiel oeffnet das Figurenfenster fuer genau dieses Spiel.
int ImportiereEingang(const MCHAR* pfad, BOOL ohneRueckfragen);

} // namespace swbf2
