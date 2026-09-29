// ============================================================
//  fbzuordnung.h - Animationen einer Figur so zuordnen, wie das Spiel es tut
//  (0.72.0). Gemessen im Codec-Labor (Z1-Z4):
//   * Die Spezialisierung eines Helden schreibt WriteEnumerationGameState
//     HeroCharacter.EnumGS = Wert (Luke 1, Vader 2, Boba Fett 3, Han 4, ...,
//     Grievous 11, Maul 14, Rey 15, ...).
//   * Der gemeinsame Zustandsautomat waehlt an IndexChooserControllerAssets
//     ChoiceAssetList[Wert] und prueft EnumBoolAssets (Source = HeroCharacter,
//     Value) in Uebergaengen und Chooser-Eintraegen.
//  Hier: die Wurzeln (AntRefs) und den Heldenwert aus den EBX der Figur holen.
// ============================================================
#pragma once
#include "fbanim.h"
#include "fbgame.h"
#include "fbindex.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace fbzuordnung {

struct Wurzeln {
    int held = -1;                     // Wert von HeroCharacter.EnumGS, -1 = unbekannt
    std::string heldGS;                // Schluessel von HeroCharacter.EnumGS
    std::vector<std::string> keys;     // ANT-Schluessel aus den AntRefs
    size_t ebx = 0;                    // gelesene EBX
    // 0.75.0: Blaster-Figuren (Boba, Han, Iden, Soldaten) verzweigen nicht
    // nach dem Helden, sondern nach der Waffe: WeaponType.EnumGS (Liste 9),
    // SpecificWeapon.EnumGS (Liste 86), Game.Character.BodyType.EnumGS (33).
    // Jede geschriebene Zustandsgroesse mit ihrem Wert: Schluessel -> Werte.
    std::map<std::string, std::vector<int>> zustaende;
    std::map<std::string, std::string> zustandName;
    std::map<std::string, size_t> ausEbx;        // 0.76.0: welcher EBX-Pfad wie viele Wurzeln lieferte
};

// Suchwoerter fuer die EBX-Pfade einer Figur (Schluessel wie im Figurenfenster,
// z. B. "darthvader" -> darthvader, vader).
std::vector<std::string> Suchwoerter(const std::string& figur);

Wurzeln LiesWurzeln(fbgame::Spiel& spiel, const fbindex::Index& idx, fbanim::Quelle& q, const std::string& figur);

// Die obersten Automaten des Spiels (0.80.0). Gemessen in Z10: die Wurzeln der
// Figuren fuehren nur in den Automaten der Ich-Ansicht bzw. den Nahkampf-
// Automaten der Helden. Die sichtbaren Bewegungen haengen unter '.3P.Top.SF'
// (Bank coop_nt_mc85), den keine Figuren-EBX verweist. Diese Automaten werden
// deshalb als zusaetzliche Wurzeln genommen. Die Suche laeuft einmal.
const std::vector<std::string>& AutomatenWurzeln(fbanim::Quelle& q);

// Passt ein vom Spiel zugeordneter Clip zum Skelett der Figur (0.81.0)?
// Gemessen in Z11: ueber die Wurzeln der Figuren kommen auch Clips der
// Ich-Ansicht mit (Han 28, Iden 28) - die gehoeren zum Armskelett. Und ueber
// den 3p-Automaten kommen alle Koerpertypen (A_HM_ Mensch, A_B1_/A_B2_
// Kampfdroiden), weil die Figuren keinen Koerpertyp schreiben.
// clipKlein = Anzeigename in Kleinbuchstaben.
bool PasstZumSkelett(const std::string& figur, const std::string& clipKlein);

// Clips eines FAHRZEUGS (1.13.0): Gemessen in Z13 - Fahrzeuge haben eigene
// Clips (AT-TE 44, AT-RT 32), die man an den Knochennamen erkennt, nicht am
// Namen. `eigeneBones` sind die Bones des Fahrzeugskeletts OHNE die des
// Menschenskeletts; ein Clip gehoert dazu, wenn er mindestens die Haelfte
// davon bedient. Dauert ueber alle Clips etwa 30 s, deshalb einmal je Fahrzeug.
std::set<std::string> ClipsFuerSkelett(fbanim::Quelle& q, const std::set<std::string>& eigeneBones, double maxSekunden);

} // namespace fbzuordnung
