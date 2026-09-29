// ============================================================
//  fbfahrzeug.h - Fahrzeuge finden (0.82.0).
//
//  Gemessen (Labor Z6/Z10): Fahrzeuge haengen NICHT am Animations-
//  automaten der Figuren - der AT-ST hat 48 EBX, aber keinen einzigen
//  AntRef. Sie bringen eigene Assets mit:
//    gameplay/vehicles/<art>/<name>/<name>_masterskeleton   (Skelett)
//    gameplay/vehicles/<art>/<name>/..._animset             (Animationen)
//    gameplay/vehicles/<art>/<name>/..._ske                 (Teilskelette,
//                                                            auch Zerstoerung)
//  Dazu die MeshSets im Index (resType 0x49B156D4) unter demselben Ordner.
//
//  Hier wird die Liste der Fahrzeuge aufgebaut: Ordner, Art (air, ground,
//  capital ...), Zahl der MeshSets, Skelett und die Animationssaetze.
//  Das Laden des Modells macht danach fbfigur::BaueFigur mit dem
//  Fahrzeug-Skelett.
// ============================================================
#pragma once
#include "fbgame.h"
#include "fbindex.h"

#include <string>
#include <vector>

namespace fbfahrzeug {

struct Fahrzeug {
    // 0.84.0: Gemessen an DHs fahrzeuge.log - nur 6 von 99 Ordnern haben ein
    // Skelett im EIGENEN Ordner (at-at, at-st, at_te, atrt, droideka_01,
    // dwarfspiderdroid). Andere Fahrzeuge teilen sich Skelett und Animationen
    // (homingspiderdroid, spha_t ...) oder haben gar keine Bones. Deshalb wird
    // zusaetzlich ausserhalb gesucht: nach Namensteilen des Fahrzeugs.
    bool skelettFremd = false;      // Skelett liegt nicht im Fahrzeugordner
    std::string ordner;        // gameplay/vehicles/ground/at-st
    std::string name;          // at-st
    std::string art;           // ground, air, capital, stationary, pilots ...
    size_t meshsets = 0;
    std::string skelett;       // ..._masterskeleton oder kuerzestes _ske
    std::vector<std::string> animsets;
    std::vector<std::string> meshsetNamen;   // 0.86.0: die MeshSets selbst
    std::vector<std::string> bundles;   // Bundles mit den MeshSets (fuer den Import)
};

// Gehoert ein MeshSet zum Fahrzeug selbst? Nein bei Zerstoerung, Wrack,
// Geschossen und ungenutzten Fassungen (0.87.0, gemessen an AT-ST/AT-RT).
// 0.89.0: `mitStatisch` nimmt die "static_donotuse"-Fassungen dazu. Gemessen
// am AT-ST: er hat gar kein geskinntes Rumpf-Mesh - seine einzige vollstaendige
// Fassung heisst gameplay/vehicles/ground/atst/atst_static_donotuse_mesh. Beim
// AT-RT traegt dieselbe Fassung sogar die 53 Bones. "donotuse" heisst also
// nicht kaputt, sondern "nicht fuers Spiel" (Requisite/Kulisse).
// 1.43.0: besatzungErlaubt fuer die Kategorie "pilots" - dort IST der Pilot das Modell.
bool IstFahrzeugteil(const std::string& meshsetName, bool mitStatisch = false, bool besatzungErlaubt = false);

// Gehoert ein MeshSet zum Fahrzeug SELBST (0.94.0)? Die Suche nach dem Namen
// findet auch Fremdes: Requisiten und Kulissen (objects/props/...,
// cinematics/...), Effekte (fx/...), Level-Deko und Namensverwechslungen wie
// die Endor-Bunkertuer "at_at_station_bunker_door". Gemessen am AT-ST: von
// 19 gebauten Teilen gehoerten nur sechs zum Fahrzeug.
bool IstEigenesTeil(const std::string& meshsetName, const std::string& fahrzeugName);

// Hat das Fahrzeug ein eigenes Hauptmesh (0.95.0)? Der AT-RT hat "atrt_mesh"
// (geskinnt, 53 Bones); der AT-ST hat keins - seine einzige vollstaendige
// Fassung heisst "atst_static_donotuse_mesh". Danach entscheidet sich, ob die
// "static_donotuse"-Fassungen ins Modell kommen oder als Dubletten wegfallen.
bool HatHauptmesh(const std::vector<std::string>& meshsetNamen, const std::string& fahrzeugName);

// Liegt das MeshSet im EIGENEN Ordner des Fahrzeugs (0.96.0)? Die Namenssuche
// findet auch andere Fassungen desselben Fahrzeugtyps: den First-Order-AT-ST
// (s1/.../nt_fo_at_st/) und den ausgeschlachteten (s9_2/.../atst/). Beide
// bringen eigene Rumpf- und Beinmeshes mit - im Modell laegen sie uebereinander.
// Verlangt: Pfad beginnt mit gameplay/vehicles/ UND ein Ordnerteil ist das
// Fahrzeug selbst.
bool IstKernTeil(const std::string& meshsetName, const std::string& fahrzeugName);

// Ein SkeletonAsset im Index finden, dessen Pfad den Fahrzeugnamen enthaelt
// (0.91.0). Gemessen am AT-ST: sein "atst_masterskeleton" ist KEIN
// SkeletonAsset - keins der 47 Teile fand darueber Bones. Das echte Skelett
// heisst anders; deshalb wird hier jeder Kandidat wirklich geoeffnet.
// spiel wird zum Lesen gebraucht; leer, wenn keins passt.
std::string SucheSkelett(fbgame::Spiel& spiel, const fbindex::Index& idx, const std::string& fahrzeugName);

// Alle Fahrzeuge aus dem Index, nach Ordner sortiert.
std::vector<Fahrzeug> Fahrzeuge(const fbindex::Index& idx);

// "gameplay/vehicles/ground/at-st" -> "at-st"
std::string FahrzeugSchluessel(const std::string& ordner);

} // namespace fbfahrzeug
