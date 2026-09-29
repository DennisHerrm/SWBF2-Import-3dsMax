// ============================================================
//  fbanim.h - Animationsclips: finden, benennen, entpacken (RAW, FRAME).
//
//  Die Namenskette, am Spiel GEMESSEN (0.36.1, outro_team1: Drehkanaele
//  109/109 und 107/107 mit ".q", Vektoren 61/63 und 61/61 mit ".t"):
//    Clip.__base.ChannelToDofAsset -> DofIds des Clips
//    RigAsset mit der groessten Deckung (mindestens 90 %) -> Platz je DofId
//    RigDofSets + DofSetIdIndices -> Slotnamen je DofSet (LayoutAsset.Slots,
//    rekursiv ueber LayoutAssets/Children), an ihre STARTPOSITION gelegt
//    RAW:   Kanal j heisst namen[MappingIndices[j]]
//    FRAME: Kanal i heisst namen[i]
//  Entpacken nach fb_anim.py (eigener Code aus fbtools):
//    RAW je Key: QuatCount x 4 Floats, Vec3Count x 4 (der vierte ist
//    Fuellung), dann FloatCount Floats, auf ein Vielfaches von 4 aufgefuellt.
//    Reihenfolge der Kanaele: bewegte q, t, f, dann konstante q, t, f.
//    FRAME ist eine einzelne Pose ohne Zeitachse.
//  DCT und VBR folgen als eigene Umsetzungen (Veroeffentlichung).
// ============================================================
#pragma once

#include "fbgame.h"
#include "fbgd.h"
#include "fbgdwerte.h"
#include "fbindex.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fbanim {

struct Kanal {
    std::string name;
    char art = 'f';              // q, t, f
    bool konstant = false;
    int komponenten = 1;         // 4, 3, 1
    std::vector<float> werte;    // komponenten * (1 oder Keyzahl)
};

struct Clip {
    std::string name, bank, klasse, codec, rig;
    float dauer = 0.0f;
    int32_t endFrame = -1;
    bool additiv = false;
    std::vector<float> zeiten;
    std::vector<Kanal> kanaele;
    size_t dofIds = 0, benannt = 0, rigTreffer = 0;
    size_t nachbenannt = 0, uneinig = 0;   // 1.42.0: aus anderen Rigs benannt / Rigs uneins
};

struct ClipEintrag {
    size_t bank = 0, eintrag = 0;
    std::string name, klasse, codec, key;
    int32_t endFrame = -1;
    // Lesbarer Name und Bildrate aus dem ClipControllerAsset, das auf den
    // Clip verweist (gemessen: 79 261 Controller, alle lesbar, alle mit
    // Verweis "Anim" auf einen Clip). Leer/0, wenn keiner verweist.
    std::string anzeige;
    float fps = 0.0f;
    size_t controller = 0;
};

class Quelle {
public:
    // Einmal ueber alle AssetBanks: Keys, RigAssets, Clips.
    bool Baue(fbgame::Spiel& spiel, const fbindex::Index& idx, std::string& fehler);
    const std::vector<ClipEintrag>& Clips() const { return clips_; }
    const std::string& BankName(size_t b) const { return bankName_[b]; }
    size_t KeyZahl() const { return nachKey_.size(); }
    size_t RigZahl() const { return rigs_.size(); }
    bool Entpacke(const ClipEintrag& c, Clip& aus, std::string& fehler);

    // Ein beliebiger Eintrag nach seinem __key (16 Hexzeichen): Bank, Klasse,
    // Name - und als ClipEintrag, falls es eine Animation ist (0.42.0, fuer
    // die Suche nach der Gesichtspose, auf die ein VisualUnlock verweist).
    bool FindeEintrag(const std::string& key, ClipEintrag& aus);
    // Die Felder eines beliebigen Eintrags (fuer Messungen).
    bool Felder(const ClipEintrag& ce, fbgd::Datensatz& aus);
    // Zuordnung wie im Spiel (0.67.0): von ANT-Schluesseln (aus den AntRefs
    // der EBX einer Figur) allen Verweisen im ANT-Graphen folgen und die
    // erreichbaren Clips sammeln - ueber ClipController (Feld Anim) und direkt
    // verwiesene AnimationAssets.
    struct Reichweite {
        std::set<std::string> clips;
        std::set<std::string> clipsHeld;           // 0.73.0: nur ueber eine Heldenweiche erreicht (FolgeFuerHeld)
        std::map<std::string, size_t> typen;       // besuchte Asset-Klassen
        size_t knoten = 0, startGefunden = 0;
        bool abgebrochen = false;
    };
    Reichweite Folge(const std::vector<std::string>& startKeys, size_t maxKnoten);
    // Wie Folge, aber mit Helden-Filter (0.72.0, gemessen in Z3/Z4):
    // IndexChooserControllerAssets, die HeroCharacter.EnumGS abfragen, nehmen
    // nur ChoiceAssetList[held]; Uebergaenge und Chooser-Eintraege, deren
    // EnumBool-Bedingungen (Source = HeroCharacter.EnumGS, Value) fuer diesen
    // Helden nicht gelten, werden nicht verfolgt.
    Reichweite FolgeFuerHeld(const std::vector<std::string>& startKeys, const std::string& heldGS, int held, size_t maxKnoten);

    // 0.75.0: mehrere Weichen gleichzeitig (Held, Waffentyp, konkrete Waffe,
    // Koerpertyp ...). zustaende: Schluessel der Zustandsgroesse -> erlaubte
    // Werte. An einem IndexChooser, der eine dieser Groessen abfragt, werden
    // nur die Plaetze dieser Werte genommen; EnumBool-Bedingungen mit einer
    // bekannten Groesse gelten entsprechend.
    Reichweite FolgeFuerZustaende(const std::vector<std::string>& startKeys,
                                  const std::map<std::string, std::vector<int>>& zustaende, size_t maxKnoten);
    bool HatKey(const std::string& key) const { return nachKey_.count(key) != 0; }
    // Ein beliebiges ANT-Asset nach Schluessel lesen (0.69.0, fuer die Zuordnung).
    bool LiesAsset(const std::string& key, fbgd::Datensatz& ds, std::string& klasse);

    // Assets einer Bank nach Klasse und Namensteil suchen (0.78.0): so findet
    // sich der 3p-Automat, den die Wurzeln der Figuren nicht erreichen.
    struct Fund { std::string key, name, klasse, bank; };
    std::vector<Fund> SucheAssets(const std::string& bankTeil, const std::string& klasseTeil, const std::string& nameTeil, size_t maxTreffer);

    // Die Kanalnamen eines Clips in DOF-Reihenfolge (0.54.0) - dieselbe
    // Namenskette wie in Entpacke (ChannelToDof -> Rig -> Slotnamen), aber fuer
    // JEDEN Codec, also auch VBR/DCT. Namen enden auf .q/.t (Drehung/Vektor).
    bool Kanalnamen(const ClipEintrag& c, std::vector<std::string>& namen, std::string& rig, size_t& benannt);

    // Zwischenspeicher auf Platte (0.41.0): dasselbe Verzeichnis, das Baue()
    // aus allen Baenken liest, als Binaerdatei. Gebunden an die Kopfnummer
    // des Spiels - nach einem Spiel-Update wird neu gelesen. Nichts wird
    // geschaetzt: gespeichert ist exakt, was Baue() ermittelt hat.
    bool Speichere(const std::string& utf8Pfad, int64_t kopfnummer, std::string& fehler) const;
    bool Lade(fbgame::Spiel& spiel, const std::string& utf8Pfad, int64_t kopfnummer, std::string& grund);

private:
    struct Ort { size_t bank = 0, eintrag = 0; };
    fbgame::Spiel* spiel_ = nullptr;
    std::vector<std::string> bankName_, bankSha_;
    std::map<std::string, Ort> nachKey_;
    std::vector<Ort> rigs_;
    std::vector<ClipEintrag> clips_;
    std::map<size_t, std::pair<std::vector<uint8_t>, fbgd::Bank>> cache_;
    std::map<size_t, std::vector<long long>> rigDofs_;                       // Rig -> DofIds
    std::map<size_t, std::pair<std::vector<std::string>, std::string>> rigNamen_;  // Rig -> Slotnamen, Key
    std::map<std::string, std::pair<std::string, float>> controller_;       // Clip-Key -> (Name, FPS)
    // 0.65.2: Namenskette je ChannelToDofAsset zwischengespeichert. Die Suche
    // ueber alle Rigs kostete ~0,1 s je Clip (Labor E1: 1130 s fuer VBR, Max:
    // 240 Clips in 27 s). Viele Clips teilen dasselbe ChannelToDofAsset.
    struct NamenErgebnis { std::vector<std::string> namen; std::string rig; size_t benannt = 0, rigTreffer = 0, dofs = 0, nachbenannt = 0, uneinig = 0; };
    std::map<std::string, NamenErgebnis> namenCache_;
    // 1.12.0: die DofIds jedes Rigs als Menge - einmal gebaut. Vorher baute
    // NamenFuer diese Menge fuer JEDE Kanalkette und JEDES Rig neu; bei 22 000
    // Clips und Hunderten Rigs lief das Labor deshalb stundenlang.
    std::map<size_t, std::set<long long>> rigDofSet_;
    const std::set<long long>& RigDofSet(size_t rig);
    const NamenErgebnis& NamenFuer(const fbgd::Felder& basis);
    std::map<std::string, size_t> controllerZahl_;

    const fbgd::Eintrag* Satz(const Ort& o, fbgd::Datensatz& ds);
    void SlotsVon(const std::string& key, int tiefe, std::set<std::string>& gesehen, std::vector<std::string>& namen);
    const std::vector<long long>& RigDofs(size_t r);
    const std::pair<std::vector<std::string>, std::string>& RigNamen(size_t r);
};

// .fbanim nach fb_animdump.schreibe_bin (Kopf 64 Byte, ohne Bones).
bool SchreibeFbanim(const std::string& utf8Pfad, const Clip& c, std::string& fehler);

// 1.42.0: LAUFRICHTUNG. AITrajectory ist die Bewegungsrichtung fuer KI und
// Steuerung, keine sichtbare Drehung - das Spiel gibt sie selbst vor. In 84 %
// der absoluten Clips steht sie konstant auf -90 Grad (gemessen an 4016 Clips,
// castool --wurzel); Richtungsclips (Obi-Wan: Walk_Bwd +90, Walk_Left/Right,
// 45er) tragen dort ihre Laufrichtung. Weil Hips unter AITrajectory haengt,
// drehte Max den ganzen Koerper mit - rueckwaerts schaute Obi-Wan nach hinten.
// Diese Funktion setzt eine KONSTANTE AITrajectory-Drehung absoluter Clips auf
// die Grundrichtung; animierte (Turn-Clips) und additive bleiben unberuehrt.
// true = ersetzt; ersetztGrad ist dann die Gierdrehung, die im Clip stand.
constexpr double kGrundrichtungGrad = -90.0;
bool NormiereLaufrichtung(Clip& c, double& ersetztGrad);

} // namespace fbanim
