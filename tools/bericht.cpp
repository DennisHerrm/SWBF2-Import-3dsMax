// ============================================================
//  bericht.cpp (1.14.0) - "bekommt wirklich jeder seine Animationen?"
//
//  Eine Zeile je FIGUR und je FAHRZEUG: wie viele Clips sie bekommen und
//  auf welchem Weg. Am Ende steht, wer leer ausgeht - genau die Faelle,
//  die wir uns danach ansehen.
//
//  Die beiden Wege sind dieselben wie im Animationsfenster:
//   * Figuren: Name im Clip ODER die Zuordnung des Spiels
//     (fbzuordnung::LiesWurzeln + Quelle::FolgeFuerZustaende), gefiltert
//     mit PasstZumSkelett (keine Ich-Ansicht, keine fremden Helden).
//   * Fahrzeuge: die eigenen Knochen des Fahrzeugskeletts gegen die
//     Kanalnamen der Clips (fbzuordnung::ClipsFuerSkelett).
//
//  Damit das in Minuten statt Stunden laeuft, werden die Kanalnamen ALLER
//  Clips EINMAL eingesammelt (je Kanalkette, nicht je Clip) und danach nur
//  noch verglichen.
// ============================================================
#include "fbanim.h"
#include "fbauswahl.h"
#include "fbebx.h"
#include "fbfahrzeug.h"
#include "fbgame.h"
#include "fbindex.h"
#include "fbzuordnung.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// characters/hero/darthvader/... -> darthvader (wie im Animationsfenster)
std::string FigurSchluessel(const std::string& quelle) {
    std::vector<std::string> teile;
    size_t a = 0;
    for (size_t i = 0; i <= quelle.size(); ++i)
        if (i == quelle.size() || quelle[i] == '/') { teile.push_back(quelle.substr(a, i - a)); a = i + 1; }
    for (size_t i = 0; i + 2 < teile.size(); ++i) if (teile[i] == "characters") return Klein(teile[i + 2]);
    // 1.15.0: Ohne "characters" im Pfad (blueprintbundle_sp_kit_... und andere)
    // zaehlt der letzte Pfadteil - so steht wirklich jeder Eintrag im Bericht.
    return teile.empty() ? std::string() : Klein(teile.back());
}

// Soldaten auf ihr Klassenprofil abbilden - genau wie das Animationsfenster
// (l_assault_newera -> #assault). Ohne das standen im Bericht 33 leere
// Soldatenzeilen, obwohl sie im Fenster ihre Clips bekommen.
// 1.16.0: Kit- und Buddy-Bundles tragen den Figurennamen im Schluessel:
// blueprintbundle_sp_kit_hansolo -> hansolo, ..._buddy_shriv_rebel -> shriv,
// ..._kit_vehicle_xwing -> xwing. Ohne das standen 40 solcher Zeilen mit
// "Clips 0" im Bericht, obwohl Han, Leia, Luke und Iden ihre Clips haben.
std::string AusBundleName(const std::string& schluessel) {
    const std::string k = Klein(schluessel);
    static const char* const vorn[] = { "blueprintbundle_sp_kit_vehicle_", "blueprintbundle_sp_kit_", "blueprintbundle_sp_buddy_",
                                        "blueprintbundle_mp_kit_", "blueprintbundle_" };
    for (const char* v : vorn) {
        const size_t l = std::strlen(v);
        if (k.compare(0, l, v) != 0) continue;
        std::string rest = k.substr(l);
        // Anhaengsel wie _imperial_nohelmet, _rebel, _gunholstered abschneiden.
        static const char* const raus[] = { "_imperial", "_rebel", "_resistance", "_whiteimperial", "_nohelmet", "_withhelmet",
                                            "_nochestbox", "_nojacket", "_withjacket", "_nogun", "_gunholstered", "_pilot", "_dlc", "_inferno" };
        bool wieder = true;
        while (wieder) {
            wieder = false;
            for (const char* r : raus) {
                const size_t rl = std::strlen(r);
                if (rest.size() > rl && rest.compare(rest.size() - rl, rl, r) == 0) { rest.resize(rest.size() - rl); wieder = true; }
            }
        }
        return rest;
    }
    return std::string();
}

// 1.18.0: Level-, Spielmodus- und Kulissenpakete sind keine Figuren. Sie
// stehen im Importfenster (man kann sie laden), haben aber naturgemaess keine
// Animationen - im Bericht sollen sie nicht wie Luecken aussehen.
bool IstKulisse(const std::string& schluessel, size_t meshsets) {
    const std::string k = Klein(schluessel);
    static const char* const woerter[] = { "level", "rootlevel", "gamemodes", "mode", "collection", "content", "art", "vfx",
                                           "logic", "outro", "intro", "cantina", "hangar", "serverroom", "ioncannonroom",
                                           "dunes", "extraction", "domination", "planetarymissions", "lootboxes", "heroarena",
                                           "heroesversusvillains", "heroesvsvillains", "initialexperience", "streamout",
                                           "mainstreet", "dish_zone", "transitions", "imports" };
    for (const char* w : woerter) if (k == w || k.compare(0, std::strlen(w), w) == 0) return true;
    // Planeten und Missionsabschnitte: felucia_01, kamino_03, a3_m2pil_ds02_...
    if (k.size() > 3 && k[k.size() - 3] == '_' && k[k.size() - 2] == '0') return true;
    if (k.size() > 3 && (k[0] == 'a' || k[0] == 's') && k[1] >= '0' && k[1] <= '9' && k[2] == '_') return true;
    if (k.compare(0, 5, "coop_") == 0 || k.compare(0, 3, "fb_") == 0 || k.compare(0, 5, "fosd_") == 0 || k.compare(0, 5, "mc85") == 0) return true;
    // Ein "Figur" mit hunderten MeshSets ist eine Karte, kein Charakter.
    return meshsets > 300;
}

std::string ProfilFuer(const std::string& schluessel) {
    const std::string k = Klein(schluessel);
    if (k.find("assault") != std::string::npos || k.find("stormtrooper") != std::string::npos ||
        k.find("rifle") != std::string::npos || k.find("trooper") != std::string::npos) return "#assault";
    if (k.find("heavy") != std::string::npos) return "#heavy";
    if (k.find("officer") != std::string::npos || k.find("pistol") != std::string::npos) return "#officer";
    if (k.find("specialist") != std::string::npos || k.find("sniper") != std::string::npos) return "#specialist";
    if (k.find("droid") != std::string::npos || k.find("_b1") != std::string::npos || k.find("_b2") != std::string::npos) return "#droid";
    return std::string();
}

std::set<std::string> BonesVon(fbgame::Spiel& spiel, const fbindex::Index& idx, const std::string& ebxName) {
    std::set<std::string> aus;
    const auto it = idx.ebx.find(ebxName);
    if (it == idx.ebx.end()) return aus;
    std::vector<uint8_t> roh;
    std::string f;
    fbebx::Datei e;
    if (!spiel.HoleNachSha1(it->second.sha1, roh, f) || !e.Lies(roh, f)) return aus;
    const fbebx::Skelett sk = fbebx::LiesSkelett(e);
    for (const std::string& b : sk.namen) aus.insert(Klein(b));
    return aus;
}

} // namespace

int Bericht(int argc, char** argv) {
    const auto t0 = std::chrono::steady_clock::now();
    const std::string spielordner = argv[2];
    std::string cache, clipCache, logPfad = "bericht.log";
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
        if (std::strcmp(argv[i], "--clipcache") == 0) clipCache = argv[i + 1];
        if (std::strcmp(argv[i], "--log") == 0) logPfad = argv[i + 1];
    }
    FILE* log = std::fopen(logPfad.c_str(), "wb");
    auto Zeile = [&](const std::string& z) {
        std::fputs(z.c_str(), stdout);
        std::fputs("\n", stdout);
        std::fflush(stdout);
        if (log != nullptr) { std::fputs(z.c_str(), log); std::fputs("\n", log); std::fflush(log); }
    };

    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund))
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbanim::Quelle q;
    if (clipCache.empty() || !q.Lade(spiel, clipCache, spiel.Kopfnummer(), grund)) {
        if (!q.Baue(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
        if (!clipCache.empty()) q.Speichere(clipCache, spiel.Kopfnummer(), fehler);
    }
    Zeile("BERICHT SWBF2 Import 1.19.0 - bekommt jede Figur und jedes Fahrzeug Animationen?");
    Zeile("Clipverzeichnis: " + std::to_string(q.Clips().size()) + " Clips");

    // ---- 1. Die Kanalnamen aller Clips EINMAL einsammeln -----------------
    const auto t1 = std::chrono::steady_clock::now();
    struct ClipInfo { size_t index; std::set<std::string> bones; };
    std::vector<ClipInfo> clipInfos;
    {
        std::set<std::string> gesehen;
        const auto& clips = q.Clips();
        for (size_t i = 0; i < clips.size(); ++i) {
            if (!gesehen.insert(clips[i].key).second) continue;
            std::vector<std::string> namen;
            std::string rig;
            size_t benannt = 0;
            if (!q.Kanalnamen(clips[i], namen, rig, benannt) || namen.empty()) continue;
            ClipInfo ci;
            ci.index = i;
            for (const std::string& n : namen) {
                if (n.empty()) continue;
                std::string kurz = Klein(n);
                const size_t p = kurz.find_last_of('.');
                if (p != std::string::npos) kurz = kurz.substr(0, p);
                ci.bones.insert(kurz);
            }
            if (!ci.bones.empty()) clipInfos.push_back(std::move(ci));
        }
    }
    Zeile("Kanalnamen von " + std::to_string(clipInfos.size()) + " Clips eingesammelt in " +
          std::to_string(static_cast<int>(std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count())) + " s");

    // Die Bones des Menschenskeletts - sie zaehlen bei Fahrzeugen nicht.
    std::set<std::string> menschlich;
    for (const auto& kv : idx.ebx) {
        if (Klein(kv.first).find("characters/rigs/humanoids/walrus_humanmale") == std::string::npos) continue;
        menschlich = BonesVon(spiel, idx, kv.first);
        break;
    }

    // ---- 2. Fahrzeuge ----------------------------------------------------
    Zeile("");
    Zeile("== FAHRZEUGE ==");
    size_t fzOhne = 0, fzMit = 0;
    std::vector<std::string> fzLeer;
    for (const fbfahrzeug::Fahrzeug& v : fbfahrzeug::Fahrzeuge(idx)) {
        if (v.meshsetNamen.empty()) continue;
        const std::string skelett = fbfahrzeug::SucheSkelett(spiel, idx, v.name);
        std::set<std::string> eigene;
        if (!skelett.empty()) {
            eigene = BonesVon(spiel, idx, skelett);
            for (const std::string& b : menschlich) eigene.erase(b);
        }
        size_t treffer = 0;
        std::string beispiel;
        if (eigene.size() >= 4) {
            for (const ClipInfo& ci : clipInfos) {
                size_t drin = 0;
                for (const std::string& b : eigene) if (ci.bones.count(b)) ++drin;
                if (drin * 2 < eigene.size()) continue;
                ++treffer;
                if (beispiel.size() < 120) {
                    const fbanim::ClipEintrag& c = q.Clips()[ci.index];
                    beispiel += (beispiel.empty() ? "" : ", ") + (c.anzeige.empty() ? c.name : c.anzeige);
                }
            }
        }
        char t[420];
        std::snprintf(t, sizeof t, "FAHRZEUG %-30s Art %-11s MeshSets %3zu  eigene Bones %3zu  Clips %4zu  %s", v.name.c_str(), v.art.c_str(),
                      v.meshsets, eigene.size(), treffer, beispiel.c_str());
        Zeile(t);
        if (treffer == 0) { ++fzOhne; if (fzLeer.size() < 40) fzLeer.push_back(v.name); } else ++fzMit;
    }
    Zeile("FAHRZEUGE mit Clips: " + std::to_string(fzMit) + ", ohne: " + std::to_string(fzOhne));

    // ---- 3. Figuren ------------------------------------------------------
    Zeile("");
    Zeile("== FIGUREN ==");
    std::map<std::string, size_t> jeSchluessel;                 // Schluessel -> MeshSets
    for (const fbauswahl::Figur& fi : fbauswahl::Figuren(idx)) {
        const std::string k = FigurSchluessel(Klein(fi.name));
        if (k.size() >= 3) jeSchluessel[k] += fi.meshsets;
    }
    const std::vector<std::string>& automaten = fbzuordnung::AutomatenWurzeln(q);
    size_t figMit = 0, figOhne = 0, figKulisse = 0;
    std::vector<std::string> figLeer;
    // Die Klassenprofile gehoeren mit in den Bericht (sie stehen auch im Fenster).
    for (const char* p2 : { "#assault", "#heavy", "#officer", "#specialist", "#droid" }) jeSchluessel.emplace(p2, 0);
    std::map<std::string, std::set<std::string>> spielCache;      // Schluessel -> Clips vom Spiel
    for (const auto& kv : jeSchluessel) {
        const std::string& figur = kv.first;
        // Soldaten wie im Fenster auf ihr Profil abbilden; die eigentliche
        // Rechnung laeuft dann einmal je Profil.
        const std::string ausBundle = figur[0] == '#' ? std::string() : AusBundleName(figur);
        const std::string profil = figur[0] == '#' ? std::string() : ProfilFuer(ausBundle.empty() ? figur : ausBundle);
        const std::string rechnenMit = !profil.empty() ? profil : (ausBundle.empty() ? figur : ausBundle);
        std::set<std::string> vomSpiel;
        const auto da = spielCache.find(rechnenMit);
        fbzuordnung::Wurzeln w;
        if (da != spielCache.end()) { vomSpiel = da->second; w.held = -1; }
        else {
            w = fbzuordnung::LiesWurzeln(spiel, idx, q, rechnenMit);
            std::vector<std::string> wurzeln = w.keys;
            for (const std::string& a2 : automaten) wurzeln.push_back(a2);
            if (!wurzeln.empty() && !w.zustaende.empty()) vomSpiel = q.FolgeFuerZustaende(wurzeln, w.zustaende, 400000).clipsHeld;
            spielCache[rechnenMit] = vomSpiel;
        }
        size_t ausSpiel = 0, ausName = 0;
        std::string beispiel;
        std::set<std::string> gesehen;
        const auto& clips = q.Clips();
        for (size_t i = 0; i < clips.size(); ++i) {
            if (!gesehen.insert(clips[i].key).second) continue;
            const std::string an = Klein(clips[i].anzeige);
            // 1.17.0: Kurze Namen (Del, Zay) fielen durch die Vier-Zeichen-Schranke.
            // Jetzt ab drei Zeichen, dafuer nur am Wortanfang - sonst traefe
            // "del" auch "Model" oder "Delta".
            bool nameTrifft = false;
            // 1.18.0: Auch der hintere Wortteil - alderaanhonorguard heisst in
            // den Clips vielleicht nur "HonorGuard". Nur ab sechs Zeichen,
            // damit "agent" oder "guard" nicht alles Moegliche trifft.
            if (!an.empty() && !nameTrifft && rechnenMit.size() >= 10) {
                for (size_t teilL = rechnenMit.size() - 6; teilL >= 6; --teilL) {
                    const std::string hinten = rechnenMit.substr(rechnenMit.size() - teilL);
                    if (an.find(hinten) != std::string::npos) { nameTrifft = true; break; }
                    if (teilL == 6) break;
                }
            }
            if (!an.empty() && !nameTrifft && rechnenMit[0] != '#') {
                if (rechnenMit.size() >= 5) nameTrifft = an.find(rechnenMit) != std::string::npos;
                else if (rechnenMit.size() >= 3) {
                    for (size_t p2 = an.find(rechnenMit); p2 != std::string::npos; p2 = an.find(rechnenMit, p2 + 1)) {
                        const bool vornGrenze = p2 == 0 || an[p2 - 1] == '_' || an[p2 - 1] == '-';
                        const size_t e = p2 + rechnenMit.size();
                        const bool hintenGrenze = e >= an.size() || an[e] == '_' || an[e] == '-' || an[e] == '.';
                        if (vornGrenze && hintenGrenze) { nameTrifft = true; break; }
                    }
                }
            }
            const bool spielTrifft = vomSpiel.count(clips[i].key) != 0 && fbzuordnung::PasstZumSkelett(rechnenMit, an);
            if (!nameTrifft && !spielTrifft) continue;
            if (nameTrifft) ++ausName; else ++ausSpiel;
            if (beispiel.size() < 100 && !clips[i].anzeige.empty()) beispiel += (beispiel.empty() ? "" : ", ") + clips[i].anzeige;
        }
        char t[420];
        const bool kulisse = ausName + ausSpiel == 0 && IstKulisse(figur, kv.second);
        std::snprintf(t, sizeof t, "%-7s %-30s MeshSets %4zu  Heldenwert %3d  %-12s Clips %4zu (Name %zu, Spiel %zu)  %s",
                      kulisse ? "KULISSE" : "FIGUR", figur.c_str(), kv.second,
                      w.held, rechnenMit == figur ? "" : rechnenMit.c_str(), ausName + ausSpiel, ausName, ausSpiel, beispiel.c_str());
        Zeile(t);
        if (ausName + ausSpiel == 0) {
            if (IstKulisse(figur, kv.second)) ++figKulisse;
            else { ++figOhne; if (figLeer.size() < 60) figLeer.push_back(figur); }
        } else ++figMit;
    }
    Zeile("FIGUREN mit Clips: " + std::to_string(figMit) + ", ohne: " + std::to_string(figOhne) +
          " (dazu " + std::to_string(figKulisse) + " Level- und Kulissenpakete, die keine haben koennen)");

    Zeile("");
    Zeile("== OHNE ANIMATIONEN ==");
    std::string z = "Fahrzeuge:";
    for (const std::string& x : fzLeer) z += " " + x;
    Zeile(z);
    z = "Figuren:";
    for (const std::string& x : figLeer) z += " " + x;
    Zeile(z);
    char t[160];
    std::snprintf(t, sizeof t, "BERICHT fertig in %.0f s", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    Zeile(t);
    if (log != nullptr) std::fclose(log);
    return 0;
}
