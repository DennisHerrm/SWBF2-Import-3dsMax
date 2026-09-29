// ============================================================
//  fbzuordnung.cpp - siehe fbzuordnung.h
// ============================================================
#include "fbzuordnung.h"

#include "fbebx.h"
#include "fbgesicht.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <set>

namespace fbzuordnung {
namespace {

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Pfad in Woerter zerlegen (an / und _): "hero_lightsaber_luke_sp" -> hero, lightsaber, luke, sp
std::set<std::string> Woerter(const std::string& pfad) {
    std::set<std::string> w;
    std::string cur;
    for (char c : pfad) {
        if (c == '/' || c == '_' || c == '.' || c == '-') { if (!cur.empty()) w.insert(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) w.insert(cur);
    return w;
}

// 1.09.0: Ein Pfadwort passt zur Figur, wenn es gleich ist, dicht geschrieben
// gleich ist oder sich um HOECHSTENS EINEN Buchstaben unterscheidet. Gemessen:
// Bobas Waffentyp steht in "characters/npc/characters/actor_weapon_bobbafett"
// - mit zwei b, ein Tippfehler von DICE. Genau diese Datei setzt
// WeaponType.EnumGS = 0; ohne sie fehlten ihm alle geteilten 3p-Clips.
bool PasstWort(const std::string& wort, const std::string& name) {
    if (wort == name) return true;
    auto dicht = [](const std::string& x) { std::string r; for (char c : x) if (c != '-' && c != '_' && c != ' ') r += c; return r; };
    if (dicht(wort) == dicht(name)) return true;
    if (name.size() < 6) return false;                       // kurze Namen nicht aufweichen
    const size_t a2 = wort.size(), b2 = name.size();
    if (a2 + 1 < b2 || b2 + 1 < a2) return false;
    size_t i = 0, j = 0, fehler = 0;
    while (i < a2 && j < b2) {
        if (wort[i] == name[j]) { ++i; ++j; continue; }
        if (++fehler > 1) return false;
        if (a2 > b2) ++i;                                     // ein Zeichen zu viel
        else if (b2 > a2) ++j;                                // eins zu wenig
        else { ++i; ++j; }                                    // ein Zeichen anders
    }
    return fehler + (a2 - i) + (b2 - j) <= 1;
}

} // namespace

std::vector<std::string> Suchwoerter(const std::string& figur) {
    const std::string f = Klein(figur);
    // 0.75.0: Profile der Standardklassen (#assault ...) - das Spiel fuehrt sie
    // als actor_weapon_<seite>[_<klasse>] unter characters/npc/characters/.
    if (!f.empty() && f[0] == '#') {
        const std::string k = f.substr(1);
        if (k == "assault") return { "stormtrooper", "rebelsoldier", "clonetrooper", "fnsoldier", "resistancesoldier", "assault" };
        if (k == "heavy") return { "heavy" };
        if (k == "officer") return { "officer" };
        if (k == "specialist") return { "sniper", "specialist" };
        if (k == "droid") return { "b1", "b2", "droid" };
        return { k };
    }
    std::vector<std::string> v{ f };
    static const char* const titel[] = { "darth", "count", "general", "captain", "emperor", "princess", "commander", "admiral", "lord" };
    for (const char* t : titel) {
        const std::string tt(t);
        if (f.size() > tt.size() + 2 && f.compare(0, tt.size(), tt) == 0) v.push_back(f.substr(tt.size()));
    }
    // Vornamen, unter denen das Spiel seine Heldendateien fuehrt (hero_lightsaber_luke ...)
    static const std::pair<const char*, const char*> vorname[] = {
        { "lukeskywalker", "luke" }, { "kyloren", "kylo" }, { "obiwankenobi", "obiwan" }, { "idenversio", "iden" },
        { "landocalrissian", "lando" }, { "leiaorgana", "leia" }, { "emperorpalpatine", "palpatine" }, { "emperorpalpatine", "emperor" },
        { "hansolo", "han" },
    };
    for (const auto& p : vorname) if (f == p.first) v.push_back(p.second);
    return v;
}

const std::vector<std::string>& AutomatenWurzeln(fbanim::Quelle& q) {
    static std::vector<std::string> keys;
    static bool gesucht = false;
    if (gesucht) return keys;
    gesucht = true;
    // Namen der obersten Automaten, die die sichtbare Figur bewegen. Die
    // Ich-Ansicht (.1P) bleibt aussen vor - ihre Clips gehoeren zum Armskelett.
    static const char* const namen[] = { ".3P.Top.SF", ".HeroMelee.Top.SF", ".Droideka.Top.SF", "Droid.Top.SF",
                                         "Droid.MP.Top.SF", "Ewok.Top.SF", "Astromech.Top.SF", "LW.Humanoid.Top.SF" };
    // Erst in den Baenken suchen, in denen sie laut Messung liegen (schnell),
    // sonst in allen (dauert Minuten).
    for (const char* bank : { "coop_nt_mc85", "crait_02", "rootlevel", "tatooine_02", "scarif_02", "sharedbundleanimation_common", "" }) {
        for (const fbanim::Quelle::Fund& f : q.SucheAssets(bank, "StateFlowControllerAsset", "Top.SF", 600)) {
            for (const char* n : namen)
                if (f.name == n && std::find(keys.begin(), keys.end(), f.key) == keys.end()) keys.push_back(f.key);
        }
        if (keys.size() >= sizeof(namen) / sizeof(namen[0])) break;
        if (keys.size() >= 2 && std::string(bank) == "sharedbundleanimation_common") break;   // genug gefunden
    }
    return keys;
}

std::set<std::string> ClipsFuerSkelett(fbanim::Quelle& q, const std::set<std::string>& eigeneBones, double maxSekunden) {
    std::set<std::string> aus;
    if (eigeneBones.empty()) return aus;
    const auto t0 = std::chrono::steady_clock::now();
    std::set<std::string> gesehen;
    const auto& clips = q.Clips();
    for (size_t i = 0; i < clips.size(); ++i) {
        if ((i & 511) == 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > maxSekunden) break;
        if (!gesehen.insert(clips[i].key).second) continue;
        std::vector<std::string> namen;
        std::string rig;
        size_t benannt = 0;
        if (!q.Kanalnamen(clips[i], namen, rig, benannt) || namen.empty()) continue;
        std::set<std::string> getroffen;
        for (const std::string& n : namen) {
            if (n.empty()) continue;
            std::string kurz = Klein(n);
            const size_t p = kurz.find_last_of('.');
            if (p != std::string::npos) kurz = kurz.substr(0, p);
            if (eigeneBones.count(kurz)) getroffen.insert(kurz);
        }
        if (getroffen.size() * 2 >= eigeneBones.size()) aus.insert(clips[i].key);
    }
    return aus;
}

bool PasstZumSkelett(const std::string& figur, const std::string& clipKlein) {
    if (clipKlein.find("1p") != std::string::npos) return false;          // Armskelett der Ich-Ansicht
    // 1.06.0: Traegt der Clip den Namen eines ANDEREN Helden, gehoert er nicht
    // hierher. Gemessen an Boba Fett: nach der Vorgaben-Erkennung blieben noch
    // T_Luke_RunFwdToStop_01 und P_Luke_StandIdle_01 uebrig - Vorgaben aus
    // Listen, in denen kein Wert die Mehrheit hat.
    {
        static const char* const helden[] = { "luke", "vader", "anakin", "grievous", "maul", "yoda", "dooku", "kylo",
                                              "palpatine", "boba", "hansolo", "chewbacca", "leia", "lando", "phasma",
                                              "iden", "finn", "obiwan", "bossk", "rey" };
        const std::string f2 = Klein(figur);
        for (const char* h : helden) {
            const std::string hh(h);
            if (f2.find(hh) != std::string::npos) continue;               // der eigene Name zaehlt nicht
            // "rey" nur am Wortanfang - sonst traefe es "grey", "reyes" ...
            const size_t p = clipKlein.find("_" + hh + "_");
            if (p != std::string::npos) return false;
        }
    }
    const std::string f = Klein(figur);
    const bool droide = f.find("droid") != std::string::npos || f.find("b1") != std::string::npos || f.find("b2") != std::string::npos ||
                        f.find("bx") != std::string::npos || f.find("ig") != std::string::npos;
    if (droide) return true;
    static const char* const droidenTeile[] = { "_b1_", "_b2_", "_b2rp_", "a_b1", "a_b2", "l_b1", "l_b2", "t_b1", "t_b2", "p_b1", "p_b2" };
    for (const char* t : droidenTeile) if (clipKlein.find(t) != std::string::npos) return false;
    // 1.43.0: auch am Namensanfang ("B1_Rifle_T_LF_RunBwdToRunFwd_v02") - dort
    // steht kein Unterstrich davor, die Liste oben traf sie nicht.
    if (clipKlein.rfind("b1_", 0) == 0 || clipKlein.rfind("b2_", 0) == 0) return false;
    // ... und ohne Unterstrich ("B1OfficerCapture" im Officer-Profil: Wirbelsaeule
    // des Menschen um 154 % gestreckt). Ziffer danach waere ein anderer Name.
    if (clipKlein.size() > 2 && (clipKlein.rfind("b1", 0) == 0 || clipKlein.rfind("b2", 0) == 0) &&
        clipKlein[2] >= 'a' && clipKlein[2] <= 'z') return false;
    return true;
}

Wurzeln LiesWurzeln(fbgame::Spiel& spiel, const fbindex::Index& idx, fbanim::Quelle& q, const std::string& figur) {
    Wurzeln w;
    const std::vector<std::string> such = Suchwoerter(figur);
    std::set<std::string> gesehen;
    std::function<void(const fbebx::Wert&, int)> lauf;
    lauf = [&](const fbebx::Wert& o, int tiefe) {
        if (tiefe > 40) return;
        // Heldenwert: WriteEnumerationGameState { GameState = AntRef, Value = n }
        if (o.typ == "WriteEnumerationGameState") {
            const fbebx::Wert* gs = o.Feldwert("GameState");
            const fbebx::Wert* wert = o.Feldwert("Value");
            if (gs != nullptr && wert != nullptr) {
                if (const fbebx::Wert* g = gs->Feldwert("AssetGuid")) {
                    const std::string k = fbgesicht::KeyAusGuid(Klein(g->text));
                    fbgd::Datensatz d;
                    std::string kl;
                    if (!k.empty() && q.LiesAsset(k, d, kl)) {
                        const int v = static_cast<int>(wert->zahl);
                        if (d.nameDa && d.name == "HeroCharacter.EnumGS" && v > 0) { w.held = v; w.heldGS = k; }
                        if (v >= 0) {                                // 0.75.0: alle Zustaende sammeln
                            std::vector<int>& liste = w.zustaende[k];
                            if (std::find(liste.begin(), liste.end(), v) == liste.end()) liste.push_back(v);
                            if (d.nameDa) w.zustandName[k] = d.name;
                        }
                    }
                }
            }
        }
        if (const fbebx::Wert* g = o.Feldwert("AssetGuid")) {
            const std::string k = fbgesicht::KeyAusGuid(Klein(g->text));
            if (!k.empty() && q.HatKey(k) && gesehen.insert(k).second) w.keys.push_back(k);
        }
        for (const auto& fe : o.felder) if (fe.wert) lauf(*fe.wert, tiefe + 1);
        for (const auto& l : o.liste) if (l) lauf(*l, tiefe + 1);
    };
    for (const auto& kv : idx.ebx) {
        const std::string pfad = Klein(kv.first);
        // 0.76.0: auch Waffen und alle Kits - Boba schreibt WeaponType erst in
        // seiner Waffe, Soldaten haben eigene Kits statt kits/hero.
        const bool bereich = pfad.find("gameplay/characters/") == 0 || pfad.find("characters/npc/characters/actor_") == 0 ||
                             pfad.find("gameplay/kits/") == 0 || pfad.find("gameplay/weapons/") == 0;
        if (!bereich) continue;
        const std::set<std::string> wo = Woerter(pfad);
        bool passt = false;
        for (const std::string& s : such) { for (const std::string& pw : wo) if (PasstWort(pw, s)) { passt = true; break; } if (passt) break; }
        if (!passt) continue;
        std::vector<uint8_t> roh;
        std::string f;
        fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f) || !e.Lies(roh, f)) continue;
        ++w.ebx;
        const size_t vorher = w.keys.size();
        for (const auto& o : e.Objekte()) if (o) lauf(*o, 0);
        if (w.keys.size() > vorher) w.ausEbx[kv.first] = w.keys.size() - vorher;
    }
    return w;
}

} // namespace fbzuordnung
