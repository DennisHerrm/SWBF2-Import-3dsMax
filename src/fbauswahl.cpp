// ============================================================
//  fbauswahl.cpp - siehe fbauswahl.h
// ============================================================
#include "fbauswahl.h"

#include "fbdatei.h"
#include "fbfigur.h"
#include "fbgame.h"

#include <algorithm>
#include <map>
#include <cstdlib>

namespace fbauswahl {

// Das Plugin uebersetzt mit /Zp8 (Pflicht fuer das Max-SDK), diese
// Bibliothek ohne. Solange kein Typ, der zwischen beiden wandert, mehr als
// 8 Byte Ausrichtung braucht, liegen die Felder in beiden gleich. Die
// Pruefung steht absichtlich HIER: nur in einer Datei OHNE /Zp8 zeigt
// alignof die echte Ausrichtung.
static_assert(alignof(fbindex::Index) <= 8, "Index braucht mehr als 8 Byte Ausrichtung");
static_assert(alignof(fbindex::Fortschritt) <= 8, "Fortschritt braucht mehr als 8 Byte Ausrichtung");
static_assert(alignof(fbgame::Spiel) <= 8, "Spiel braucht mehr als 8 Byte Ausrichtung");
static_assert(alignof(fbfigur::Modell) <= 8, "Modell braucht mehr als 8 Byte Ausrichtung");
static_assert(alignof(Figur) <= 8, "Figur braucht mehr als 8 Byte Ausrichtung");

namespace {
std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
}

Art Einordnen(const std::string& n) {
    if (n.find("characters/hero/") != std::string::npos) return Art::Held;
    if (n.find("characters/light/") != std::string::npos) return Art::Hell;
    if (n.find("characters/dark/") != std::string::npos) return Art::Dunkel;
    return Art::Sonstige;
}

bool IstErstePerson(const std::string& n) {
    return n.find("_bundle1p") != std::string::npos;
}

std::vector<Figur> Figuren(const fbindex::Index& idx) {
    // Die Zaehler gehen ueber die Bundle-NUMMER (Platz im Manifest), nicht
    // ueber die Zahl gelesener Bundles. castool hat die Felder bis 0.32.0
    // nach bundles.size() bemessen und dann mit nummer hineingegriffen -
    // faellt ein Bundle beim Lesen aus, waere das ein Zugriff hinter das Ende.
    size_t groesste = 0;
    for (const fbindex::BundleInfo& b : idx.bundles) groesste = std::max(groesste, b.nummer + 1);
    std::vector<size_t> mitMesh(groesste, 0), mitChar(groesste, 0), mitTex(groesste, 0);
    for (const auto& kv : idx.res) {
        const std::string typ = fbindex::ResTypName(kv.second.resType);
        if (typ != "MeshSet" && typ != "Texture") continue;
        const bool istFigur = (kv.first.rfind("characters/", 0) == 0);
        for (size_t bi : kv.second.bundles) {
            if (bi >= groesste) continue;
            if (typ == "Texture") { ++mitTex[bi]; continue; }
            ++mitMesh[bi];
            if (istFigur) ++mitChar[bi];
        }
    }
    std::vector<Figur> aus;
    for (const fbindex::BundleInfo& b : idx.bundles) {
        if (b.nummer >= groesste || mitChar[b.nummer] == 0) continue;
        Figur f;
        f.name = b.name;
        f.meshsets = mitMesh[b.nummer];
        f.texturen = mitTex[b.nummer];
        f.art = Einordnen(b.name);
        f.erstePerson = IstErstePerson(b.name);
        aus.push_back(std::move(f));
    }
    std::sort(aus.begin(), aus.end(), [](const Figur& a, const Figur& b) { return a.name < b.name; });
    return aus;
}

size_t ZaehleHelden(const std::vector<Figur>& figuren) {
    size_t n = 0;
    for (const Figur& f : figuren) if (f.name.find("characters/hero/") != std::string::npos) ++n;
    return n;
}

std::string Klassenname(const std::string& bundleName) {
    std::string n = bundleName;
    for (char& c : n) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    const bool hell = n.find("characters/light/") != std::string::npos;
    const bool dunkel = n.find("characters/dark/") != std::string::npos;
    if (!hell && !dunkel) return std::string();
    const char* seite = nullptr;
    if (n.find("_orig") != std::string::npos) seite = dunkel ? "Empire" : "Rebels";
    else if (n.find("_newera") != std::string::npos) seite = dunkel ? "First Order" : "Resistance";
    else if (n.find("_preq") != std::string::npos) seite = dunkel ? "Separatists (droids)" : "Republic (clones)";
    const char* klasse = nullptr;
    if (n.find("_assault_") != std::string::npos) klasse = "Assault";
    else if (n.find("_heavy_") != std::string::npos) klasse = "Heavy";
    else if (n.find("_officer_") != std::string::npos) klasse = "Officer";
    else if (n.find("_specialist_") != std::string::npos) klasse = "Specialist";
    if (seite == nullptr && klasse == nullptr) return std::string();
    return std::string(seite != nullptr ? seite : (dunkel ? "Dark side" : "Light side")) + (klasse != nullptr ? std::string(" \xC2\xB7 ") + klasse : std::string());
}

std::string Zeile(const Figur& f) {
    return "F " + f.name + "\t" + std::to_string(f.meshsets) + " MeshSets\t" +
           std::to_string(f.texturen) + " Texturen";
}

bool LiesFigurenliste(const std::string& pfad, std::vector<Figur>& aus, std::string& fehler) {
    std::vector<uint8_t> roh;
    if (!fbdatei::LiesAlles(pfad, roh, fehler)) return false;
    aus.clear();
    std::string zeile;
    auto verarbeite = [&aus](std::string z) {
        while (!z.empty() && (z.back() == '\r' || z.back() == '\n')) z.pop_back();
        if (z.size() < 3 || z[0] != 'F' || z[1] != ' ') return;
        const size_t t1 = z.find('\t', 2);
        if (t1 == std::string::npos) return;
        const size_t t2 = z.find('\t', t1 + 1);
        Figur f;
        f.name = z.substr(2, t1 - 2);
        f.meshsets = std::strtoull(z.c_str() + t1 + 1, nullptr, 10);
        if (t2 != std::string::npos) f.texturen = std::strtoull(z.c_str() + t2 + 1, nullptr, 10);
        f.art = Einordnen(f.name);
        f.erstePerson = IstErstePerson(f.name);
        aus.push_back(std::move(f));
    };
    for (uint8_t c : roh) {
        if (c == '\n') { verarbeite(zeile); zeile.clear(); }
        else zeile += static_cast<char>(c);
    }
    if (!zeile.empty()) verarbeite(zeile);
    if (aus.empty()) { fehler = "keine Zeilen der Form \"F <bundle>\" in " + pfad; return false; }
    return true;
}

std::vector<std::string> RigKandidaten(const fbindex::Index& idx, const std::string& bundleName,
                                       size_t hoechstens) {
    std::vector<std::string> aus;
    const std::string gesucht = Klein(bundleName);
    const fbindex::BundleInfo* bundle = nullptr;
    for (const fbindex::BundleInfo& b : idx.bundles) {
        if (Klein(b.name) == gesucht) { bundle = &b; break; }
    }
    if (bundle == nullptr) return aus;
    for (const auto& kv : idx.ebx) {
        if (std::find(kv.second.bundles.begin(), kv.second.bundles.end(), bundle->nummer) ==
            kv.second.bundles.end()) continue;
        const std::string k = Klein(kv.first);
        if (k.find("rigs/") == std::string::npos && k.find("skeleton") == std::string::npos) continue;
        aus.push_back(kv.first);
        if (aus.size() >= hoechstens) break;
    }
    return aus;
}

// Welches Skelett zu welcher Figur gehoert, ist noch OFFEN. Bewiesen ist
// Walrus_HumanMale fuer Anakin; castool und fbtools nehmen es ebenso. Das
// Fenster sagt es offen, und das Protokoll schreibt je Figur die EBX-Namen
// mit, die nach Skelett aussehen - Messmaterial fuer die naechste Runde.

// Eigenes Skelett einer Figur (0.48.0). Gemessen im Index: unter
// characters/rigs/humanoids gibt es nur walrus_humanmale (+1p, masterske) -
// Anakin, Vader und fast alle Helden teilen es. Eigene Skelette haben nur
// b2, bossk, dio, ewok, generalgrievous und yoda (dazu Kreaturen, Droiden,
// Aliens): "<ordner>/<name>_ske". Gesucht wird im Ordner der Figur, dann im
// Ordner des Helden; sonst walrus_humanmale.
std::string SkelettFuer(const fbindex::Index& idx, const std::string& bundleName, std::string& herkunft) {
    auto klein = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return x; };
    std::string b = klein(bundleName);
    if (b.compare(0, 6, "win32/") == 0) b = b.substr(6);
    const std::string ordner = b.substr(0, b.find_last_of('/') + 1);                 // characters/hero/yoda/yoda_01/
    std::string held = ordner;                                                        // characters/hero/yoda/
    if (held.size() > 1) held = held.substr(0, held.find_last_of('/', held.size() - 2) + 1);
    for (int stufe = 0; stufe < 2; ++stufe) {
        const std::string& o = stufe == 0 ? ordner : held;
        if (o.compare(0, 11, "characters/") != 0 || o.size() < 14) continue;
        // 1.43.0: Der Ordner muss eine FIGUR sein (characters/hero/yoda/), nicht
        // eine ganze Kategorie. Der Skytrooper liegt direkt in
        // characters/hero/skytrooper_01/ - eine Stufe hoeher ist characters/hero/,
        // und dort fand die Suche als erstes b2_01_ske (96 statt 248 Bones,
        // 12 662 Vertices ohne gueltigen Bone).
        if (std::count(o.begin(), o.end(), '/') < 3) continue;
        for (const auto& kv : idx.ebx) {
            const std::string n = klein(kv.first);
            if (n.compare(0, o.size(), o) != 0) continue;
            if (n.size() < 4 || n.compare(n.size() - 4, 4, "_ske") != 0 || n.find("_1p") != std::string::npos) continue;
            herkunft = stufe == 0 ? "eigenes Skelett im Figurenordner" : "eigenes Skelett im Heldenordner";
            return kv.first;
        }
    }
    // 1.43.0: Stufe 3 - im Ordner der Figurmeshes des Bundles. Separatisten-
    // Officer, -Heavy und -Specialist tragen den B1-Koerper aus
    // characters/dark/d_assault_preq/d_assault_preq_01/ - dort liegt das
    // Droidenskelett. Bisher bekamen sie das Menschenskelett: beim Officer
    // flogen im ersten Clip die Teile auseinander.
    {
        // Nur fuer Figuren-Bundles (.../characters/...): Level-Bundles mischen viele
        // Figuren, dort gewaenne einfach der Ordner mit den meisten Meshes (Gonk-Droide).
        size_t nummer = static_cast<size_t>(-1);
        const bool figurenBundle = b.compare(0, 11, "characters/") == 0 || b.find("/characters/") != std::string::npos;
        if (figurenBundle)
            for (const fbindex::BundleInfo& bi : idx.bundles) if (bi.name == bundleName) { nummer = bi.nummer; break; }
        std::map<std::string, size_t> ordnerZahl;
        if (nummer != static_cast<size_t>(-1))
            for (const auto& kv : idx.res) {
                if (fbindex::ResTypName(kv.second.resType) != "MeshSet") continue;
                const std::string n = klein(kv.first);
                if (n.compare(0, 11, "characters/") != 0) continue;
                if (std::find(kv.second.bundles.begin(), kv.second.bundles.end(), nummer) == kv.second.bundles.end()) continue;
                ++ordnerZahl[n.substr(0, n.find_last_of('/') + 1)];
            }
        std::vector<std::pair<size_t, std::string>> reihe;
        for (const auto& oz : ordnerZahl) reihe.push_back({ oz.second, oz.first });
        std::sort(reihe.rbegin(), reihe.rend());                                  // meiste Meshes zuerst
        for (const auto& r : reihe) {
            const std::string& o = r.second;
            if (std::count(o.begin(), o.end(), '/') < 3) continue;
            if (o.compare(0, 17, "characters/heads/") == 0) continue;               // Gesichtsrigs gehoeren nicht hierher
            for (const auto& kv : idx.ebx) {
                const std::string n = klein(kv.first);
                if (n.compare(0, o.size(), o) != 0) continue;
                if (n.size() < 4 || n.compare(n.size() - 4, 4, "_ske") != 0 || n.find("_1p") != std::string::npos) continue;
                if (n.find('/', o.size()) != std::string::npos) continue;                // nur direkt im Ordner
                herkunft = "eigenes Skelett im Ordner der Figurmeshes (" + o + ")";
                return kv.first;
            }
        }
    }
    herkunft = "gemeinsames Skelett (kein *_ske im Figurenordner)";
    return kGemeinsamesSkelett;
}

} // namespace fbauswahl
