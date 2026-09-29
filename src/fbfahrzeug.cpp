// ============================================================
//  fbfahrzeug.cpp - siehe fbfahrzeug.h
// ============================================================
#include "fbfahrzeug.h"

#include "fbebx.h"

#include <algorithm>
#include <map>

namespace fbfahrzeug {
namespace {

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

const char* const kWurzel = "gameplay/vehicles/";

// gameplay/vehicles/ground/at-st/... -> "ground/at-st" (leer, wenn zu kurz)
std::string OrdnerVon(const std::string& klein) {
    const size_t w = std::char_traits<char>::length(kWurzel);
    if (klein.compare(0, w, kWurzel) != 0) return std::string();
    const size_t a1 = klein.find('/', w);
    if (a1 == std::string::npos) return std::string();
    const size_t a2 = klein.find('/', a1 + 1);
    if (a2 == std::string::npos) return std::string();
    return klein.substr(w, a2 - w);
}

} // namespace

bool IstFahrzeugteil(const std::string& meshsetName, bool mitStatisch, bool besatzungErlaubt) {
    const std::string n = Klein(meshsetName);
    // 1.20.0: Besatzung raus. Der AT-TE bringt "at_te_gunner_mesh" mit - eine
    // eigene FIGUR mit Figurmaterialien (M_Gloves, M_Helmet). Sie brachte die
    // Szene durcheinander: das Animationsfenster hielt den AT-TE fuer die Figur
    // "gunner" und fand deshalb keine Fahrzeug-Clips.
    // 1.34.0: "landmark" ist die Kulissenfassung fuer die Ferne (beim AT-AT
    // ohne jede Gewichtung); sie gehoert nicht ins Modell.
    if (n.find("landmark") != std::string::npos) return false;
    static const char* const besatzung[] = { "gunner", "pilot", "driver", "crew", "passenger" };
    // 1.43.0: nicht in der Kategorie "pilots" - dort flogen alle zwoelf Piloten
    // heraus ("kein Teil des Fahrzeugs konnte gebaut werden").
    if (!besatzungErlaubt)
        for (const char* x : besatzung) if (n.find(x) != std::string::npos) return false;
    static const char* const raus[] = { "destruction", "despawn", "leftover", "wreck", "projectile", "detonator", "/old/" };
    for (const char* x : raus) if (n.find(x) != std::string::npos) return false;
    if (n.find("donotuse") != std::string::npos) return mitStatisch;
    return true;
}

std::string FahrzeugSchluessel(const std::string& ordner) {
    const size_t s = ordner.find_last_of('/');
    return s == std::string::npos ? ordner : ordner.substr(s + 1);
}

bool IstEigenesTeil(const std::string& meshsetName, const std::string& fahrzeugName) {
    const std::string n = Klein(meshsetName);
    // Fremde Bereiche: Kulissen, Kinofassungen, Effekte, Level-Deko.
    static const char* const fremd[] = { "objects/props/", "objects/", "cinematics/", "fx/", "levels/", "_gleb_", "standdisplay" };
    for (const char* x : fremd) if (n.compare(0, std::char_traits<char>::length(x), x) == 0 || n.find(std::string("/") + x) != std::string::npos) return false;
    // Der Fahrzeugname muss dicht geschrieben im Pfad vorkommen - so faellt die
    // Endor-Bunkertuer "at_at_station_bunker_door" bei "at-st" durch ("atat").
    std::string dichtN, dichtF;
    for (char c : n) if (c != '-' && c != '_' && c != ' ') dichtN += c;
    for (char c : Klein(fahrzeugName)) if (c != '-' && c != '_' && c != ' ') dichtF += c;
    if (dichtF.size() >= 3 && dichtN.find(dichtF) == std::string::npos) return false;
    return true;
}

bool IstKernTeil(const std::string& meshsetName, const std::string& fahrzeugName) {
    const std::string n = Klein(meshsetName);
    if (n.compare(0, std::char_traits<char>::length(kWurzel), kWurzel) != 0) return false;
    auto dicht = [](const std::string& x) { std::string r; for (char c : x) if (c != '-' && c != '_' && c != ' ') r += c; return r; };
    const std::string ziel = dicht(Klein(fahrzeugName));
    size_t von = 0;
    while (von < n.size()) {
        const size_t bis = n.find('/', von);
        if (bis == std::string::npos) break;                      // der Dateiname zaehlt nicht
        if (dicht(n.substr(von, bis - von)) == ziel) return true;
        von = bis + 1;
    }
    return false;
}

bool HatHauptmesh(const std::vector<std::string>& meshsetNamen, const std::string& fahrzeugName) {
    auto dicht = [](const std::string& x) { std::string r; for (char c : x) if (c != '-' && c != '_' && c != ' ') r += c; return r; };
    const std::string ziel = dicht(Klein(fahrzeugName)) + "mesh";
    for (const std::string& m : meshsetNamen) {
        const std::string n = Klein(m);
        if (!IstEigenesTeil(m, fahrzeugName) || !IstFahrzeugteil(m, false)) continue;
        const size_t s2 = n.find_last_of('/');
        if (dicht(s2 == std::string::npos ? n : n.substr(s2 + 1)) == ziel) return true;
    }
    return false;
}

std::string SucheSkelett(fbgame::Spiel& spiel, const fbindex::Index& idx, const std::string& fahrzeugName) {
    auto dicht = [](const std::string& x) { std::string r; for (char c : x) if (c != '-' && c != '_' && c != ' ') r += c; return r; };
    const std::string name = dicht(Klein(fahrzeugName));
    if (name.size() < 3) return std::string();
    std::string bester;
    size_t besteBones = 0;
    for (const auto& kv : idx.ebx) {
        const std::string n = Klein(kv.first);
        if (n.find("destruction") != std::string::npos || n.find("/old/") != std::string::npos) continue;
        if (dicht(n).find(name) == std::string::npos) continue;
        // Nur Eintraege, die nach Skelett aussehen - sonst waeren es Tausende.
        // 1.13.0: "_ske" ueberall statt nur am Ende. Die Fahrzeugliste zaehlte
        // das schon seit 0.93.0 so, hier stand noch die alte, engere Regel -
        // deshalb meldete das Labor beim AT-ST "kein Skelett gefunden", obwohl
        // cinematics/objects/atst/atst_ske01 mit 32 Bones da ist.
        if (n.find("skel") == std::string::npos && n.find("_ske") == std::string::npos) continue;
        std::vector<uint8_t> roh;
        std::string f;
        fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f) || !e.Lies(roh, f)) continue;
        const fbebx::Skelett sk = fbebx::LiesSkelett(e);
        if (!sk.gefunden || sk.namen.size() <= besteBones) continue;
        besteBones = sk.namen.size();
        bester = kv.first;
    }
    return bester;
}

std::vector<Fahrzeug> Fahrzeuge(const fbindex::Index& idx) {
    std::map<std::string, Fahrzeug> fz;
    auto dicht = [](const std::string& x) { std::string r; for (char c : x) if (c != '-' && c != '_' && c != ' ') r += c; return r; };
    // MeshSets (resType wie in der Fahrzeugmessung) samt Bundlename
    for (const auto& kv : idx.res) {
        if (kv.second.resType != 0x49B156D4u) continue;
        const std::string n = Klein(kv.first);
        const std::string o = OrdnerVon(n);
        if (o.empty()) continue;
        Fahrzeug& f = fz[o];
        ++f.meshsets;
        if (f.meshsetNamen.size() < 64) f.meshsetNamen.push_back(kv.first);          // 0.86.0
        // 0.85.0: Nicht der MeshSet-Name, sondern die BUNDLES, in denen es
        // liegt (Messung: "kein Bundle mit gameplay/vehicles/.../..._mesh" -
        // Fahrzeug-MeshSets tragen keinen Bundlenamen wie bei Figuren).
        for (size_t b : kv.second.bundles) {
            if (b >= idx.bundles.size() || f.bundles.size() >= 64) continue;
            const std::string& bn = idx.bundles[b].name;
            if (std::find(f.bundles.begin(), f.bundles.end(), bn) == f.bundles.end()) f.bundles.push_back(bn);
        }
    }
    // Skelett und Animationssaetze aus den EBX
    for (const auto& kv : idx.ebx) {
        const std::string n = Klein(kv.first);
        const std::string o = OrdnerVon(n);
        if (o.empty()) continue;
        const auto it = fz.find(o);
        if (it == fz.end()) continue;
        Fahrzeug& f = it->second;
        const bool zerstoerung = n.find("destruction") != std::string::npos || n.find("/old/") != std::string::npos;
        if (n.find("_animset") != std::string::npos && !zerstoerung) {
            if (f.animsets.size() < 32) f.animsets.push_back(kv.first);
        }
        if (zerstoerung) continue;
        // 0.93.0: "_ske" ueberall (atst_ske01), MasterSkeletonAsset traegt keine
        // Bones und zaehlt daher nicht mehr als Vorzugstreffer.
        const bool master = false;
        const bool ske = n.find("_ske") != std::string::npos;
        if (!master && !ske) continue;
        if (n.find("_animset") != std::string::npos) continue;          // 0.90.0: kein Skelett
        // Das Hauptskelett hat Vorrang, sonst das kuerzeste _ske.
        const bool bisherMaster = Klein(f.skelett).find("masterskeleton") != std::string::npos;
        if (f.skelett.empty() || (master && !bisherMaster) || (master == bisherMaster && kv.first.size() < f.skelett.size())) f.skelett = kv.first;
    }
    // 0.84.0: Fahrzeuge ohne eigenes Skelett - nach dem Namen ausserhalb suchen
    // (dichte Schreibweise ohne - und _, damit at_te auch "atte" trifft).
    std::vector<std::pair<std::string, std::string>> fremd;      // (dichter Name, EBX)
    for (const auto& kv : idx.ebx) {
        const std::string n = Klein(kv.first);
        if (n.find("destruction") != std::string::npos || n.find("/old/") != std::string::npos) continue;
        // 0.93.0: "_ske" ueberall (atst_ske01), MasterSkeletonAsset traegt keine
        // Bones und zaehlt daher nicht mehr als Vorzugstreffer.
        const bool master = false;
        const bool ske = n.find("_ske") != std::string::npos;
        if (!master && !ske) continue;
        if (OrdnerVon(n).empty() && n.compare(0, 11, "characters/") == 0) continue;   // Figurenskelette nicht
        fremd.push_back({ dicht(n), kv.first });
    }
    for (auto& kv : fz) {
        Fahrzeug& f = kv.second;
        if (!f.skelett.empty()) continue;
        const std::string name = dicht(FahrzeugSchluessel(kv.first));
        if (name.size() < 4) continue;
        for (const auto& p : fremd) {
            if (p.first.find(name) == std::string::npos) continue;
            if (f.skelett.empty() || p.second.size() < f.skelett.size()) { f.skelett = p.second; f.skelettFremd = true; }
        }
    }
    // Animationssaetze ebenso: auch ausserhalb des Ordners nach dem Namen
    for (auto& kv : fz) {
        Fahrzeug& f = kv.second;
        if (!f.animsets.empty()) continue;
        const std::string name = dicht(FahrzeugSchluessel(kv.first));
        if (name.size() < 4) continue;
        for (const auto& e : idx.ebx) {
            const std::string n = Klein(e.first);
            if (n.find("_animset") == std::string::npos || n.find("destruction") != std::string::npos) continue;
            if (dicht(n).find(name) == std::string::npos) continue;
            if (f.animsets.size() < 32) f.animsets.push_back(e.first);
        }
    }
    // 0.87.0: Ordner mit gleichem dichten Namen zusammenlegen - der AT-ST liegt
    // in "ground/at-st" (Kopf, Geschosse) UND "ground/atst" (der Rumpf).
    {
        std::map<std::string, std::string> ersterMitName;                 // dichter Name -> Ordnerschluessel
        std::vector<std::string> weg;
        for (auto& kv : fz) {
            const std::string d = dicht(FahrzeugSchluessel(kv.first));
            const auto it = ersterMitName.find(d);
            if (it == ersterMitName.end()) { ersterMitName[d] = kv.first; continue; }
            Fahrzeug& ziel = fz[it->second];
            Fahrzeug& quelle = kv.second;
            ziel.meshsets += quelle.meshsets;
            for (const std::string& x : quelle.meshsetNamen) if (ziel.meshsetNamen.size() < 128) ziel.meshsetNamen.push_back(x);
            for (const std::string& x : quelle.bundles) if (std::find(ziel.bundles.begin(), ziel.bundles.end(), x) == ziel.bundles.end()) ziel.bundles.push_back(x);
            for (const std::string& x : quelle.animsets) if (std::find(ziel.animsets.begin(), ziel.animsets.end(), x) == ziel.animsets.end()) ziel.animsets.push_back(x);
            if (ziel.skelett.empty()) { ziel.skelett = quelle.skelett; ziel.skelettFremd = quelle.skelettFremd; }
            weg.push_back(kv.first);
        }
        for (const std::string& k : weg) fz.erase(k);
    }
    // 0.88.0: MeshSets auch AUSSERHALB von gameplay/vehicles/ suchen - beim
    // AT-ST enthalten seine zehn eigenen MeshSets nur Kopf-Cluster, Geschosse
    // und Zerstoerungsteile; der Rumpf muss woanders liegen.
    for (auto& kv : fz) {
        Fahrzeug& f = kv.second;
        const std::string name = dicht(FahrzeugSchluessel(kv.first));
        if (name.size() < 4) continue;
        for (const auto& r : idx.res) {
            if (r.second.resType != 0x49B156D4u) continue;
            const std::string n = Klein(r.first);
            if (!OrdnerVon(n).empty()) continue;                       // die eigenen sind schon drin
            if (dicht(n).find(name) == std::string::npos) continue;
            if (f.meshsetNamen.size() >= 128) break;
            if (std::find(f.meshsetNamen.begin(), f.meshsetNamen.end(), r.first) != f.meshsetNamen.end()) continue;
            f.meshsetNamen.push_back(r.first);
            ++f.meshsets;
            for (size_t b : r.second.bundles)
                if (b < idx.bundles.size() && f.bundles.size() < 64 &&
                    std::find(f.bundles.begin(), f.bundles.end(), idx.bundles[b].name) == f.bundles.end())
                    f.bundles.push_back(idx.bundles[b].name);
        }
    }
    std::vector<Fahrzeug> aus;
    for (auto& kv : fz) {
        Fahrzeug f = std::move(kv.second);
        f.ordner = std::string(kWurzel) + kv.first;
        f.name = FahrzeugSchluessel(kv.first);
        f.art = kv.first.substr(0, kv.first.find('/'));
        std::sort(f.animsets.begin(), f.animsets.end());
        std::sort(f.bundles.begin(), f.bundles.end());
        std::sort(f.meshsetNamen.begin(), f.meshsetNamen.end());
        aus.push_back(std::move(f));
    }
    return aus;
}

} // namespace fbfahrzeug
