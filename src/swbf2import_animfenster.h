// ============================================================
//  swbf2import_animfenster.h - Animationsfenster (Win32, ohne SDK).
//
//  Wie das Figurenfenster kennt es kein Max-SDK. Die Bruecke liefert
//  Theme-Farben, das Protokoll und das Anwenden eines Clips auf das
//  Skelett in der Szene (Max-Seite: swbf2import_max.cpp).
// ============================================================
#pragma once

#include "fbanim.h"
#include "fbebx.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace swbf2 {

using Ruhelagen = std::map<std::string, fbebx::Lage>;   // Bone -> LocalPose

// Ein Clip in der Zeitleiste (0.41.0): Name, Bereich in Ticks und in Bildern.
struct Sequenz {
    std::string name;
    int start = 0, ende = 0;
    double startBild = 0.0, endeBild = 0.0;
};

// Ein Rig in der Szene (0.48.0): oberster Bone einer importierten Figur. Anakin
// und Vader teilen dasselbe Skelett (walrus_humanmale) - beide koennen in
// derselben Szene stehen, das Fenster waehlt, welches Rig die Clips bekommt.
struct RigInfo {
    unsigned long handle = 0;    // Knoten-Handle des obersten Bones (eindeutig in der Szene)
    std::string name;            // swbf2_rig, z. B. "anakin_01", "darthvader_01 #2"
    std::string figur;           // swbf2_figur-Schluessel fuer die Clipauswahl, z. B. "anakin"
};

struct AnimBruecke {
    void* hInstance = nullptr;   // HINSTANCE der .dlu - dort liegt die Dialogvorlage
    void* eltern = nullptr;      // HWND des Max-Hauptfensters
    uint32_t (*farbe)(int) = nullptr;
    void (*protokoll)(const char*) = nullptr;
    // Den Clip auf das Skelett in der Szene legen; jeder Aufruf ERSETZT die
    // vorige Animation (Entscheidung DH, 10.09.2026).
    // rig = Handle aus rigs() (0 = wie frueher: Auswahl bzw. erster Treffer nach Namen).
    bool (*wendeAn)(const fbanim::Clip& clip, float fps, const Ruhelagen& ruhe, unsigned long rig, std::wstring& bericht) = nullptr;
    // Die Rigs in der Szene (0.48.0).
    std::vector<RigInfo> (*rigs)() = nullptr;
    // Alle Clips als Folge in die Zeitleiste (0.39.0, nach dem Sequenzmodus
    // des XFBIN-Importers): Bindepose bei 0, dann jeder Clip mit Abstand,
    // Ruhe-Keys an beiden Enden, Notizspur "animations" auf der Szenenwurzel.
    // Ab 0.48.0 haengt die Folge HINTEN AN, wenn schon Sequenzen in der Notizspur
    // stehen (Wunsch DH: erst Anakins, dann Vaders Clips, ohne das Rig neu zu
    // laden); plan liefert danach ALLE Sequenzen.
    bool (*wendeAnFolge)(const std::vector<fbanim::Clip>& clips, const std::vector<std::string>& namen,
                         const std::vector<float>& fps, int abstand, bool notizen, const Ruhelagen& ruhe,
                         unsigned long rig, std::vector<Sequenz>& plan, std::wstring& bericht) = nullptr;
    // Durchklicken (0.41.0): die Sequenzen aus der Notizspur der Szenenwurzel
    // lesen und einen Bereich in Max zeigen (Zeitbereich + Zeitschieber).
    bool (*liesSequenzen)(std::vector<Sequenz>& aus) = nullptr;
    void (*zeigeBereich)(int startTicks, int endeTicks) = nullptr;
    std::wstring ablage, version;
    // Die Figur in der Szene (0.38.1): Quelle (Bundle), Suchwort (z. B.
    // "anakin") und woher das Wissen stammt. Leer, wenn keine erkannt wurde.
    std::string figur, figurSchluessel, figurHerkunft;
    // 1.43.0: Namen der Meshes in der Szene (klein, mit '|' getrennt). Fuer die
    // Profilwahl, wenn der Figurname kein Klassenwort traegt (ARC Trooper:
    // Meshes l_assault_preq_01_... -> Soldier: Assault).
    std::string szeneMeshes;
};

int ZeigeAnimFenster(const AnimBruecke& b);

} // namespace swbf2
