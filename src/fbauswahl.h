// ============================================================
//  fbauswahl.h - die Figurenliste.
//
//  Frueher stand die Regel nur in castool. Jetzt benutzen castool
//  UND das Plugin-Fenster dieselbe Funktion - so kann nie
//  auseinanderlaufen, was START.bat prueft und was das Fenster zeigt.
//
//  Ein Bundle zaehlt als Figur, wenn darin ein MeshSet liegt, DESSEN
//  NAME MIT "characters/" BEGINNT. Ohne diese zweite Bedingung kommen
//  2.572 statt 1.858 heraus.
// ============================================================
#pragma once

#include "fbindex.h"

#include <string>
#include <vector>

namespace fbauswahl {

// Reihenfolge = Reiter im Fenster.
// 0.97.0: Fahrzeuge sind eine eigene Art - sie kommen nicht aus den
// Figuren-Bundles, sondern aus gameplay/vehicles/ (siehe fbfahrzeug).
enum class Art { Held = 0, Hell = 1, Dunkel = 2, Sonstige = 3, Fahrzeug = 4 };

struct Figur {
    std::string name;
    size_t meshsets = 0;
    size_t texturen = 0;
    Art art = Art::Sonstige;
    bool erstePerson = false;    // "_bundle1p": die Egoperspektive
    // 0.97.0, nur fuer Fahrzeuge gefuellt:
    std::string skelett;         // eigenes Skelett (atrt_ske, atst_ske01 ...)
    std::vector<std::string> meshsetNamen;
};

Art  Einordnen(const std::string& bundleName);
bool IstErstePerson(const std::string& bundleName);

// Nach Bundlenamen sortiert, wie fbtools.
std::vector<Figur> Figuren(const fbindex::Index& idx);

// Wie castool: Name enthaelt "characters/hero/".
size_t ZaehleHelden(const std::vector<Figur>& figuren);

// Genau die Zeile, die castool schreibt und START.bat vergleicht.
std::string Zeile(const Figur& f);

// Klartext fuer die Standardklassen (0.49.0): characters/<light|dark>/
// <l|d>_<assault|heavy|officer|specialist>_<orig|newera|preq>/... - "orig" ist
// der Galaktische Buergerkrieg (Imperium/Rebellen), "newera" die neue Aera
// (Erste Ordnung/Widerstand), "preq" die Prequel-Aera (Separatisten-Droiden/
// Klonkrieger). Leer fuer alles andere (Helden haben sprechende Namen).
std::string Klassenname(const std::string& bundleName);

// castools Liste zurueck lesen - fuer den Probelauf des Fensters ohne Spiel.
bool LiesFigurenliste(const std::string& utf8Pfad, std::vector<Figur>& aus, std::string& fehler);

// EBX-Namen im Bundle, die nach Skelett aussehen ("rigs/", "skeleton").
// Keine Entscheidung, nur eine MESSUNG fuers Protokoll: welches Skelett
// zu welcher Figur gehoert, ist noch offen.
std::vector<std::string> RigKandidaten(const fbindex::Index& idx, const std::string& bundleName,
                                       size_t hoechstens = 20);

// Skelett einer Figur (seit 0.48.0 im Figurenfenster, 1.43.0 hierher, damit
// Import und Animationsfenster dieselbe Regel benutzen): eigenes *_ske im
// Ordner der Figur, im Ordner des Helden, dann im Ordner der Figurmeshes des
// Bundles; sonst kGemeinsamesSkelett. herkunft sagt, woher.
constexpr const char* kGemeinsamesSkelett = "Characters/Rigs/Humanoids/Walrus_HumanMale";
std::string SkelettFuer(const fbindex::Index& idx, const std::string& bundleName, std::string& herkunft);

} // namespace fbauswahl
