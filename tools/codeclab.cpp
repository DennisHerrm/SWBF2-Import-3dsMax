// ============================================================
//  codeclab.cpp - das Codec-Labor (0.50.0).
//
//  Ziel: die restlichen Animationsformate von SWBF2 lesen lernen - VBR
//  (31 % aller Clips, gemessen: die Standardklassen fast nur VBR, z. B.
//  "rebel" 1794 von 2054), DCT (15 %) und CURV. Statt EINEM Test pro Lauf
//  laufen hier mehrere Ansaetze gleichzeitig; jeder schreibt einen eigenen
//  Abschnitt ins Log. Alles EIGENE Umsetzung - keine fremden Decoder
//  (Entscheidung DH: kein Port aus IceBloc/GPL-2 oder Frosty/CC BY-NC-ND).
//
//  Methodik aus der Recherche (Quellen im Log-Kopf und in README):
//   * Entropie misst Zufaelligkeit und trennt Abschnitte (Kopf, Tabellen,
//     gepackte Bitstroeme);
//   * bekannte Werte suchen ("known plaintext"): dieselbe Animation liegt oft
//     in mehreren Baenken - einmal als RAW (lesbar), einmal als VBR/DCT.
//     Aus dem RAW-Zwilling werden die erwarteten Zahlen berechnet und im
//     komprimierten Strom gesucht;
//   * Differenzen zwischen aehnlichen Dateien zeigen, wo Kopf und Nutzdaten
//     liegen;
//   * Hypothesen als Code pruefen (Bitbreiten, Bitreihenfolge, Wertebereich,
//     Blockgroessen, DCT-Koeffizienten) statt nur zu beschreiben.
//
//  Ansaetze (Abschnitte im Log):
//   A1 Feldkatalog      - alle Felder je Codecklasse: Typ, Laengen, Werte
//   A2 Zwillinge        - gleiche Clips als RAW/FRAME und VBR/DCT, Zaehlerabgleich
//   A3 Bytestatistik    - Entropie, Nullanteil, Bitebenen, Blocksummen
//   A4 Bitstrom-Suche   - quantisierte RAW-Werte im VBR-Strom suchen
//                         (Bitbreite 2..16, LSB/MSB zuerst, Bereich global
//                         QuatMin/Max oder je Spur, Abstand 1/3/4 Werte)
//   A5 DCT-Korrelation  - DCT-II der RAW-Spur gegen int8/int16-Folgen im Strom
//   A6 Differenzen      - Clip-Paare gleicher Groesse: gleiche Anfangsbytes
//   A7 Hexdateien       - die kleinsten Clips je Codec komplett als Hex
// ============================================================
#include "fbanim.h"
#include "fbebx.h"
#include "fbgame.h"
#include "fbgesicht.h"
#include "fbfahrzeug.h"
#include "fbzuordnung.h"
#include "fbgdwerte.h"
#include "fbindex.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

struct Log {
    FILE* f = nullptr;
    void operator()(const char* fmt, ...);
};
void Log::operator()(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    char puffer[4096];
    std::vsnprintf(puffer, sizeof puffer, fmt, a);
    va_end(a);
    if (f != nullptr) { std::fputs(puffer, f); std::fflush(f); }   // 0.65.1: sofort schreiben (Runde 16 brach ohne Rest ab)
    std::fputs(puffer, stdout);
    std::fflush(stdout);
}

std::vector<uint8_t> Bytes(const fbgd::Wert* w) {
    std::vector<uint8_t> b;
    if (w == nullptr || w->art != fbgd::Wert::Art::Feld) return b;
    b.reserve(w->werte.size());
    for (const fbgd::Wert& x : w->werte) b.push_back(static_cast<uint8_t>(x.ganz & 0xFF));
    return b;
}

const fbgd::Wert* FeldBeide(const fbgd::Datensatz& d, const char* n) {
    if (const fbgd::Wert* w = fbgd::Feld(d.felder, n)) return w;
    return fbgd::Feld(d.basis, n);
}

double Zahl(const fbgd::Datensatz& d, const char* n, double vorgabe = -1.0) {
    const fbgd::Wert* w = FeldBeide(d, n);
    if (w == nullptr) return vorgabe;
    if (w->art == fbgd::Wert::Art::Gleit) return w->gleit;
    if (w->art == fbgd::Wert::Art::Ganz || w->art == fbgd::Wert::Art::Bool) return static_cast<double>(w->ganz);
    return vorgabe;
}

double Entropie(const std::vector<uint8_t>& b) {
    if (b.empty()) return 0.0;
    size_t h[256] = {};
    for (uint8_t x : b) ++h[x];
    double e = 0.0;
    for (size_t v : h) {
        if (v == 0) continue;
        const double p = static_cast<double>(v) / static_cast<double>(b.size());
        e -= p * std::log2(p);
    }
    return e;
}

// w Bits ab Bitposition p; lsb = true: Bit 0 des Bytes zuerst.
uint32_t LiesBits(const std::vector<uint8_t>& b, size_t p, int w, bool lsb) {
    uint32_t v = 0;
    for (int i = 0; i < w; ++i) {
        const size_t q = p + static_cast<size_t>(i);
        const size_t byte = q >> 3;
        if (byte >= b.size()) return 0xFFFFFFFFu;
        const int bit = lsb ? static_cast<int>(q & 7) : 7 - static_cast<int>(q & 7);
        const uint32_t x = (b[byte] >> bit) & 1u;
        if (lsb) v |= x << i;
        else v = (v << 1) | x;
    }
    return v;
}

std::string Kurz(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 3) + "..."; }

// Sucht vier Werte (Toleranz +-1) als w-Bit-Folge mit Abstand abst*w Bits.
// Liefert die Anzahl der Fundstellen, erste = erste Bitposition.
size_t SucheFolge(const std::vector<uint8_t>& b, const uint32_t soll[4], int w, int abst, bool lsb, size_t& erste) {
    const size_t gesamtBits = b.size() * 8;
    const size_t schritt = static_cast<size_t>(w * abst);
    size_t hier = 0;
    erste = 0;
    for (size_t p = 0; p + 3 * schritt + static_cast<size_t>(w) <= gesamtBits; ++p) {
        bool gut = true;
        for (int j = 0; j < 4 && gut; ++j) {
            const uint32_t v = LiesBits(b, p + static_cast<size_t>(j) * schritt, w, lsb);
            const int64_t dlt = static_cast<int64_t>(v) - static_cast<int64_t>(soll[j]);
            gut = dlt >= -1 && dlt <= 1;
        }
        if (gut) { if (hier == 0) erste = p; ++hier; }
    }
    return hier;
}

} // namespace

int CodecLabor(int argc, char** argv) {
    const auto t0 = std::chrono::steady_clock::now();
    const std::string spielordner = argv[2];
    std::string cache, clipCache, logPfad = "codeclab.log", hexPfad = "codeclab_hex.txt";
    size_t probe = 200;
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
        if (std::strcmp(argv[i], "--clipcache") == 0) clipCache = argv[i + 1];
        if (std::strcmp(argv[i], "--log") == 0) logPfad = argv[i + 1];
        if (std::strcmp(argv[i], "--hex") == 0) hexPfad = argv[i + 1];
        if (std::strcmp(argv[i], "--probe") == 0) probe = static_cast<size_t>(std::atoi(argv[i + 1]));
    }
    // 0.69.0: die Codec-Runden sind abgeschlossen - standardmaessig nur noch
    // die Zuordnungs-Abschnitte (Z1, Z2). Mit --alles laeuft wieder alles.
    bool alles = false;
    for (int i = 3; i < argc; ++i) if (std::strcmp(argv[i], "--alles") == 0) alles = true;
    Log L;
    L.f = std::fopen(logPfad.c_str(), "wb");
    L("CODECLAB SWBF2 Import 1.13.0 - mehrere Ansaetze gleichzeitig\n");
    L("Methodik: Entropie/Abschnitte, bekannte Werte (RAW-Zwillinge), Differenzen, Hypothesen als Code.\n");
    L("Quellen: FOSDEM 2021 'Reverse-Engineering of (binary) File-Formats'; ServiceNow Security Lab 2025\n");
    L("         'Binary Data Analysis: The Role of Entropy'; Frechette, Animation Compression Library (VBR);\n");
    L("         Unreal-Engine-API (Per-Track-Codec, Festkomma mit Log2-Bereich).\n\n");

    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { L("FEHLER Spiel: %s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { L("FEHLER Index: %s\n", fehler.c_str()); return 1; }
    }
    fbanim::Quelle q;
    bool ausAblage = false;
    if (!clipCache.empty() && q.Lade(spiel, clipCache, spiel.Kopfnummer(), grund)) ausAblage = true;
    else {
        if (!q.Baue(spiel, idx, fehler)) { L("FEHLER Clipverzeichnis: %s\n", fehler.c_str()); return 1; }
        if (!clipCache.empty()) q.Speichere(clipCache, spiel.Kopfnummer(), fehler);
    }
    const auto& clips = q.Clips();
    L("Clipverzeichnis: %zu Clips (%s)\n\n", clips.size(), ausAblage ? "aus der Ablage" : "neu gebaut");

    // ==================================================================
    // Z1 (0.67.0) - Zuordnung wie im Spiel. Recherche (Nexus-Anleitungen):
    // Figuren haben "Actor"-EBX (Actor_Weapon_<Held>, Actor_Lightsaber_<Held>)
    // mit Komponenten, deren Feld AntGameStates in die ANT-Daten zeigt; Haltung
    // und Bewegungsstil kommen ueber die Waffen-Blueprint aus dem Kit
    // (Gameplay/Kits/Hero/<Held>/Kits/Kit_Hero_<Held>). Hier: alle EBX, deren
    // Pfad den Figurennamen enthaelt, nach AntRefs (Feld AssetGuid) absuchen,
    // in ANT-Schluessel umrechnen und im ANT-Graphen allen Verweisen folgen.
    // ==================================================================
    L("== Z1 ZUORDNUNG WIE IM SPIEL: EBX der Figur -> AntRefs -> ANT-Graph -> Clips ==\n");
    {
        std::map<std::string, size_t> keyZuClip;
        for (size_t i = 0; i < clips.size(); ++i) keyZuClip.emplace(clips[i].key, i);
        struct Ziel { const char* name; std::vector<const char*> pfadWoerter; };
        const std::vector<Ziel> ziele = {
            { "bobafett", { "bobafett", "boba_fett" } }, { "darthvader", { "darthvader", "vader" } }, { "anakin", { "anakin" } },
            { "lukeskywalker", { "luke" } }, { "rey", { "/rey/", "_rey_", "_rey" } }, { "hansolo", { "hansolo", "han_solo" } },
            { "chewbacca", { "chewbacca" } }, { "yoda", { "yoda" } }, { "iden", { "iden" } },
        };
        std::function<void(const fbebx::Wert&, std::vector<std::string>&, int)> sucheGuids;
        sucheGuids = [&](const fbebx::Wert& w, std::vector<std::string>& aus2, int tiefe) {
            if (tiefe > 40) return;
            if (const fbebx::Wert* g = w.Feldwert("AssetGuid")) if (!g->text.empty()) aus2.push_back(Klein(g->text));
            for (const auto& fe : w.felder) if (fe.wert) sucheGuids(*fe.wert, aus2, tiefe + 1);
            for (const auto& l : w.liste) if (l) sucheGuids(*l, aus2, tiefe + 1);
        };
        for (const Ziel& z : ziele) {
            const auto tz = std::chrono::steady_clock::now();
            std::vector<std::string> startKeys;
            std::map<std::string, size_t> jeEbx;
            size_t ebxGelesen = 0;
            for (const auto& kv : idx.ebx) {
                const std::string pfad = Klein(kv.first);
                bool passt = false;
                for (const char* w : z.pfadWoerter) if (pfad.find(w) != std::string::npos) { passt = true; break; }
                if (!passt) continue;
                std::vector<uint8_t> roh;
                std::string f2;
                fbebx::Datei e;
                if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) continue;
                ++ebxGelesen;
                std::vector<std::string> guids;
                for (const auto& o : e.Objekte()) if (o) sucheGuids(*o, guids, 0);
                for (const std::string& g : guids) {
                    if (g.find_first_not_of("0-") == std::string::npos) continue;
                    const std::string k = fbgesicht::KeyAusGuid(g);
                    if (!k.empty() && q.HatKey(k)) { startKeys.push_back(k); ++jeEbx[kv.first]; }
                }
            }
            const fbanim::Quelle::Reichweite rw = q.Folge(startKeys, 400000);
            size_t mitName = 0, ladbar = 0;
            for (const std::string& k : rw.clips) {
                const auto it = keyZuClip.find(k);
                if (it == keyZuClip.end()) continue;
                const fbanim::ClipEintrag& c = clips[it->second];
                if (Klein(c.anzeige).find(z.name) != std::string::npos) ++mitName;
                if (c.klasse != "CurveAnimationAsset") ++ladbar;
            }
            const double sek = std::chrono::duration<double>(std::chrono::steady_clock::now() - tz).count();
            L("\n  %s: %zu EBX mit dem Namen im Pfad, %zu AntRefs in den ANT-Daten gefunden, %zu Knoten besucht%s, %zu Clips erreicht (%zu tragen den Namen, %zu ladbar), %.1f s\n",
              z.name, ebxGelesen, rw.startGefunden, rw.knoten, rw.abgebrochen ? " (ABGEBROCHEN)" : "", rw.clips.size(), mitName, ladbar, sek);
            std::vector<std::pair<size_t, std::string>> ebxRang;
            for (const auto& kv : jeEbx) ebxRang.push_back({ kv.second, kv.first });
            std::sort(ebxRang.rbegin(), ebxRang.rend());
            for (size_t i = 0; i < ebxRang.size() && i < 8; ++i) L("    AntRefs aus %s: %zu\n", ebxRang[i].second.c_str(), ebxRang[i].first);
            std::vector<std::pair<size_t, std::string>> typRang;
            for (const auto& kv : rw.typen) typRang.push_back({ kv.second, kv.first });
            std::sort(typRang.rbegin(), typRang.rend());
            L("    besuchte Klassen:");
            for (size_t i = 0; i < typRang.size() && i < 12; ++i) L(" %s %zu,", typRang[i].second.c_str(), typRang[i].first);
            L("\n    Beispiele:");
            size_t gezeigt = 0;
            for (const std::string& k : rw.clips) {
                const auto it = keyZuClip.find(k);
                if (it == keyZuClip.end() || clips[it->second].anzeige.empty()) continue;
                L(" %s |", clips[it->second].anzeige.c_str());
                if (++gezeigt >= 25) break;
            }
            L("\n");
        }
    }

    // ==================================================================
    // Z2 (0.69.0) - wie waehlt der gemeinsame Zustandsautomat den Helden aus?
    // Z1 zeigte: alle Lichtschwert-Helden erreichen dieselben 272 Clips, die
    // Auswahl steckt in Tag-/Zustandsbedingungen. Hier fuer Vader und Boba:
    //  (a) die Wurzeln (direkt aus den EBX verwiesene ANT-Assets): Klasse, Name,
    //      Felder - welche Werte setzt die Figur?
    //  (b) Beispiele der Knotenklassen des Automaten mit allen Feldern und den
    //      Klassen der verwiesenen Assets - wie sehen Bedingungen aus?
    //  (c) Hex-Werte nach Laenge: stecken Verweise auch als 16-Byte-GUID drin
    //      (die Folge() bisher nicht nimmt)?
    // ==================================================================
    L("\n== Z2 AUFBAU DES ZUSTANDSAUTOMATEN (Wurzeln, Knotenbeispiele, Verweisformen) ==\n");
    {
        std::function<void(const fbebx::Wert&, std::vector<std::string>&, int)> guidsZ2;
        guidsZ2 = [&](const fbebx::Wert& w, std::vector<std::string>& aus2, int tiefe) {
            if (tiefe > 40) return;
            if (const fbebx::Wert* g = w.Feldwert("AssetGuid")) if (!g->text.empty()) aus2.push_back(Klein(g->text));
            for (const auto& fe : w.felder) if (fe.wert) guidsZ2(*fe.wert, aus2, tiefe + 1);
            for (const auto& l : w.liste) if (l) guidsZ2(*l, aus2, tiefe + 1);
        };
        // Kurzbeschreibung eines Wertes; Schluessel werden mit Klasse und Name aufgeloest
        std::function<std::string(const fbgd::Wert&, int)> zeig;
        zeig = [&](const fbgd::Wert& w, int tiefe) -> std::string {
            char t[160];
            switch (w.art) {
                case fbgd::Wert::Art::Hex: {
                    std::string r = w.text;
                    if (w.text.size() == 16) {
                        fbgd::Datensatz d2; std::string kl;
                        if (q.LiesAsset(w.text, d2, kl)) r += "->" + kl + (d2.nameDa && !d2.name.empty() ? "'" + d2.name + "'" : "");
                    }
                    return r;
                }
                case fbgd::Wert::Art::Text: return "\"" + w.text + "\"";
                case fbgd::Wert::Art::Gleit: std::snprintf(t, sizeof t, "%g", static_cast<double>(w.gleit)); return t;
                case fbgd::Wert::Art::Ganz: case fbgd::Wert::Art::Bool: std::snprintf(t, sizeof t, "%lld", static_cast<long long>(w.ganz)); return t;
                case fbgd::Wert::Art::Feld: {
                    std::string r = "[" + std::to_string(w.anzahl) + "]{";
                    for (size_t i = 0; i < w.werte.size() && i < 8; ++i) r += (i ? ", " : "") + zeig(w.werte[i], tiefe + 1);
                    for (size_t i = 0; i < w.saetze.size() && i < 4 && tiefe < 3; ++i) {
                        r += " (";
                        for (const auto& p : w.saetze[i]) r += p.first + "=" + zeig(p.second, tiefe + 1) + " ";
                        r += ")";
                    }
                    return r + (w.werte.size() > 8 || w.saetze.size() > 4 ? " ...}" : "}");
                }
                default: return "-";
            }
        };
        auto dump = [&](const std::string& key, const char* praefix) {
            fbgd::Datensatz d2; std::string kl;
            if (!q.LiesAsset(key, d2, kl)) return;
            std::string z = std::string(praefix) + kl + " '" + (d2.nameDa ? d2.name : std::string()) + "' " + key + ":";
            for (const auto* fs : { &d2.basis, &d2.felder })
                for (const auto& p : *fs) {
                    if (p.first == "__key" || p.first == "__base" || p.first == "__name") continue;
                    z += " " + p.first + "=" + zeig(p.second, 0) + ";";
                }
            if (z.size() > 1600) z = z.substr(0, 1600) + " ...";
            L("%s\n", z.c_str());
        };
        std::map<std::string, std::vector<std::string>> beispielJeKlasse;
        std::map<size_t, size_t> hexLaengen;
        size_t guid32 = 0, guid32Treffer = 0, guid32TrefferDreh = 0;
        for (const char* held : { "darthvader", "bobafett" }) {
            std::vector<std::string> start;
            std::map<std::string, std::string> herkunft;
            for (const auto& kv : idx.ebx) {
                const std::string pfad = Klein(kv.first);
                if (pfad.find(held) == std::string::npos) continue;
                if (pfad.find("specializations/") == std::string::npos && pfad.find("/actor_") == std::string::npos) continue;
                std::vector<uint8_t> roh; std::string f2; fbebx::Datei e;
                if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) continue;
                std::vector<std::string> guids;
                for (const auto& o : e.Objekte()) if (o) guidsZ2(*o, guids, 0);
                for (const std::string& g : guids) {
                    const std::string k = fbgesicht::KeyAusGuid(g);
                    if (!k.empty() && q.HatKey(k)) { start.push_back(k); herkunft.emplace(k, kv.first); }
                }
            }
            std::sort(start.begin(), start.end());
            start.erase(std::unique(start.begin(), start.end()), start.end());
            L("\n  %s: %zu Wurzeln (aus specializations/ und actor_):\n", held, start.size());
            std::map<std::string, size_t> wurzelKlassen;
            for (const std::string& k : start) { fbgd::Datensatz d2; std::string kl; if (q.LiesAsset(k, d2, kl)) ++wurzelKlassen[kl]; }
            L("    Klassen der Wurzeln:");
            for (const auto& kv : wurzelKlassen) L(" %s %zu,", kv.first.c_str(), kv.second);
            L("\n");
            size_t gezeigt = 0;
            for (const std::string& k : start) {
                if (gezeigt >= 30) break;
                fbgd::Datensatz d2; std::string kl;
                if (!q.LiesAsset(k, d2, kl)) continue;
                if (kl == "ClipControllerAsset") continue;
                dump(k, "    W ");
                ++gezeigt;
            }
            // Beispiele je Klasse und Hex-Laengen aus dem erreichbaren Teil
            const fbanim::Quelle::Reichweite rw = q.Folge(start, 400000);
            (void)rw;
            std::vector<std::string> offen(start);
            std::set<std::string> gesehen(start.begin(), start.end());
            size_t besucht = 0;
            while (!offen.empty() && besucht < 20000) {
                const std::string k = offen.back(); offen.pop_back(); ++besucht;
                fbgd::Datensatz d2; std::string kl;
                if (!q.LiesAsset(k, d2, kl)) continue;
                auto& bsp = beispielJeKlasse[kl];
                if (bsp.size() < 3) bsp.push_back(k);
                std::function<void(const fbgd::Wert&)> lauf;
                lauf = [&](const fbgd::Wert& w) {
                    if (w.art == fbgd::Wert::Art::Hex) {
                        ++hexLaengen[w.text.size()];
                        if (w.text.size() == 16 && q.HatKey(w.text) && gesehen.insert(w.text).second) offen.push_back(w.text);
                        if (w.text.size() == 32) {
                            ++guid32;
                            if (q.HatKey(w.text.substr(0, 16))) ++guid32Treffer;
                            const std::string k2 = fbgesicht::KeyAusGuid(w.text);
                            if (!k2.empty() && q.HatKey(k2)) ++guid32TrefferDreh;
                        }
                    }
                    for (const auto& x : w.werte) lauf(x);
                    for (const auto& satz : w.saetze) for (const auto& p : satz) lauf(p.second);
                };
                for (const auto* fs : { &d2.basis, &d2.felder })
                    for (const auto& p : *fs) if (p.first != "__key" && p.first != "__base") lauf(p.second);
            }
        }
        L("\n  Hex-Werte nach Laenge:");
        for (const auto& kv : hexLaengen) L(" %zu Zeichen: %zu,", kv.first, kv.second);
        L("\n  32-stellige Werte: %zu, davon als Schluessel (erste 16) %zu, (umgedreht wie AntRef) %zu\n", guid32, guid32Treffer, guid32TrefferDreh);
        L("\n  Beispiele je Knotenklasse:\n");
        for (const char* kl : { "BranchOutTagCollectionAsset", "TagCollectionSetAsset", "DefaultTagCollectionAsset", "EnumerationValueAsset",
                                "StateFlowTransitionAsset", "FloatGameStateTag", "BoolGameStateTag", "LayoutHierarchyAsset", "LayoutAsset",
                                "JointMapTemplateIDAsset", "ClipControllerAsset", "ClipVirtualAsset" }) {
            const auto it = beispielJeKlasse.find(kl);
            if (it == beispielJeKlasse.end()) continue;
            for (const std::string& k : it->second) dump(k, "    K ");
        }
        L("  weitere Klassen:");
        for (const auto& kv : beispielJeKlasse) L(" %s,", kv.first.c_str());
        L("\n");
    }

    // ==================================================================
    // Z3 (0.70.0) - die Heldenwahl im Automaten. Z2 fand unter Vaders
    // Wurzeln 'HeroCharacter.EnumGS' (GameStateEnumerationAsset ->
    // EnumerationAsset 'HeroCharacter.Enum'); EnumerationValueAssets tragen
    // Enum + Value; Uebergaenge haben ConditionsRequiredTrue/False.
    //  (a) EBX-Seite: welche Werte setzt die Figur? - jedes EBX-Objekt mit
    //      einem AntRef auf einen GameState, samt seiner uebrigen Felder.
    //  (b) die Werte von HeroCharacter.Enum im Graphen (Name, Value) und wer
    //      auf sie verweist.
    //  (c) fuer drei Vader-ClipController die Kette nach oben bis zu einem
    //      Knoten, der einen HeroCharacter-Wert oder -Zustand verwendet.
    // ==================================================================
    L("\n== Z3 HELDENWAHL IM AUTOMATEN (HeroCharacter.Enum) ==\n");
    {
        auto klasseUndName = [&](const std::string& k) {
            fbgd::Datensatz d2; std::string kl;
            if (!q.LiesAsset(k, d2, kl)) return std::string("?");
            return kl + " '" + (d2.nameDa ? d2.name : std::string()) + "'";
        };
        // (a) EBX-Seite
        std::function<void(const fbebx::Wert&, int, std::vector<std::string>&)> antRefsMitEltern;
        antRefsMitEltern = [&](const fbebx::Wert& o, int tiefe, std::vector<std::string>& aus2) {
            if (tiefe > 30) return;
            for (const auto& fe : o.felder) {
                if (!fe.wert) continue;
                if (const fbebx::Wert* g = fe.wert->Feldwert("AssetGuid")) {
                    const std::string k = fbgesicht::KeyAusGuid(Klein(g->text));
                    if (!k.empty() && q.HatKey(k)) {
                        const std::string kn = klasseUndName(k);
                        if (kn.find("GameState") != std::string::npos || kn.find("Enum") != std::string::npos) {
                            std::string z = o.typ + "." + fe.name + " -> " + kn + " |";
                            for (const auto& f2 : o.felder) {
                                if (!f2.wert || &f2 == &fe) continue;
                                const fbebx::Wert& v = *f2.wert;
                                char t[120];
                                if (v.art == fbebx::Art::Ganz || v.art == fbebx::Art::Bool) std::snprintf(t, sizeof t, " %s=%lld", f2.name.c_str(), static_cast<long long>(v.zahl));
                                else if (v.art == fbebx::Art::Gleit) std::snprintf(t, sizeof t, " %s=%g", f2.name.c_str(), v.gleit);
                                else if (v.art == fbebx::Art::Text) std::snprintf(t, sizeof t, " %s=\"%.60s\"", f2.name.c_str(), v.text.c_str());
                                else std::snprintf(t, sizeof t, " %s(%s)", f2.name.c_str(), v.typ.c_str());
                                z += t;
                            }
                            aus2.push_back(z);
                        }
                    }
                }
                antRefsMitEltern(*fe.wert, tiefe + 1, aus2);
            }
            for (const auto& l : o.liste) if (l) antRefsMitEltern(*l, tiefe + 1, aus2);
        };
        std::vector<std::string> startVader;
        for (const auto& kv : idx.ebx) {
            const std::string pfad = Klein(kv.first);
            if (pfad.find("darthvader") == std::string::npos) continue;
            if (pfad.find("specializations/") == std::string::npos && pfad.find("/actor_") == std::string::npos) continue;
            std::vector<uint8_t> roh; std::string f2; fbebx::Datei e;
            if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) continue;
            std::vector<std::string> zeilen;
            for (const auto& o : e.Objekte()) if (o) antRefsMitEltern(*o, 0, zeilen);
            std::sort(zeilen.begin(), zeilen.end());
            zeilen.erase(std::unique(zeilen.begin(), zeilen.end()), zeilen.end());
            L("  (a) %s: %zu Zustands-Verweise\n", kv.first.c_str(), zeilen.size());
            for (size_t i = 0; i < zeilen.size() && i < 25; ++i) L("      %s\n", zeilen[i].c_str());
            std::function<void(const fbebx::Wert&, int)> g2;
            g2 = [&](const fbebx::Wert& w, int tiefe) {
                if (tiefe > 40) return;
                if (const fbebx::Wert* g = w.Feldwert("AssetGuid")) { const std::string k = fbgesicht::KeyAusGuid(Klein(g->text)); if (!k.empty() && q.HatKey(k)) startVader.push_back(k); }
                for (const auto& fe : w.felder) if (fe.wert) g2(*fe.wert, tiefe + 1);
                for (const auto& l : w.liste) if (l) g2(*l, tiefe + 1);
            };
            for (const auto& o : e.Objekte()) if (o) g2(*o, 0);
        }
        // Graph mit Rueckverweisen
        std::map<std::string, std::vector<std::string>> eltern;
        std::map<std::string, std::string> klasseVon;
        std::vector<std::string> offen(startVader);
        std::set<std::string> gesehen(startVader.begin(), startVader.end());
        std::string heroEnum;
        size_t besucht = 0;
        while (!offen.empty() && besucht < 60000) {
            const std::string k = offen.back(); offen.pop_back(); ++besucht;
            fbgd::Datensatz d2; std::string kl;
            if (!q.LiesAsset(k, d2, kl)) continue;
            klasseVon[k] = kl + " '" + (d2.nameDa ? d2.name : std::string()) + "'";
            if (kl == "EnumerationAsset" && d2.nameDa && d2.name == "HeroCharacter.Enum") heroEnum = k;
            std::function<void(const fbgd::Wert&)> lauf;
            lauf = [&](const fbgd::Wert& w) {
                if (w.art == fbgd::Wert::Art::Hex && w.text.size() == 16 && q.HatKey(w.text)) {
                    eltern[w.text].push_back(k);
                    if (gesehen.insert(w.text).second) offen.push_back(w.text);
                }
                for (const auto& x : w.werte) lauf(x);
                for (const auto& satz : w.saetze) for (const auto& p2 : satz) lauf(p2.second);
            };
            for (const auto* fs : { &d2.basis, &d2.felder }) for (const auto& p2 : *fs) if (p2.first != "__key" && p2.first != "__base") lauf(p2.second);
        }
        L("  Graph ab Vaders Wurzeln: %zu Knoten\n", besucht);
        // (b) Werte von HeroCharacter.Enum
        if (!heroEnum.empty()) {
            fbgd::Datensatz de; std::string kl;
            if (q.LiesAsset(heroEnum, de, kl)) {
                std::string z = "  (b) HeroCharacter.Enum " + heroEnum + ":";
                for (const auto& p2 : de.felder) {
                    if (p2.first.rfind("__", 0) == 0) continue;
                    z += " " + p2.first + "[" + std::to_string(p2.second.anzahl) + "]";
                    for (size_t i = 0; i < p2.second.werte.size() && i < 60; ++i) z += (i ? "," : "=") + (p2.second.werte[i].text.empty() ? std::to_string(p2.second.werte[i].ganz) : p2.second.werte[i].text);
                    for (size_t i = 0; i < p2.second.saetze.size() && i < 60; ++i) { z += " ("; for (const auto& p3 : p2.second.saetze[i]) z += p3.first + "=" + (p3.second.text.empty() ? std::to_string(p3.second.ganz) : p3.second.text) + " "; z += ")"; }
                }
                L("%s\n", z.substr(0, 3000).c_str());
            }
            size_t werte = 0;
            for (const auto& kv : klasseVon) {
                if (kv.second.rfind("EnumerationValueAsset", 0) != 0) continue;
                fbgd::Datensatz dv; std::string kl2;
                if (!q.LiesAsset(kv.first, dv, kl2)) continue;
                const fbgd::Wert* ea = fbgd::Feld(dv.felder, "EnumerationAsset");
                if (ea == nullptr || ea->text != heroEnum) continue;
                const fbgd::Wert* v = fbgd::Feld(dv.felder, "Value");
                std::string z = "      Wert " + kv.second + " Value=" + (v ? std::to_string(v->ganz) : std::string("?")) + " <- ";
                const auto el = eltern.find(kv.first);
                if (el != eltern.end()) for (size_t i = 0; i < el->second.size() && i < 4; ++i) z += klasseVon[el->second[i]] + "; ";
                if (++werte <= 40) L("%s\n", z.c_str());
            }
            L("      %zu Werte von HeroCharacter.Enum im Graphen\n", werte);
        } else L("  (b) HeroCharacter.Enum im Graphen nicht gefunden\n");
        // (c) Ketten nach oben fuer Vader-Clips
        size_t ketten = 0;
        for (const auto& kv : klasseVon) {
            if (kv.second.rfind("ClipControllerAsset", 0) != 0 || kv.second.find("Vader") == std::string::npos) continue;
            if (++ketten > 3) break;
            L("  (c) Kette nach oben ab %s:\n", kv.second.c_str());
            std::string k = kv.first;
            for (int stufe = 0; stufe < 10; ++stufe) {
                const auto el = eltern.find(k);
                if (el == eltern.end() || el->second.empty()) break;
                std::string z = "      ^ ";
                for (size_t i = 0; i < el->second.size() && i < 3; ++i) z += klasseVon[el->second[i]] + (i + 1 < el->second.size() && i < 2 ? " | " : "");
                if (el->second.size() > 3) z += " (+" + std::to_string(el->second.size() - 3) + ")";
                L("%s\n", z.c_str());
                k = el->second[0];
            }
        }
    }

    // ==================================================================
    // Z4 (0.71.0) - Befund Z3: die Held-EBX schreibt WriteEnumerationGameState
    // HeroCharacter.EnumGS = Wert (Vader 2, Luke 1, Boba Fett 3, Han 4, Leia 5,
    // Palpatine 6, Yoda 7, Kylo 8, Lando 9, Phasma 13, Rey 15, Iden 16, B2 17,
    // Finn 19, Ewok 20, ObiWan 21, Anakin 22, Dooku 23 ...). Vaders Clips haengen
    // unter IndexChooserControllerAssets ('HeroMelee.Idle.AimFwd.Index' ...).
    //  (a) Aufbau der Auswahlknoten (IndexChooser, Chooser, Eintraege,
    //      EnumerationEnumeratorPair, Steering) mit allen Feldern, und jeder
    //      Knoten, der HeroCharacter.EnumGS direkt verwendet. Daraus ergibt
    //      sich im naechsten Schritt der Helden-Filter fuer den Graphen.
    // ==================================================================
    L("\n== Z4 AUSWAHLKNOTEN UND HELDEN-FILTER ==\n");
    {
        const std::string heroGS = "2446b7a061d9b848";                       // HeroCharacter.EnumGS (Z2/Z3)
        std::function<std::string(const fbgd::Wert&, int)> zeig4;
        zeig4 = [&](const fbgd::Wert& w, int tiefe) -> std::string {
            char t[120];
            switch (w.art) {
                case fbgd::Wert::Art::Hex: {
                    std::string r = w.text;
                    if (w.text.size() == 16) { fbgd::Datensatz d2; std::string kl; if (q.LiesAsset(w.text, d2, kl)) r += "->" + kl + (d2.nameDa ? "'" + d2.name + "'" : ""); }
                    return r;
                }
                case fbgd::Wert::Art::Gleit: std::snprintf(t, sizeof t, "%g", static_cast<double>(w.gleit)); return t;
                case fbgd::Wert::Art::Ganz: case fbgd::Wert::Art::Bool: std::snprintf(t, sizeof t, "%lld", static_cast<long long>(w.ganz)); return t;
                case fbgd::Wert::Art::Text: return "\"" + w.text + "\"";
                case fbgd::Wert::Art::Feld: {
                    std::string r = "[" + std::to_string(w.anzahl) + "]{";
                    for (size_t i = 0; i < w.werte.size() && i < 50; ++i) r += (i ? ", " : "") + std::to_string(i) + ":" + zeig4(w.werte[i], tiefe + 1);
                    for (size_t i = 0; i < w.saetze.size() && i < 50 && tiefe < 3; ++i) { r += " (" + std::to_string(i) + ": "; for (const auto& p2 : w.saetze[i]) r += p2.first + "=" + zeig4(p2.second, tiefe + 1) + " "; r += ")"; }
                    return r + "}";
                }
                default: return "-";
            }
        };
        auto dump4 = [&](const std::string& key) {
            fbgd::Datensatz d2; std::string kl;
            if (!q.LiesAsset(key, d2, kl)) return;
            std::string z = "    " + kl + " '" + (d2.nameDa ? d2.name : std::string()) + "' " + key + ":";
            for (const auto* fs : { &d2.basis, &d2.felder })
                for (const auto& p2 : *fs) { if (p2.first.rfind("__", 0) == 0) continue; z += " " + p2.first + "=" + zeig4(p2.second, 0) + ";"; }
            if (z.size() > 4000) z = z.substr(0, 4000) + " ...";
            L("%s\n", z.c_str());
        };
        // Wurzeln je Held wie in Z1 (EBX mit dem Namen im Pfad)
        auto wurzeln = [&](const std::vector<const char*>& woerter) {
            std::vector<std::string> st;
            std::function<void(const fbebx::Wert&, int)> g4;
            g4 = [&](const fbebx::Wert& w, int tiefe) {
                if (tiefe > 40) return;
                if (const fbebx::Wert* g = w.Feldwert("AssetGuid")) { const std::string k = fbgesicht::KeyAusGuid(Klein(g->text)); if (!k.empty() && q.HatKey(k)) st.push_back(k); }
                for (const auto& fe : w.felder) if (fe.wert) g4(*fe.wert, tiefe + 1);
                for (const auto& l : w.liste) if (l) g4(*l, tiefe + 1);
            };
            for (const auto& kv : idx.ebx) {
                const std::string pfad = Klein(kv.first);
                bool passt = false;
                for (const char* w : woerter) if (pfad.find(w) != std::string::npos) passt = true;
                if (!passt) continue;
                std::vector<uint8_t> roh; std::string f2; fbebx::Datei e;
                if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) continue;
                for (const auto& o : e.Objekte()) if (o) g4(*o, 0);
            }
            return st;
        };
        // (a) Beispiele der Auswahlknoten aus Vaders Graph
        {
            const std::vector<std::string> st = wurzeln({ "darthvader" });
            std::map<std::string, size_t> gezeigt;
            std::vector<std::string> offen(st);
            std::set<std::string> gesehen(st.begin(), st.end());
            size_t besucht = 0;
            size_t mitHero = 0;
            while (!offen.empty() && besucht < 60000) {
                const std::string k = offen.back(); offen.pop_back(); ++besucht;
                fbgd::Datensatz d2; std::string kl;
                if (!q.LiesAsset(k, d2, kl)) continue;
                std::vector<std::string> kinder;
                bool nutztHero = false;
                std::function<void(const fbgd::Wert&)> lauf;
                lauf = [&](const fbgd::Wert& w) {
                    if (w.art == fbgd::Wert::Art::Hex && w.text.size() == 16 && q.HatKey(w.text)) { kinder.push_back(w.text); if (w.text == heroGS) nutztHero = true; }
                    for (const auto& x : w.werte) lauf(x);
                    for (const auto& satz : w.saetze) for (const auto& p2 : satz) lauf(p2.second);
                };
                for (const auto* fs : { &d2.basis, &d2.felder }) for (const auto& p2 : *fs) if (p2.first.rfind("__", 0) != 0) lauf(p2.second);
                const bool auswahl = kl.find("Chooser") != std::string::npos || kl == "EnumerationEnumeratorPair" || kl == "SteeringControllerAsset";
                if (nutztHero && mitHero < 6) { L("  (a) verwendet HeroCharacter.EnumGS:\n"); dump4(k); ++mitHero; }
                else if (auswahl && gezeigt[kl] < 2) { dump4(k); ++gezeigt[kl]; }
                for (const std::string& c : kinder) if (gesehen.insert(c).second) offen.push_back(c);
            }
            L("  (a) %zu Knoten, %zu verwenden HeroCharacter.EnumGS direkt (gezeigt bis 6)\n", besucht, mitHero);
        }
    }

    // ==================================================================
    // Z5 (0.72.0) - der Helden-Filter, wie ihn jetzt das Animationsfenster
    // nutzt (fbzuordnung::LiesWurzeln + Quelle::FolgeFuerHeld), je Held.
    // ==================================================================
    L("\n== Z5 HELDEN-FILTER JE HELD (wie im Animationsfenster) ==\n");
    {
        std::map<std::string, size_t> keyZuClip5;
        for (size_t i = 0; i < clips.size(); ++i) keyZuClip5.emplace(clips[i].key, i);
        for (const char* figur : { "darthvader", "lukeskywalker", "rey", "anakin", "generalgrievous", "darthmaul", "yoda", "countdooku",
                                   "bobafett", "hansolo", "chewbacca", "kyloren", "emperorpalpatine", "iden" }) {
            const auto tz = std::chrono::steady_clock::now();
            const fbzuordnung::Wurzeln w = fbzuordnung::LiesWurzeln(spiel, idx, q, figur);
            fbanim::Quelle::Reichweite rw;
            if (w.held >= 0) rw = q.FolgeFuerHeld(w.keys, w.heldGS, w.held, 400000);
            std::vector<std::string> suchw = fbzuordnung::Suchwoerter(figur);
            size_t eigen = 0, ladbar = 0;
            std::string beispiele;
            size_t n = 0;
            L("  %-17s alle erreichten Clips %zu, davon ueber eine Heldenweiche %zu:\n", figur, rw.clips.size(), rw.clipsHeld.size());
            for (const std::string& k : rw.clipsHeld) {
                const auto it = keyZuClip5.find(k);
                if (it == keyZuClip5.end()) continue;
                const fbanim::ClipEintrag& c = clips[it->second];
                const std::string an = Klein(c.anzeige);
                bool mitName = false;
                for (const std::string& sw : suchw) if (sw.size() >= 3 && an.find(sw) != std::string::npos) mitName = true;
                if (mitName) ++eigen;
                if (c.klasse != "CurveAnimationAsset") ++ladbar;
                if (n++ < 30 && !c.anzeige.empty()) beispiele += c.anzeige + " | ";
            }
            const double sek = std::chrono::duration<double>(std::chrono::steady_clock::now() - tz).count();
            L("  %-17s Heldenwert %3d, %3zu EBX, %4zu Wurzeln -> %4zu heldbezogene Clips (%zu mit eigenem Namen, %zu ladbar), %.1f s\n    %s\n", figur, w.held, w.ebx,
              w.keys.size(), rw.clipsHeld.size(), eigen, ladbar, sek, beispiele.substr(0, 900).c_str());
        }
    }

    // ==================================================================
    // Z7 (0.75.0) - der Zustands-Filter (Held UND Waffe UND Koerper), wie ihn
    // jetzt das Animationsfenster nutzt. Z6 zeigte: Blaster-Figuren verzweigen
    // an WeaponType.EnumGS (Liste 9), SpecificWeapon.EnumGS (86) und
    // Game.Character.BodyType.EnumGS (33); Stormtrooper schreiben z. B.
    // WeaponType 0, 2, 7 und viele CharacterState-Werte.
    // ==================================================================
    L("\n== Z7 ZUSTANDS-FILTER (Held, Waffe, Koerper) JE FIGUR ==\n");
    {
        std::map<std::string, size_t> keyZuClip7;
        for (size_t i = 0; i < clips.size(); ++i) keyZuClip7.emplace(clips[i].key, i);
        for (const char* figur : { "darthvader", "generalgrievous", "bobafett", "hansolo", "iden", "#assault", "#heavy", "#officer", "#droid" }) {
            const auto tz = std::chrono::steady_clock::now();
            const fbzuordnung::Wurzeln w = fbzuordnung::LiesWurzeln(spiel, idx, q, figur);
            std::string zst;
            for (const auto& kv : w.zustaende) {
                const auto nm = w.zustandName.find(kv.first);
                zst += (nm != w.zustandName.end() ? nm->second : kv.first) + "=";
                for (size_t i = 0; i < kv.second.size() && i < 6; ++i) zst += (i ? "," : "") + std::to_string(kv.second[i]);
                zst += "; ";
            }
            fbanim::Quelle::Reichweite rw;
            if (!w.keys.empty() && !w.zustaende.empty()) rw = q.FolgeFuerZustaende(w.keys, w.zustaende, 400000);
            std::string beispiele;
            size_t n = 0, ladbar = 0;
            for (const std::string& k : rw.clipsHeld) {
                const auto it = keyZuClip7.find(k);
                if (it == keyZuClip7.end()) continue;
                if (clips[it->second].klasse != "CurveAnimationAsset") ++ladbar;
                if (n++ < 20 && !clips[it->second].anzeige.empty()) beispiele += clips[it->second].anzeige + " | ";
            }
            // 0.76.0: 1p-Clips (Ego-Perspektive, eigenes Armskelett) getrennt zaehlen
            size_t einsP = 0;
            for (const std::string& k : rw.clipsHeld) {
                const auto it = keyZuClip7.find(k);
                if (it == keyZuClip7.end()) continue;
                const std::string an = Klein(clips[it->second].anzeige);
                if (an.find("1p") != std::string::npos) ++einsP;
            }
            L("  %-16s %zu EBX, %zu Wurzeln, Zustaende: %s\n    -> %zu Clips gesamt, %zu ueber eine Weiche (%zu ladbar, davon %zu in Ego-Perspektive), %.1f s\n    %s\n", figur, w.ebx,
              w.keys.size(), zst.substr(0, 500).c_str(), rw.clips.size(), rw.clipsHeld.size(), ladbar, einsP,
              std::chrono::duration<double>(std::chrono::steady_clock::now() - tz).count(), beispiele.substr(0, 700).c_str());
            std::vector<std::pair<size_t, std::string>> herkunft;
            for (const auto& kv : w.ausEbx) herkunft.push_back({ kv.second, kv.first });
            std::sort(herkunft.rbegin(), herkunft.rend());
            for (size_t i = 0; i < herkunft.size() && i < 6; ++i) L("      Wurzeln aus %s: %zu\n", herkunft[i].second.c_str(), herkunft[i].first);
        }
    }

    // ==================================================================
    // Z11 (0.80.0) - der Filter mit den Automaten-Wurzeln, wie ihn jetzt das
    // Animationsfenster nutzt. Z10: '.3P.Top.SF' (Bank coop_nt_mc85) ist der
    // Automat der sichtbaren Figur - Soldat 123 Clips, Han 47, alle ohne
    // Ego-Perspektive; '.HeroMelee.Top.SF' liefert Han und Boba ihre
    // Emotes/Nahkampf (je 26).
    // ==================================================================
    L("\n== Z11 FILTER MIT AUTOMATEN-WURZELN ==\n");
    {
        const auto tz = std::chrono::steady_clock::now();
        const std::vector<std::string>& autom = fbzuordnung::AutomatenWurzeln(q);
        L("  %zu Automaten gefunden (%.1f s)\n", autom.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - tz).count());
        std::map<std::string, size_t> keyZuClip11;
        for (size_t i = 0; i < clips.size(); ++i) keyZuClip11.emplace(clips[i].key, i);
        for (const char* figur : { "darthvader", "generalgrievous", "hansolo", "bobafett", "iden", "#assault", "#heavy", "#officer", "#specialist", "#droid" }) {
            const auto t2 = std::chrono::steady_clock::now();
            const fbzuordnung::Wurzeln w = fbzuordnung::LiesWurzeln(spiel, idx, q, figur);
            std::vector<std::string> wurzeln = w.keys;
            for (const std::string& k : autom) wurzeln.push_back(k);
            fbanim::Quelle::Reichweite rw;
            if (!wurzeln.empty() && !w.zustaende.empty()) rw = q.FolgeFuerZustaende(wurzeln, w.zustaende, 400000);
            size_t ladbar = 0, einsP = 0, mitName = 0;
            std::string bsp;
            size_t m = 0;
            const std::vector<std::string> such = fbzuordnung::Suchwoerter(figur);
            size_t passend = 0;
            for (const std::string& k : rw.clipsHeld) {
                const auto it = keyZuClip11.find(k);
                if (it == keyZuClip11.end()) continue;
                const std::string an = Klein(clips[it->second].anzeige);
                if (!fbzuordnung::PasstZumSkelett(figur, an)) continue;             // 0.81.0
                ++passend;
                if (clips[it->second].klasse != "CurveAnimationAsset") ++ladbar;
                if (an.find("1p") != std::string::npos) ++einsP;
                for (const std::string& sw : such) if (sw.size() >= 3 && an.find(sw) != std::string::npos) { ++mitName; break; }
                if (m++ < 14 && !clips[it->second].anzeige.empty()) bsp += clips[it->second].anzeige + " | ";
            }
            // 1.05.0: Welche Weichen fragt der Automat ab, zu denen die Figur
            // KEINEN Wert schreibt? Bei Boba Fett ist das der Waffentyp - deshalb
            // bleiben seine 3p-Clips (Jetpack, Blaster) aus.
            {
                std::map<std::string, size_t> offen;
                std::vector<std::string> off2(wurzeln);
                std::set<std::string> ges(wurzeln.begin(), wurzeln.end());
                size_t besucht2 = 0;
                while (!off2.empty() && besucht2 < 60000) {
                    const std::string k = off2.back(); off2.pop_back(); ++besucht2;
                    fbgd::Datensatz d2; std::string kl;
                    if (!q.LiesAsset(k, d2, kl)) continue;
                    const fbgd::Wert* liste = kl == "IndexChooserControllerAsset" ? fbgd::Feld(d2.felder, "ChoiceAssetList") : nullptr;
                    std::vector<std::string> kinder;
                    std::function<void(const fbgd::Wert&)> lauf2;
                    lauf2 = [&](const fbgd::Wert& x) {
                        if (x.art == fbgd::Wert::Art::Hex && x.text.size() == 16 && q.HatKey(x.text)) kinder.push_back(x.text);
                        for (const auto& y : x.werte) lauf2(y);
                        for (const auto& satz : x.saetze) for (const auto& p2 : satz) lauf2(p2.second);
                    };
                    for (const auto* fs : { &d2.basis, &d2.felder }) for (const auto& p2 : *fs) if (p2.first.rfind("__", 0) != 0) lauf2(p2.second);
                    if (liste != nullptr)
                        for (const auto& p2 : d2.felder) {
                            if (p2.second.art != fbgd::Wert::Art::Hex || p2.second.text.size() != 16 || &p2.second == liste) continue;
                            if (w.zustaende.count(p2.second.text)) continue;
                            fbgd::Datensatz d3; std::string kl3;
                            if (q.LiesAsset(p2.second.text, d3, kl3) && d3.nameDa) ++offen[d3.name];
                        }
                    for (const std::string& c : kinder) if (ges.insert(c).second) off2.push_back(c);
                }
                std::vector<std::pair<size_t, std::string>> or2;
                for (const auto& kv : offen) or2.push_back({ kv.second, kv.first });
                std::sort(or2.rbegin(), or2.rend());
                std::string z2;
                for (size_t i = 0; i < or2.size() && i < 5; ++i) z2 += or2[i].second + " (" + std::to_string(or2[i].first) + "x) ";
                if (!z2.empty()) L("    ohne eigenen Wert, aber abgefragt: %s\n", z2.c_str());
            }
            L("  %-13s %zu Clips ueber eine Weiche, %zu passen zum Skelett (%zu ladbar, %zu Ego, %zu mit eigenem Namen), %.1f s\n    %s\n", figur,
              rw.clipsHeld.size(), passend, ladbar, einsP, mitName, std::chrono::duration<double>(std::chrono::steady_clock::now() - t2).count(),
              bsp.substr(0, 600).c_str());
        }
    }

    // ==================================================================
    // Z12 (1.07.0) - woher bekommt eine Figur ihren Waffentyp? Z11 zeigte:
    // Boba Fett schreibt WeaponType.EnumGS NIE in seinen eigenen EBX, Han und
    // Iden schon - deshalb fehlen Boba die geteilten 3p-Clips (Jetpack,
    // Blaster). Also nicht nach Namen suchen, sondern den VERWEISEN folgen:
    // von Kit/Spezialisierung/Actor der Figur ueber die Datei-GUIDs der
    // Importe zu den naechsten EBX (Waffen, Gadgets) und dort nach
    // WriteEnumerationGameState sehen.
    // ==================================================================
    // 1.12.0: Z12 hat seinen Zweck erfuellt (Bobas Tippfehler gefunden) und
    // braucht allein fuer die GUID-Tabelle 40 s, insgesamt ueber 10 Minuten.
    // Laeuft deshalb nur noch mit --alles.
    if (alles) {
    L("\n== Z12 WAFFENTYP UEBER DIE VERWEISE DER FIGUR ==\n");
    {
        // Datei-GUID -> EBX-Name (einmal aufbauen, ueber alle EBX)
        const auto tz = std::chrono::steady_clock::now();
        std::map<std::string, std::string> nachGuid;
        for (const auto& kv : idx.ebx) {
            std::vector<uint8_t> roh;
            std::string f2;
            fbebx::Datei e;
            if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) continue;
            if (!e.DateiGuid().empty()) nachGuid.emplace(Klein(e.DateiGuid()), kv.first);
        }
        L("  %zu EBX mit Datei-GUID (%.1f s)\n", nachGuid.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - tz).count());
        // Die Werte der Waffen-Aufzaehlungen mit Namen - daran laesst sich Bobas
        // Waffe zuordnen, auch wenn seine EBX den Wert nicht selbst schreiben.
        for (const char* enumName : { "WeaponType.Enum", "SpecificWeapon.Enum", "Game.Character.BodyType.Enum" }) {
            for (const fbanim::Quelle::Fund& f : q.SucheAssets("", "EnumerationAsset", enumName, 4)) {
                if (f.name != enumName) continue;
                fbgd::Datensatz d2;
                std::string kl;
                if (!q.LiesAsset(f.key, d2, kl)) continue;
                std::string z = std::string("  ") + enumName + ":";
                for (const auto& p2 : d2.felder) {
                    if (p2.first.rfind("__", 0) == 0) continue;
                    for (size_t i = 0; i < p2.second.saetze.size() && i < 90; ++i) {
                        std::string nm, wt;
                        for (const auto& p3 : p2.second.saetze[i]) {
                            if (p3.first == "Name" || p3.first == "__name") nm = p3.second.text;
                            if (p3.first == "Value") wt = std::to_string(p3.second.ganz);
                        }
                        if (!nm.empty()) z += " " + wt + "=" + nm;
                    }
                    for (size_t i = 0; i < p2.second.werte.size() && i < 90; ++i)
                        if (!p2.second.werte[i].text.empty()) z += " " + std::to_string(i) + "=" + p2.second.werte[i].text;
                }
                L("%s\n", z.substr(0, 2500).c_str());
                break;
            }
        }
        // Wer schreibt den Waffentyp ueberhaupt? Stichprobe ueber die Kits und
        // Waffen des Spiels - daraus ergibt sich das Muster (welche Datei einer
        // Figur den Wert setzt und wie sie heisst).
        {
            size_t gezeigt = 0, gelesen2 = 0;
            for (const auto& kv : idx.ebx) {
                if (gezeigt >= 14 || gelesen2 >= 1500) break;
                const std::string p2 = Klein(kv.first);
                if (p2.find("gameplay/weapons/") != 0 && p2.find("gameplay/kits/") != 0) continue;
                std::vector<uint8_t> roh;
                std::string f2;
                fbebx::Datei e;
                if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) continue;
                ++gelesen2;
                std::string treffer;
                std::function<void(const fbebx::Wert&, int)> lauf2;
                lauf2 = [&](const fbebx::Wert& o, int tiefe) {
                    if (tiefe > 25 || !treffer.empty()) return;
                    if (o.typ == "WriteEnumerationGameState") {
                        const fbebx::Wert* gs = o.Feldwert("GameState");
                        const fbebx::Wert* wert = o.Feldwert("Value");
                        if (gs != nullptr) if (const fbebx::Wert* g = gs->Feldwert("AssetGuid")) {
                            const std::string k = fbgesicht::KeyAusGuid(Klein(g->text));
                            fbgd::Datensatz d3;
                            std::string kl3;
                            if (!k.empty() && q.LiesAsset(k, d3, kl3) && d3.nameDa && d3.name.find("WeaponType") != std::string::npos)
                                treffer = d3.name + " = " + std::to_string(wert ? wert->zahl : -1);
                        }
                    }
                    for (const auto& fe : o.felder) if (fe.wert) lauf2(*fe.wert, tiefe + 1);
                    for (const auto& l : o.liste) if (l) lauf2(*l, tiefe + 1);
                };
                for (const auto& o : e.Objekte()) if (o) lauf2(*o, 0);
                if (treffer.empty()) continue;
                L("  Waffentyp gesetzt in %-62s %s\n", kv.first.c_str(), treffer.c_str());
                ++gezeigt;
            }
            L("  (%zu EBX aus gameplay/weapons und gameplay/kits geprueft)\n", gelesen2);
        }
        for (const char* figur : { "bobafett", "hansolo", "darthvader" }) {
            const std::vector<std::string> such = fbzuordnung::Suchwoerter(figur);
            std::vector<std::string> offen;
            std::set<std::string> gesehen;
            for (const auto& kv : idx.ebx) {
                const std::string p2 = Klein(kv.first);
                if (p2.find("gameplay/kits/") != 0 && p2.find("gameplay/characters/") != 0 && p2.find("characters/npc/characters/actor_") != 0) continue;
                bool passt = false;
                for (const std::string& w : such) if (w.size() >= 4 && p2.find(w) != std::string::npos) passt = true;
                if (passt && gesehen.insert(kv.first).second) offen.push_back(kv.first);
            }
            const size_t start = offen.size();
            std::map<std::string, std::vector<std::string>> schreibt;      // Zustand -> Werte mit Herkunft
            size_t gelesen = 0;
            // 1.08.0: Nur Dateien verfolgen, die ueberhaupt Zustaende setzen
            // koennen. Der erste Versuch lief in 400 Dateien aus lauter Meshes,
            // Texturen und Level-Kram leer.
            auto lohnt = [](const std::string& p3) {
                static const char* const raus[] = { "_mesh", "texture", "/t_", "sound", "/fx/", "vfx", "levels/", "ui/", "font", "decal" };
                for (const char* x : raus) if (p3.find(x) != std::string::npos) return false;
                static const char* const rein[] = { "weapon", "gadget", "abilit", "kit", "character", "hero", "specialization", "soldier", "blueprint" };
                for (const char* x : rein) if (p3.find(x) != std::string::npos) return true;
                return false;
            };
            while (!offen.empty() && gelesen < 4000) {
                const std::string name = offen.back();
                offen.pop_back();
                const auto it = idx.ebx.find(name);
                if (it == idx.ebx.end()) continue;
                std::vector<uint8_t> roh;
                std::string f2;
                fbebx::Datei e;
                if (!spiel.HoleNachSha1(it->second.sha1, roh, f2) || !e.Lies(roh, f2)) continue;
                ++gelesen;
                std::function<void(const fbebx::Wert&, int)> lauf;
                lauf = [&](const fbebx::Wert& o, int tiefe) {
                    if (tiefe > 30) return;
                    if (o.typ == "WriteEnumerationGameState") {
                        const fbebx::Wert* gs = o.Feldwert("GameState");
                        const fbebx::Wert* wert = o.Feldwert("Value");
                        if (gs != nullptr) if (const fbebx::Wert* g = gs->Feldwert("AssetGuid")) {
                            const std::string k = fbgesicht::KeyAusGuid(Klein(g->text));
                            fbgd::Datensatz d2;
                            std::string kl;
                            if (!k.empty() && q.LiesAsset(k, d2, kl) && d2.nameDa)
                                schreibt[d2.name].push_back(std::to_string(wert ? wert->zahl : -1) + " aus " + name);
                        }
                    }
                    // Verweise auf andere EBX: Import-Werte tragen die Datei-GUID
                    if (o.art == fbebx::Art::Import && !o.text.empty()) {
                        const std::string g = Klein(o.text).substr(0, 36);
                        const auto z = nachGuid.find(g);
                        if (z != nachGuid.end() && lohnt(Klein(z->second)) && gesehen.insert(z->second).second) offen.push_back(z->second);
                    }
                    for (const auto& fe : o.felder) if (fe.wert) lauf(*fe.wert, tiefe + 1);
                    for (const auto& l : o.liste) if (l) lauf(*l, tiefe + 1);
                };
                for (const auto& o : e.Objekte()) if (o) lauf(*o, 0);
            }
            L("  %-11s %zu Start-EBX, %zu EBX ueber Verweise gelesen:\n", figur, start, gelesen);
            for (const auto& kv : schreibt) {
                std::string z;
                for (size_t i = 0; i < kv.second.size() && i < 3; ++i) z += "  " + kv.second[i];
                L("    schreibt %-38s %zux%s\n", kv.first.c_str(), kv.second.size(), z.substr(0, 220).c_str());
            }
        }
    }

    }

    // ==================================================================
    // Z13 (1.10.0) - welche Clips gehoeren zu einem FAHRZEUG? Bei DH laedt das
    // Animationsfenster fuer den AT-TE nichts: es haelt die Szenenfigur fuer
    // "left" und das Rig fuer "frontend", weil Fahrzeuge keinen Vermerk tragen
    // und ihre Knochennamen nicht wie Figurennamen aussehen. Hier deshalb der
    // direkte Weg: die Knochennamen des Fahrzeugskeletts gegen die Kanalnamen
    // ALLER Clips halten. Was ueberlappt, gehoert dazu - ganz ohne Namensregel.
    // ==================================================================
    L("\n== Z13 CLIPS ZU EINEM FAHRZEUGSKELETT (Kanalnamen gegen Bones) ==\n");
    {
        std::map<std::string, size_t> keyZuClip13;
        for (size_t i = 0; i < clips.size(); ++i) keyZuClip13.emplace(clips[i].key, i);
        for (const char* name : { "at_te", "atrt", "atst", "dwarfspiderdroid" }) {
            const auto tz = std::chrono::steady_clock::now();
            const std::string skelett = fbfahrzeug::SucheSkelett(spiel, idx, name);
            if (skelett.empty()) { L("  %-16s kein Skelett gefunden\n", name); continue; }
            std::set<std::string> bones;
            for (const auto& kv : idx.ebx) {
                if (kv.first != skelett) continue;
                std::vector<uint8_t> roh;
                std::string f2;
                fbebx::Datei e;
                if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) break;
                const fbebx::Skelett sk = fbebx::LiesSkelett(e);
                for (const std::string& b : sk.namen) bones.insert(Klein(b));
                break;
            }
            if (bones.empty()) { L("  %-16s Skelett %s ohne Bones\n", name, skelett.c_str()); continue; }
            // 1.11.0: Nur die EIGENEN Bones zaehlen. Der AT-RT hat Hips,
            // LeftUpLeg usw. wie das Menschenskelett - dadurch galten 8 860
            // Figurenclips als Treffer. Bones, die auch walrus_humanmale hat,
            // fallen deshalb raus.
            const size_t bonesGesamt = bones.size();
            {
                std::set<std::string> menschlich;
                for (const auto& kv : idx.ebx) {
                    if (Klein(kv.first).find("characters/rigs/humanoids/walrus_humanmale") == std::string::npos) continue;
                    std::vector<uint8_t> roh;
                    std::string f2;
                    fbebx::Datei e;
                    if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) break;
                    const fbebx::Skelett sk = fbebx::LiesSkelett(e);
                    for (const std::string& b : sk.namen) menschlich.insert(Klein(b));
                    break;
                }
                for (const std::string& b : menschlich) bones.erase(b);
                if (bones.empty()) { L("  %-16s nur menschliche Bones - keine eigenen\n", name); continue; }
            }
            // Kanalnamen je ChannelToDofAsset einmal holen (Cache in der Quelle)
            std::map<std::string, std::pair<size_t, size_t>> jeKette;      // Kette -> (Treffer, Kanaele)
            std::vector<std::pair<double, size_t>> beste;
            std::set<std::string> gesehen;
            size_t geprueft = 0;
            for (size_t i = 0; i < clips.size(); ++i) {
                if (!gesehen.insert(clips[i].key).second) continue;
                // 1.12.0: Fortschritt zeigen und nach 240 s aufhoeren - DH musste
                // zweimal abbrechen, ohne zu sehen, ob sich ueberhaupt was tut.
                if (geprueft > 0 && geprueft % 2000 == 0)
                    L("      ... %zu Clips geprueft, %zu Treffer (%.0f s)\n", geprueft, beste.size(),
                      std::chrono::duration<double>(std::chrono::steady_clock::now() - tz).count());
                if (std::chrono::duration<double>(std::chrono::steady_clock::now() - tz).count() > 240.0) {
                    L("      ... nach 240 s abgebrochen, %zu von %zu Clips geprueft\n", geprueft, clips.size());
                    break;
                }
                std::vector<std::string> namen;
                std::string rig;
                size_t benannt = 0;
                if (!q.Kanalnamen(clips[i], namen, rig, benannt) || namen.empty()) continue;
                ++geprueft;
                std::set<std::string> getroffen;                       // je Bone nur einmal (.q und .t)
                for (const std::string& n : namen) {
                    if (n.empty()) continue;
                    std::string kurz = Klein(n);
                    const size_t p2 = kurz.find_last_of('.');
                    if (p2 != std::string::npos) kurz = kurz.substr(0, p2);
                    if (bones.count(kurz)) getroffen.insert(kurz);
                }
                const size_t treffer = getroffen.size();
                if (treffer * 2 >= bones.size()) beste.push_back({ -static_cast<double>(treffer), i });
            }
            std::sort(beste.begin(), beste.end());
            L("  %-16s Skelett %s, %zu Bones, %zu Clipketten geprueft, %zu eigene Bones -> %zu Clips mit >= 50 %% Ueberdeckung (%.1f s)\n", name,
              skelett.substr(skelett.find_last_of('/') + 1).c_str(), bonesGesamt, geprueft, bones.size(), beste.size(),
              std::chrono::duration<double>(std::chrono::steady_clock::now() - tz).count());
            for (size_t i = 0; i < beste.size() && i < 12; ++i) {
                const fbanim::ClipEintrag& c = clips[beste[i].second];
                L("      %-52s %3.0f von %zu Bones, %s, Bank %s\n", c.anzeige.empty() ? c.name.c_str() : c.anzeige.c_str(), -beste[i].first,
                  bones.size(), c.codec.c_str(), q.BankName(c.bank).substr(q.BankName(c.bank).find_last_of('/') + 1).c_str());
            }
        }
    }

    if (!alles) {
        const double sek0 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        L("\n(Codec-Abschnitte uebersprungen - mit --alles laufen sie wieder mit)\nCODECLAB fertig in %.1f s\n", sek0);
        if (L.f != nullptr) std::fclose(L.f);
        return 0;
    }

    // ------------------------------------------------------------------
    // Auswahl: je Codec die ersten `probe` Clips (eindeutige Keys), dazu
    // alle Zwillinge (gleicher Anzeigename, ein Teil RAW/FRAME).
    // ------------------------------------------------------------------
    std::map<std::string, std::vector<size_t>> jeCodec;          // Klasse -> Clipindizes
    std::map<std::string, std::vector<size_t>> jeName;           // Anzeigename -> Clipindizes
    std::set<std::string> keys;
    for (size_t i = 0; i < clips.size(); ++i) {
        const fbanim::ClipEintrag& c = clips[i];
        if (!keys.insert(c.key).second) continue;
        jeCodec[c.klasse].push_back(i);
        if (!c.anzeige.empty()) jeName[Klein(c.anzeige)].push_back(i);
    }
    L("== A1 FELDKATALOG (je Klasse bis %zu Clips) ==\n", probe);
    for (const auto& kv : jeCodec) L("  %-26s %6zu eindeutige Clips\n", kv.first.c_str(), kv.second.size());
    const char* unbekannt[] = { "VbrAnimationAsset", "DctAnimationAsset", "CurveAnimationAsset" };
    for (const char* kl : unbekannt) {
        const auto it = jeCodec.find(kl);
        if (it == jeCodec.end()) continue;
        struct Stat { std::string typ; size_t n = 0; double mn = 1e300, mx = -1e300; std::vector<double> laengen; };
        std::map<std::string, Stat> st;
        size_t gelesen = 0;
        for (size_t k = 0; k < it->second.size() && k < probe; ++k) {
            fbgd::Datensatz d;
            if (!q.Felder(clips[it->second[k]], d)) continue;
            ++gelesen;
            for (const auto* fs : { &d.basis, &d.felder }) {
                for (const auto& fp : *fs) {
                    Stat& s = st[fp.first];
                    ++s.n;
                    const fbgd::Wert& w = fp.second;
                    if (w.art == fbgd::Wert::Art::Feld) { s.typ = "Array<" + w.typ + ">"; s.laengen.push_back(w.anzahl); }
                    else {
                        const double v = w.art == fbgd::Wert::Art::Gleit ? w.gleit : static_cast<double>(w.ganz);
                        s.typ = w.art == fbgd::Wert::Art::Gleit ? "Float" : (w.art == fbgd::Wert::Art::Hex ? "Hex" : "Int");
                        if (w.art != fbgd::Wert::Art::Hex && w.art != fbgd::Wert::Art::Text) { s.mn = std::min(s.mn, v); s.mx = std::max(s.mx, v); }
                    }
                }
            }
        }
        L("\n  %s: %zu gelesen\n", kl, gelesen);
        for (auto& kv : st) {
            Stat& s = kv.second;
            if (!s.laengen.empty()) {
                std::sort(s.laengen.begin(), s.laengen.end());
                L("    %-24s %-18s in %3zu Clips  Laenge min %.0f  Median %.0f  max %.0f\n", kv.first.c_str(), s.typ.c_str(), s.n,
                  s.laengen.front(), s.laengen[s.laengen.size() / 2], s.laengen.back());
            } else if (s.mn <= s.mx) {
                L("    %-24s %-18s in %3zu Clips  Wert min %g  max %g\n", kv.first.c_str(), s.typ.c_str(), s.n, s.mn, s.mx);
            } else {
                L("    %-24s %-18s in %3zu Clips\n", kv.first.c_str(), s.typ.c_str(), s.n);
            }
        }
    }

    // ------------------------------------------------------------------
    // A2 Zwillinge
    // ------------------------------------------------------------------
    L("\n== A2 ZWILLINGE (gleicher Anzeigename: RAW/FRAME und VBR/DCT/CURV) ==\n");
    struct Paar { size_t roh, komp; };
    std::vector<Paar> paare;
    std::map<std::string, size_t> paarJeKlasse;
    for (const auto& kv : jeName) {
        size_t roh = SIZE_MAX;
        for (size_t i : kv.second) if (clips[i].klasse == "RawAnimationAsset") { roh = i; break; }
        if (roh == SIZE_MAX) continue;
        for (size_t i : kv.second) {
            const std::string& k = clips[i].klasse;
            if (k == "VbrAnimationAsset" || k == "DctAnimationAsset" || k == "CurveAnimationAsset") {
                paare.push_back({ roh, i });
                ++paarJeKlasse[k];
            }
        }
    }
    L("  %zu Paare:", paare.size());
    for (const auto& kv : paarJeKlasse) L(" %s %zu", kv.first.c_str(), kv.second);
    L("\n");
    // Paare nach Groesse der komprimierten Daten sortieren (kleine zuerst - leichter zu lesen).
    std::vector<std::pair<size_t, Paar>> sortiert;
    std::map<size_t, fbgd::Datensatz> dsCache;
    auto datensatz = [&](size_t i) -> const fbgd::Datensatz& {
        auto it = dsCache.find(i);
        if (it != dsCache.end()) return it->second;
        fbgd::Datensatz d;
        q.Felder(clips[i], d);
        return dsCache.emplace(i, std::move(d)).first->second;
    };
    for (const Paar& p : paare) {
        if (sortiert.size() > 4000) break;
        const std::vector<uint8_t> b = Bytes(FeldBeide(datensatz(p.komp), "Data"));
        sortiert.push_back({ b.size(), p });
    }
    std::sort(sortiert.begin(), sortiert.end(), [](const auto& a, const auto& b2) { return a.first < b2.first; });
    struct Zwilling { Paar p; fbanim::Clip roh; std::vector<uint8_t> daten; };
    std::vector<Zwilling> zw;
    for (const auto& sp : sortiert) {
        if (zw.size() >= 12) break;
        if (sp.first < 16) continue;
        Zwilling z;
        z.p = sp.second;
        std::string f2;
        if (!q.Entpacke(clips[z.p.roh], z.roh, f2)) continue;
        z.daten = Bytes(FeldBeide(datensatz(z.p.komp), "Data"));
        zw.push_back(std::move(z));
    }
    for (const Zwilling& z : zw) {
        const fbanim::ClipEintrag& ck = clips[z.p.komp];
        const fbgd::Datensatz& d = datensatz(z.p.komp);
        size_t qa = 0, qk = 0, ta = 0, tk = 0, fa = 0, fk = 0;
        for (const fbanim::Kanal& k : z.roh.kanaele) {
            if (k.art == 'q') (k.konstant ? qk : qa)++;
            else if (k.art == 't') (k.konstant ? tk : ta)++;
            else (k.konstant ? fk : fa)++;
        }
        const double numKeys = Zahl(d, "NumKeys");
        const double qc = Zahl(d, "QuaternionCount"), cqc = Zahl(d, "ConstQuaternionCount");
        const double vc = Zahl(d, "VectorCount"), cvc = Zahl(d, "ConstVectorCount");
        const double fc = std::max(Zahl(d, "FloatCount"), Zahl(d, "NumFloat")), cfc = Zahl(d, "ConstFloatCount");
        L("\n  PAAR %s\n    RAW  %s: %zu Keys; Drehungen %zu animiert/%zu konstant, Vektoren %zu/%zu, Floats %zu/%zu\n",
          Kurz(ck.anzeige, 70).c_str(), q.BankName(clips[z.p.roh].bank).c_str(), z.roh.zeiten.size(), qa, qk, ta, tk, fa, fk);
        L("    %s %s: Data %zu Byte; NumKeys %g, QuaternionCount %g/Const %g, VectorCount %g/Const %g, FloatCount %g/Const %g\n",
          ck.codec.c_str(), q.BankName(ck.bank).c_str(), z.daten.size(), numKeys, qc, cqc, vc, cvc, fc, cfc);
        const double spuren = (qa * 3.0 + ta * 3.0 + fa);
        if (numKeys > 0 && spuren > 0)
            L("    Bits je Key und Komponente (Data*8 / (NumKeys * (3*Drehungen + 3*Vektoren + Floats) animiert)): %.2f\n",
              z.daten.size() * 8.0 / (numKeys * spuren));
        L("    Abgleich: QuaternionCount %s animierte Drehungen (%zu), ConstQuaternionCount %s konstante (%zu), NumKeys %s RAW-Keys (%zu)\n",
          qc == static_cast<double>(qa) ? "=" : "!=", qa, cqc == static_cast<double>(qk) ? "=" : "!=", qk,
          numKeys == static_cast<double>(z.roh.zeiten.size()) ? "=" : "!=", z.roh.zeiten.size());
    }

    // ------------------------------------------------------------------
    // A3 Bytestatistik
    // ------------------------------------------------------------------
    L("\n== A3 BYTESTATISTIK der Nutzdaten (Feld Data) ==\n");
    for (const char* kl : unbekannt) {
        const auto it = jeCodec.find(kl);
        if (it == jeCodec.end()) continue;
        double eSum = 0.0, nullSum = 0.0;
        double ebene[8] = {};
        size_t n = 0, blockPasst = 0, blockGeprueft = 0;
        for (size_t k = 0; k < it->second.size() && k < probe; ++k) {
            const fbgd::Datensatz& d = datensatz(it->second[k]);
            const std::vector<uint8_t> b = Bytes(FeldBeide(d, "Data"));
            if (b.size() < 16) continue;
            ++n;
            eSum += Entropie(b);
            size_t nullen = 0;
            size_t bits[8] = {};
            for (uint8_t x : b) {
                if (x == 0) ++nullen;
                for (int i = 0; i < 8; ++i) bits[i] += (x >> i) & 1u;
            }
            nullSum += static_cast<double>(nullen) / static_cast<double>(b.size());
            for (int i = 0; i < 8; ++i) ebene[i] += static_cast<double>(bits[i]) / static_cast<double>(b.size());
            if (const fbgd::Wert* fbs = FeldBeide(d, "FrameBlockSizes")) {
                double s = 0.0;
                for (const auto& x : fbs->werte) s += static_cast<double>(x.ganz);
                ++blockGeprueft;
                if (std::fabs(s - static_cast<double>(b.size())) < 0.5 || std::fabs(s * 2.0 - b.size()) < 0.5 || std::fabs(s * 4.0 - b.size()) < 0.5) ++blockPasst;
            }
        }
        if (n == 0) continue;
        L("  %s (%zu Clips): Entropie %.2f Bit/Byte (8 = Zufall), Nullbytes %.1f %%\n", kl, n, eSum / n, 100.0 * nullSum / n);
        L("    P(Bit=1) je Bitebene 0..7:");
        for (int i = 0; i < 8; ++i) L(" %.2f", ebene[i] / n);
        L("\n");
        if (blockGeprueft) L("    FrameBlockSizes: Summe = Data-Groesse (x1, x2 oder x4) bei %zu von %zu\n", blockPasst, blockGeprueft);
    }

    // ------------------------------------------------------------------
    // A4 Bitstrom-Suche (VBR) mit RAW-Zwillingen
    // ------------------------------------------------------------------
    L("\n== A4 BITSTROM-SUCHE: quantisierte RAW-Werte im komprimierten Strom ==\n");
    L("  Je Paar: bis 4 animierte Drehspuren, Komponente x, die ersten 4 Keys; gesucht wird jede Bitposition,\n");
    L("  Bitbreite 2..16, LSB/MSB zuerst, Bereich global (QuatMin/QuatMax) oder je Spur, Abstand 1/3/4 Werte, Toleranz +-1.\n");
    size_t treffer = 0;
    for (const Zwilling& z : zw) {
        const fbgd::Datensatz& d = datensatz(z.p.komp);
        const double qmin = Zahl(d, "QuatMin", -1.0), qmax = Zahl(d, "QuatMax", 1.0);
        const std::vector<uint8_t>& b = z.daten;
        const size_t gesamtBits = b.size() * 8;
        size_t spuren = 0;
        for (const fbanim::Kanal& k : z.roh.kanaele) {
            if (k.art != 'q' || k.konstant || k.werte.size() < 16) continue;
            if (++spuren > 4) break;
            float mn = 1e9f, mx = -1e9f;
            for (size_t i = 0; i < k.werte.size(); i += 4) { mn = std::min(mn, k.werte[i]); mx = std::max(mx, k.werte[i]); }
            for (int bereich = 0; bereich < 2; ++bereich) {
                const double lo = bereich == 0 ? qmin : mn, hi = bereich == 0 ? qmax : mx;
                if (hi - lo < 1e-6) continue;
                for (int w = 2; w <= 16; ++w) {
                    const double stufen = static_cast<double>((1u << w) - 1u);
                    uint32_t soll[4];
                    for (int j = 0; j < 4; ++j) soll[j] = static_cast<uint32_t>(std::lround((k.werte[static_cast<size_t>(j) * 4] - lo) / (hi - lo) * stufen));
                    for (int abst : { 1, 3, 4 }) {
                        for (int ord = 0; ord < 2; ++ord) {
                            const bool lsb = ord == 0;
                            size_t erste = 0;
                            const size_t hier = SucheFolge(b, soll, w, abst, lsb, erste);
                            // Zufallserwartung: 4 Werte mit je 3/2^w Chance - bei kleinen w viele Zufallstreffer.
                            const double erwartet = static_cast<double>(gesamtBits) * std::pow(3.0 / std::pow(2.0, w), 4.0);
                            if (hier > 0 && hier < 6 && erwartet < 0.05) {
                                ++treffer;
                                L("  TREFFER %s: Spur %s, Bitbreite %d, %s zuerst, Bereich %s [%.4f..%.4f], Abstand %d, %zu Stelle(n), erste bei Bit %zu (Byte %zu); Zufall erwartet %.4f\n",
                                  Kurz(clips[z.p.komp].anzeige, 40).c_str(), k.name.c_str(), w, lsb ? "LSB" : "MSB", bereich == 0 ? "global" : "je Spur",
                                  lo, hi, abst, hier, erste, erste / 8, erwartet);
                            }
                        }
                    }
                }
            }
        }
    }
    L("  %zu Treffer oberhalb der Zufallsschwelle\n", treffer);

    // ------------------------------------------------------------------
    // A5 DCT-Korrelation
    // ------------------------------------------------------------------
    L("\n== A5 DCT-KORRELATION: DCT-II der RAW-Spur gegen int8/int16-Folgen im Strom ==\n");
    for (const Zwilling& z : zw) {
        if (clips[z.p.komp].klasse != "DctAnimationAsset" && clips[z.p.komp].klasse != "VbrAnimationAsset") continue;
        const std::vector<uint8_t>& b = z.daten;
        double besteR = 0.0;
        std::string beste;
        size_t spuren = 0;
        for (const fbanim::Kanal& k : z.roh.kanaele) {
            if (k.art != 'q' || k.konstant || k.werte.size() < 32) continue;
            if (++spuren > 3) break;
            const size_t n = k.werte.size() / 4;
            for (size_t block : { static_cast<size_t>(8), static_cast<size_t>(16), static_cast<size_t>(32), n }) {
                if (block > n || block < 8) continue;
                // Koeffizienten des ersten Blocks, Komponente x
                std::vector<double> c(block);
                for (size_t kk = 0; kk < block; ++kk) {
                    double s = 0.0;
                    for (size_t t = 0; t < block; ++t) s += k.werte[t * 4] * std::cos(3.14159265358979 / static_cast<double>(block) * (static_cast<double>(t) + 0.5) * static_cast<double>(kk));
                    c[kk] = s;
                }
                const size_t m = std::min<size_t>(8, block);
                for (int typ = 0; typ < 2; ++typ) {
                    const size_t breite = typ == 0 ? 1 : 2;
                    for (size_t off = 0; off + m * breite <= b.size(); ++off) {
                        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
                        for (size_t j = 0; j < m; ++j) {
                            const double y = typ == 0 ? static_cast<double>(static_cast<int8_t>(b[off + j]))
                                                      : static_cast<double>(static_cast<int16_t>(b[off + 2 * j] | (b[off + 2 * j + 1] << 8)));
                            const double x = c[j];
                            sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y;
                        }
                        const double mm = static_cast<double>(m);
                        const double nen = std::sqrt((sxx - sx * sx / mm) * (syy - sy * sy / mm));
                        if (nen < 1e-9) continue;
                        const double r = (sxy - sx * sy / mm) / nen;
                        if (r > besteR) {
                            besteR = r;
                            char t[200];
                            std::snprintf(t, sizeof t, "Spur %s, Block %zu, %s ab Byte %zu", k.name.c_str(), block, typ == 0 ? "int8" : "int16", off);
                            beste = t;
                        }
                    }
                }
            }
        }
        L("  %s %s: beste Korrelation r = %.4f (%s)%s\n", clips[z.p.komp].codec.c_str(), Kurz(clips[z.p.komp].anzeige, 40).c_str(), besteR,
          beste.empty() ? "-" : beste.c_str(), besteR > 0.995 ? "  <-- auffaellig" : "");
    }
    L("  Hinweis: bei 8 Werten liegt r schon zufaellig oft > 0,9; erst r > 0,995 an mehreren Spuren zaehlt.\n");

    // ------------------------------------------------------------------
    // A6 Differenzen
    // ------------------------------------------------------------------
    L("\n== A6 DIFFERENZEN: Paare gleicher Data-Groesse, gleiche Anfangsbytes ==\n");
    for (const char* kl : unbekannt) {
        const auto it = jeCodec.find(kl);
        if (it == jeCodec.end()) continue;
        std::map<size_t, std::vector<size_t>> jeGroesse;
        for (size_t k = 0; k < it->second.size() && k < probe; ++k) {
            const std::vector<uint8_t> b = Bytes(FeldBeide(datensatz(it->second[k]), "Data"));
            if (b.size() >= 32) jeGroesse[b.size()].push_back(it->second[k]);
        }
        size_t gezeigt = 0;
        for (const auto& kv : jeGroesse) {
            if (kv.second.size() < 2 || gezeigt >= 6) continue;
            const std::vector<uint8_t> a = Bytes(FeldBeide(datensatz(kv.second[0]), "Data"));
            const std::vector<uint8_t> c = Bytes(FeldBeide(datensatz(kv.second[1]), "Data"));
            size_t vorn = 0, gleich = 0;
            while (vorn < a.size() && a[vorn] == c[vorn]) ++vorn;
            for (size_t i = 0; i < a.size(); ++i) gleich += a[i] == c[i];
            L("  %s %zu Byte: %s / %s -> gleiche Anfangsbytes %zu, gleiche Bytes gesamt %.1f %%\n", kl, kv.first,
              Kurz(clips[kv.second[0]].anzeige, 34).c_str(), Kurz(clips[kv.second[1]].anzeige, 34).c_str(), vorn, 100.0 * gleich / a.size());
            ++gezeigt;
        }
    }

    // ==================================================================
    // Runde 2 (0.51.0): Aufteilung der Datenstroeme. Aus DHs Hexdumps von
    // 0.50.0 abgelesen und an allen drei VBR-Beispielen nachgerechnet:
    //   Data = [ConstCount Byte Palettenindizes] [ConstChanMapSize Byte
    //          Lauflaengen: abwechselnd animiert/konstant, beginnt animiert]
    //          [4 Byte je animierter Spur] [FloatOffsetSize] [Bloecke nach
    //          FrameBlockSizes, jeder beginnt mit ff XX XX]
    // Hier wird das an ALLEN Clips geprueft - und fuer Drehungen/Vektoren
    // werden die unbekannten Faktoren durchprobiert.
    // ==================================================================
    L("\n== B1 VBR-AUFTEILUNG ueber alle VBR-Clips: Data = Tabelle + Map + Deskriptoren + Offsets + Keyzeiten + Bloecke ==\n");
    struct VbrKopf { size_t i; double cq, cv, cf, aq, av, af, map, fo, vo, kt, bl, nb, d; };
    std::vector<VbrKopf> vk;
    {
        const auto it = jeCodec.find("VbrAnimationAsset");
        if (it != jeCodec.end()) {
            for (size_t k : it->second) {
                const fbgd::Datensatz& d = datensatz(k);
                VbrKopf h{};
                h.i = k;
                h.cq = Zahl(d, "ConstQuaternionCount", 0); h.cv = Zahl(d, "ConstVector3Count", 0); h.cf = Zahl(d, "ConstFloatCount", 0);
                h.aq = Zahl(d, "QuaternionCount", 0); h.av = Zahl(d, "Vector3Count", 0); h.af = Zahl(d, "FloatCount", 0);
                h.map = Zahl(d, "ConstChanMapSize", 0); h.fo = Zahl(d, "FloatOffsetSize", 0); h.vo = Zahl(d, "VectorOffsetSize", 0);
                h.kt = Zahl(d, "KeyTimeSize", 0);
                if (const fbgd::Wert* fb = FeldBeide(d, "FrameBlockSizes")) { for (const auto& x : fb->werte) h.bl += static_cast<double>(x.ganz); h.nb = static_cast<double>(fb->werte.size()); }
                const fbgd::Wert* dw = FeldBeide(d, "Data");
                h.d = dw != nullptr ? static_cast<double>(dw->werte.size()) : 0.0;
                vk.push_back(h);
            }
        }
    }
    // Kandidaten: Tabellenbytes je konstanter Drehung/Vektor, Deskriptorbytes je animierter Drehung/Vektor.
    struct Treffer { int tq, tv, dq, dv, kf; size_t n; };
    std::vector<Treffer> tr;
    for (int tq : { 1, 2, 3, 4, 6, 8 })
        for (int tv : { 1, 2, 3, 6 })
            for (int dq : { 4, 8, 12, 16 })
                for (int dv : { 4, 8, 12 })
                    for (int kf : { 1, 2 }) {                     // Keyzeiten in Byte oder als UInt16
                        size_t n = 0;
                        for (const VbrKopf& h : vk) {
                            const double soll = tq * h.cq + tv * h.cv + h.cf + h.map + dq * h.aq + dv * h.av + 4 * h.af + h.fo + h.vo + kf * h.kt + h.bl;
                            if (std::fabs(soll - h.d) < 0.5) ++n;
                        }
                        tr.push_back({ tq, tv, dq, dv, kf, n });
                    }
    std::sort(tr.begin(), tr.end(), [](const Treffer& a, const Treffer& b2) { return a.n > b2.n; });
    L("  %zu VBR-Clips. Beste Faktoren (Tabelle je konst. Drehung/Vektor, Deskriptor je anim. Drehung/Vektor, Keyzeit-Faktor):\n", vk.size());
    for (size_t t = 0; t < tr.size() && t < 8; ++t)
        L("    tq=%d tv=%d dq=%d dv=%d kt*%d: passt bei %zu von %zu (%.1f %%)\n", tr[t].tq, tr[t].tv, tr[t].dq, tr[t].dv, tr[t].kf, tr[t].n, vk.size(),
          vk.empty() ? 0.0 : 100.0 * tr[t].n / vk.size());
    // Nur-Float-Clips getrennt (dort ist die Aufteilung schon an drei Beispielen bestaetigt)
    {
        size_t nurF = 0, passtF = 0;
        for (const VbrKopf& h : vk) {
            if (h.aq + h.av + h.cq + h.cv > 0) continue;
            ++nurF;
            if (std::fabs(h.cf + h.map + 4 * h.af + h.fo + h.vo + h.kt + h.bl - h.d) < 0.5) ++passtF;
        }
        L("  Nur-Float-Clips (Gesichts-/VO-Kurven): %zu, Gleichung stimmt bei %zu\n", nurF, passtF);
    }

    // B2 Lauflaengen-Karte
    L("\n== B2 LAUFLAENGEN-KARTE (ConstChanMap): Summen gegen die Zaehler ==\n");
    if (!tr.empty() && tr[0].n > 0) {
        const Treffer& b0 = tr[0];
        size_t geprueft = 0, gleich = 0, nurKonstGleich = 0;
        std::string beispiel;
        for (const VbrKopf& h : vk) {
            const double soll = b0.tq * h.cq + b0.tv * h.cv + h.cf + h.map + b0.dq * h.aq + b0.dv * h.av + 4 * h.af + h.fo + h.vo + b0.kf * h.kt + h.bl;
            if (std::fabs(soll - h.d) >= 0.5) continue;
            const std::vector<uint8_t> b = Bytes(FeldBeide(datensatz(h.i), "Data"));
            // 0.53.0: an drei Drehungs-Clips abgelesen - die Keyzeiten stehen VOR der Karte.
            const size_t ab = static_cast<size_t>(b0.tq * h.cq + b0.tv * h.cv + h.cf + b0.kf * h.kt);
            if (ab + static_cast<size_t>(h.map) > b.size()) continue;
            ++geprueft;
            double anim = 0, konst = 0;
            for (size_t j = 0; j < static_cast<size_t>(h.map); ++j) ((j % 2 == 0) ? anim : konst) += b[ab + j];
            const bool ok1 = anim == h.aq + h.av + h.af && konst == h.cq + h.cv + h.cf;
            if (ok1) ++gleich;
            if (konst == h.cq + h.cv + h.cf) ++nurKonstGleich;
            if (!ok1 && beispiel.size() < 400) {
                char t[300];
                std::snprintf(t, sizeof t, "    abweichend %s: Laeufe anim %.0f/konst %.0f, Zaehler anim %.0f (q %.0f v %.0f f %.0f) konst %.0f (q %.0f v %.0f f %.0f)\n",
                              Kurz(clips[h.i].anzeige, 40).c_str(), anim, konst, h.aq + h.av + h.af, h.aq, h.av, h.af, h.cq + h.cv + h.cf, h.cq, h.cv, h.cf);
                beispiel += t;
            }
        }
        L("  mit den besten Faktoren geprueft: %zu Clips; Summen stimmen (anim UND konst): %zu; nur konst: %zu\n%s", geprueft, gleich, nurKonstGleich, beispiel.c_str());
    }

    // B3 Blockkoepfe
    L("\n== B3 BLOCKKOEPFE: jeder Block beginnt mit ff XX XX? ==\n");
    if (!tr.empty() && tr[0].n > 0) {
        const Treffer& b0 = tr[0];
        size_t bloecke = 0, ff = 0, paar = 0;
        std::map<int, size_t> erstes;
        for (const VbrKopf& h : vk) {
            const double soll = b0.tq * h.cq + b0.tv * h.cv + h.cf + h.map + b0.dq * h.aq + b0.dv * h.av + 4 * h.af + h.fo + h.vo + b0.kf * h.kt + h.bl;
            if (std::fabs(soll - h.d) >= 0.5) continue;
            const fbgd::Datensatz& d = datensatz(h.i);
            const std::vector<uint8_t> b = Bytes(FeldBeide(d, "Data"));
            size_t pos = static_cast<size_t>(soll - h.bl);
            const fbgd::Wert* fb = FeldBeide(d, "FrameBlockSizes");
            if (fb == nullptr) continue;
            for (const auto& x : fb->werte) {
                if (pos + 3 > b.size()) break;
                ++bloecke;
                if (b[pos] == 0xFF) ++ff;
                if (b[pos + 1] == b[pos + 2]) ++paar;
                ++erstes[b[pos]];
                pos += static_cast<size_t>(x.ganz);
            }
        }
        L("  %zu Bloecke: erstes Byte ff bei %zu (%.1f %%), Byte 2 = Byte 3 bei %zu (%.1f %%)\n", bloecke, ff, bloecke ? 100.0 * ff / bloecke : 0.0,
          paar, bloecke ? 100.0 * paar / bloecke : 0.0);
        std::vector<std::pair<size_t, int>> haeufig;
        for (const auto& kv : erstes) haeufig.push_back({ kv.second, kv.first });
        std::sort(haeufig.rbegin(), haeufig.rend());
        L("  haeufigste erste Bytes:");
        for (size_t i = 0; i < haeufig.size() && i < 8; ++i) L(" %02x (%zu)", haeufig[i].second, haeufig[i].first);
        L("\n");
    }

    // B4 Deskriptoren der Nur-Float-Clips: Halbbytes
    L("\n== B4 DESKRIPTOREN (Nur-Float-Clips): Verteilung der Halbbytes je Position ==\n");
    {
        size_t hist[8][16] = {};
        size_t spuren = 0;
        for (const VbrKopf& h : vk) {
            if (h.aq + h.av + h.cq + h.cv > 0 || h.af <= 0) continue;
            if (std::fabs(h.cf + h.map + 4 * h.af + h.fo + h.vo + h.kt + h.bl - h.d) >= 0.5) continue;
            const std::vector<uint8_t> b = Bytes(FeldBeide(datensatz(h.i), "Data"));
            const size_t ab = static_cast<size_t>(h.cf + h.map);
            for (size_t c = 0; c < static_cast<size_t>(h.af); ++c) {
                if (ab + 4 * c + 4 > b.size()) break;
                ++spuren;
                for (int j = 0; j < 4; ++j) {
                    const uint8_t x = b[ab + 4 * c + static_cast<size_t>(j)];
                    ++hist[2 * j][x >> 4];
                    ++hist[2 * j + 1][x & 15];
                }
            }
        }
        L("  %zu Spuren. Zeile = Halbbyte (0 = Byte 0 oben, 1 = Byte 0 unten, ...), Spalten = Wert 0..15:\n", spuren);
        for (int j = 0; j < 8; ++j) {
            L("    H%d:", j);
            for (int v = 0; v < 16; ++v) L(" %5zu", hist[j][v]);
            L("\n");
        }
    }

    // B5 DCT: DeltaBase als Festkomma 1/16384?
    L("\n== B5 DCT DeltaBase: Drehungen als Festkomma (1,0 = 16384)? ==\n");
    {
        const auto it = jeCodec.find("DctAnimationAsset");
        if (it != jeCodec.end()) {
            std::vector<double> normen, vek;
            size_t wNull = 0, vekN = 0;
            for (size_t k = 0; k < it->second.size() && k < probe * 5; ++k) {
                const fbgd::Datensatz& d = datensatz(it->second[k]);
                const fbgd::Wert* x = FeldBeide(d, "DeltaBaseX");
                const fbgd::Wert* y = FeldBeide(d, "DeltaBaseY");
                const fbgd::Wert* z = FeldBeide(d, "DeltaBaseZ");
                const fbgd::Wert* w = FeldBeide(d, "DeltaBaseW");
                if (!x || !y || !z || !w) continue;
                const size_t nq = static_cast<size_t>(Zahl(d, "NumQuats", 0));
                const double mult = Zahl(d, "QuantizeMultBlock", 4096);
                const size_t n = std::min({ x->werte.size(), y->werte.size(), z->werte.size(), w->werte.size() });
                for (size_t i = 0; i < n; ++i) {
                    const double a = static_cast<double>(x->werte[i].ganz), bb = static_cast<double>(y->werte[i].ganz);
                    const double c = static_cast<double>(z->werte[i].ganz), e = static_cast<double>(w->werte[i].ganz);
                    if (i < nq) {
                        const double nn = std::sqrt(a * a + bb * bb + c * c + e * e) / 16384.0;
                        if (nn > 0.0) normen.push_back(nn);
                    } else {
                        ++vekN;
                        if (e == 0.0) ++wNull;
                        if (mult > 0) vek.push_back(std::fabs(c) / mult);
                    }
                }
            }
            std::sort(normen.begin(), normen.end());
            size_t nahe = 0;
            for (double v : normen) if (std::fabs(v - 1.0) < 0.01) ++nahe;
            if (!normen.empty())
                L("  %zu Drehungen: Betrag/16384 Median %.4f, 10 %% %.4f, 90 %% %.4f; innerhalb +-1 %%: %.1f %%\n", normen.size(), normen[normen.size() / 2],
                  normen[normen.size() / 10], normen[normen.size() * 9 / 10], 100.0 * nahe / normen.size());
            std::sort(vek.begin(), vek.end());
            if (!vek.empty())
                L("  %zu Vektor-Eintraege: W = 0 bei %zu; |Z|/QuantizeMultBlock Median %.4f, 90 %% %.4f\n", vekN, wNull, vek[vek.size() / 2], vek[vek.size() * 9 / 10]);
        }
    }

    // B6 DCT: Groessenbeziehungen
    L("\n== B6 DCT-GROESSEN: BitsPerSubblock, Data, DataSize gegen Keys und Spuren ==\n");
    {
        const auto it = jeCodec.find("DctAnimationAsset");
        if (it != jeCodec.end()) {
            size_t gezeigt = 0, gleich8 = 0, geprueft = 0;
            for (size_t k = 0; k < it->second.size() && k < probe; ++k) {
                const fbgd::Datensatz& d = datensatz(it->second[k]);
                const double nKeys = Zahl(d, "NumKeys", 0), nq = Zahl(d, "NumQuats", 0), nv = Zahl(d, "NumVec3", 0), nf = Zahl(d, "NumFloat", 0);
                const double nfv = Zahl(d, "NumFloatVec", 0);
                const fbgd::Wert* bps = FeldBeide(d, "BitsPerSubblock");
                const size_t lb = bps ? bps->werte.size() : 0;
                const size_t ld = Bytes(FeldBeide(d, "Data")).size();
                ++geprueft;
                // Vermutung: 8 Eintraege je Unterblock von 8 Keys?
                if (lb % 8 == 0) ++gleich8;
                if (gezeigt < 12 && lb > 0) {
                    std::string erste;
                    for (size_t i = 0; i < lb && i < 16; ++i) { char t[16]; std::snprintf(t, sizeof t, " %lld", static_cast<long long>(bps->werte[i].ganz)); erste += t; }
                    L("  %-34s Keys %4.0f Quats %4.0f Vec3 %4.0f Float %3.0f FloatVec %3.0f | BitsPerSubblock n=%zu:%s | Data %zu, DataSize %.0f, CatchAll %.0f\n",
                      Kurz(clips[it->second[k]].anzeige, 34).c_str(), nKeys, nq, nv, nf, nfv, lb, erste.c_str(), ld, Zahl(d, "DataSize", 0),
                      Zahl(d, "CatchAllBitCount", 0));
                    ++gezeigt;
                }
            }
            L("  BitsPerSubblock-Laenge durch 8 teilbar: %zu von %zu\n", gleich8, geprueft);
        }
    }

    // ==================================================================
    // Runde 3 (0.52.0). Befund Runde 2: die VBR-Aufteilung stimmt bei
    // 11 637 von 11 720 Clips mit 4 Byte je konstanter Drehung, 3 je
    // konstantem Vektor, 1 je konstantem Float und 4 Byte Deskriptor je
    // animierter KOMPONENTE (16 je Drehung, 12 je Vektor, 4 je Float). Die
    // Halbbytes der Deskriptoren fallen in der Reihenfolge "unteres Halbbyte
    // zuerst" streng ab (Mittel ~9, 6, 4, 2,7, 2, 1,4, 1,1, 0,8) - wie
    // Bitbreiten von 8 DCT-Koeffizienten je Block (Energie in den niedrigen
    // Frequenzen). Jetzt: Werte pruefen, die eine Selbstpruefung haben.
    // ==================================================================
    // B7c (0.56.0) Erst-Verwendungs-Probe: Paletten entstehen in der Reihenfolge,
    // in der der Kodierer die Werte trifft - neue Indizes kommen dann lueckenlos
    // 0, 1, 2, ... An vier Clips (Dio, drei Sentry) war das NUR so, wenn die
    // Keyzeiten ganz vorn stehen und die Tabelle dahinter. Hier an allen Clips.
    L("\n== B7c ERST-VERWENDUNGS-PROBE: Tabelle ab 0 oder hinter den Keyzeiten? ==\n");
    {
        size_t geprueft = 0, abNull = 0, abKeyzeiten = 0, beide = 0;
        for (const VbrKopf& h : vk) {
            if (h.kt < 1 || h.cq + h.cv + h.cf < 3) continue;
            if (std::fabs(4 * h.cq + 3 * h.cv + h.cf + h.map + 16 * h.aq + 12 * h.av + 4 * h.af + h.fo + h.vo + h.kt + h.bl - h.d) >= 0.5) continue;
            const std::vector<uint8_t> b = Bytes(FeldBeide(datensatz(h.i), "Data"));
            const size_t tab = static_cast<size_t>(4 * h.cq + 3 * h.cv + h.cf), kt = static_cast<size_t>(h.kt);
            if (kt + tab > b.size()) continue;
            auto lueckenlos = [&](size_t von) {
                int naechster = 0;
                for (size_t i = von; i < von + tab; ++i) {
                    if (b[i] > naechster) return false;
                    if (b[i] == naechster) ++naechster;
                }
                return true;
            };
            ++geprueft;
            const bool a0 = lueckenlos(0), ak = lueckenlos(kt);
            if (a0) ++abNull;
            if (ak) ++abKeyzeiten;
            if (a0 && ak) ++beide;
        }
        L("  %zu Clips mit Keyzeiten: lueckenlos ab Byte 0 bei %zu, ab den Keyzeiten bei %zu (beides: %zu)\n", geprueft, abNull, abKeyzeiten, beide);
    }

    // B7 Konstante Drehungen: Einheitslaenge als Pruefstein fuer Anordnung und Abbildung
    L("\n== B7 KONSTANTE DREHUNGEN aus Palette: welche Anordnung ergibt Einheitsquaternionen? ==\n");
    {
        // Anordnungen: 0 = je Drehung xyzw hintereinander, 1 = planar (alle x, alle y, ...)
        // Abbildungen: 0 = QuatMin + p*(QuatMax-QuatMin), 1 = 2p-1, 2 = p
        size_t gesamt[2][3] = {}, einheit[2][3] = {};
        size_t clipsGeprueft = 0;
        for (const VbrKopf& h : vk) {
            if (h.cq < 1) continue;
            if (std::fabs(4 * h.cq + 3 * h.cv + h.cf + h.map + 16 * h.aq + 12 * h.av + 4 * h.af + h.fo + h.vo + h.kt + h.bl - h.d) >= 0.5) continue;
            if (++clipsGeprueft > 3000) break;
            const fbgd::Datensatz& d = datensatz(h.i);
            const std::vector<uint8_t> b = Bytes(FeldBeide(d, "Data"));
            const fbgd::Wert* pal = FeldBeide(d, "ConstantPalette");
            if (pal == nullptr) continue;
            const double qmin = Zahl(d, "QuatMin", -1), qmax = Zahl(d, "QuatMax", 1);
            const size_t cq = static_cast<size_t>(h.cq);
            for (int anord = 0; anord < 2; ++anord) {
                for (int abb = 0; abb < 3; ++abb) {
                    for (size_t j = 0; j < cq; ++j) {
                        double qv[4];
                        bool gut = true;
                        for (int c = 0; c < 4 && gut; ++c) {
                            // 0.56.0: die Keyzeiten stehen GANZ VORN, die Tabelle dahinter (Erst-Verwendungs-Probe).
                            const size_t pos = static_cast<size_t>(h.kt) + (anord == 0 ? j * 4 + static_cast<size_t>(c) : static_cast<size_t>(c) * cq + j);
                            if (pos >= b.size() || b[pos] >= pal->werte.size()) { gut = false; break; }
                            const double pv = pal->werte[b[pos]].gleit;
                            qv[c] = abb == 0 ? qmin + pv * (qmax - qmin) : (abb == 1 ? 2.0 * pv - 1.0 : pv);
                        }
                        if (!gut) continue;
                        ++gesamt[anord][abb];
                        const double n = std::sqrt(qv[0] * qv[0] + qv[1] * qv[1] + qv[2] * qv[2] + qv[3] * qv[3]);
                        if (std::fabs(n - 1.0) < 0.01) ++einheit[anord][abb];
                    }
                }
            }
        }
        const char* an[2] = { "je Drehung xyzw", "planar (alle x, alle y, ...)" };
        const char* ab[3] = { "QuatMin + p*(Max-Min)", "2p - 1", "p" };
        L("  %zu Clips mit konstanten Drehungen geprueft\n", clipsGeprueft);
        for (int a2 = 0; a2 < 2; ++a2)
            for (int b2 = 0; b2 < 3; ++b2)
                L("    %-30s %-22s Einheitslaenge (+-1 %%) bei %zu von %zu (%.1f %%)\n", an[a2], ab[b2], einheit[a2][b2], gesamt[a2][b2],
                  gesamt[a2][b2] ? 100.0 * einheit[a2][b2] / gesamt[a2][b2] : 0.0);
    }

    // B7b "Kleinste drei" (Gaffer on Games, Unity Netcode): die groesste Komponente
    // wird weggelassen und aus |q| = 1 zurueckgerechnet, ihr Index (0..3) steht
    // dabei. Bei 4 Byte je konstanter Drehung: 1 Byte Index + 3 Palettenindizes?
    L("\n== B7b KONSTANTE DREHUNGEN als 'kleinste drei' (Index + 3 Komponenten)? ==\n");
    {
        // Varianten: Indexbyte vorn/hinten; Bereich der drei Komponenten +-1 oder +-0,7071
        size_t gesamt2[2][2] = {}, gueltig[2][2] = {}, indexOk[2] = {};
        size_t clipsGeprueft2 = 0;
        for (const VbrKopf& h : vk) {
            if (h.cq < 1) continue;
            if (std::fabs(4 * h.cq + 3 * h.cv + h.cf + h.map + 16 * h.aq + 12 * h.av + 4 * h.af + h.fo + h.vo + h.kt + h.bl - h.d) >= 0.5) continue;
            if (++clipsGeprueft2 > 3000) break;
            const fbgd::Datensatz& d = datensatz(h.i);
            const std::vector<uint8_t> b = Bytes(FeldBeide(d, "Data"));
            const fbgd::Wert* pal = FeldBeide(d, "ConstantPalette");
            if (pal == nullptr) continue;
            const size_t tab0 = static_cast<size_t>(h.kt);                        // 0.56.0: Keyzeiten vorn
            for (size_t j = 0; j < static_cast<size_t>(h.cq) && tab0 + j * 4 + 4 <= b.size(); ++j) {
                for (int vorn = 0; vorn < 2; ++vorn) {
                    const uint8_t sel = vorn == 0 ? b[tab0 + j * 4] : b[tab0 + j * 4 + 3];
                    if (sel <= 3) ++indexOk[vorn];
                    for (int ber = 0; ber < 2; ++ber) {
                        ++gesamt2[vorn][ber];
                        if (sel > 3) continue;
                        double sq = 0.0;
                        bool gut = true;
                        for (int c = 0; c < 3; ++c) {
                            const uint8_t ix = vorn == 0 ? b[tab0 + j * 4 + 1 + static_cast<size_t>(c)] : b[tab0 + j * 4 + static_cast<size_t>(c)];
                            if (ix >= pal->werte.size()) { gut = false; break; }
                            const double v = (2.0 * pal->werte[ix].gleit - 1.0) * (ber == 0 ? 1.0 : 0.70710678);
                            sq += v * v;
                        }
                        if (gut && sq <= 1.0001) ++gueltig[vorn][ber];
                    }
                }
            }
        }
        L("  %zu Clips geprueft\n", clipsGeprueft2);
        for (int vorn = 0; vorn < 2; ++vorn) {
            L("    Indexbyte %s: Wert 0..3 bei %zu von %zu\n", vorn == 0 ? "vorn " : "hinten", indexOk[vorn], gesamt2[vorn][0]);
            for (int ber = 0; ber < 2; ++ber)
                L("      Komponenten %s: gueltig (Index 0..3 und Summe der Quadrate <= 1) bei %zu (%.1f %%)\n", ber == 0 ? "+-1     " : "+-0,7071",
                  gueltig[vorn][ber], gesamt2[vorn][ber] ? 100.0 * gueltig[vorn][ber] / gesamt2[vorn][ber] : 0.0);
        }
    }

    // Namens-Cache (0.55.0): viele Clips teilen dasselbe ChannelToDofAsset; die
    // Rig-Suche je Clip kostete in Runde 5 den Grossteil der 151 s.
    std::map<std::string, std::vector<std::string>> namensCache;
    auto namenVon = [&](size_t clipIdx, std::vector<std::string>& namen) -> bool {
        const fbgd::Datensatz& d0 = datensatz(clipIdx);
        const fbgd::Wert* ctd = FeldBeide(d0, "ChannelToDofAsset");
        const std::string schl = ctd != nullptr ? ctd->text : std::string();
        const auto it = namensCache.find(schl);
        if (it != namensCache.end()) { namen = it->second; return !namen.empty(); }
        std::string rigName;
        size_t benannt = 0;
        if (!q.Kanalnamen(clips[clipIdx], namen, rigName, benannt) || benannt < namen.size() * 9 / 10) namen.clear();
        namensCache[schl] = namen;
        return !namen.empty();
    };

    // B9 (0.54.0) - Vergleichswerte OHNE fremdes Programm: die Ruhelage des
    // Skeletts. Viele Knochen bewegen sich in einem Clip nicht (Finger, Gesicht,
    // Waffenknochen); ihre konstanten Drehungen sollten der Ruhelage gleichen.
    // Dazu die Kanalnamen in DOF-Reihenfolge (dieselbe Namenskette wie bei RAW)
    // und zwei Anordnungen der Konstantentabelle:
    //   A: nach Art gruppiert (alle Drehungen, dann Vektoren, dann Floats)
    //   B: in DOF-Reihenfolge verschraenkt (4/3/1 Byte je konstantem Kanal)
    // Gemessen: Einheitslaenge und Winkel zur Ruhelage (Walrus, gemessene
    // Lesart: Zeilen der Lage = Spalten von R(q)).
    L("\n== B9 KONSTANTE DREHUNGEN gegen die RUHELAGE des Skeletts (Kanalnamen in DOF-Reihenfolge) ==\n");
    {
        std::map<std::string, fbebx::Lage> ruhe;
        for (const auto& kv : idx.ebx) {
            if (Klein(kv.first).find("characters/rigs/humanoids/walrus_humanmale") == std::string::npos) continue;
            std::vector<uint8_t> roh;
            fbebx::Datei e;
            std::string f2;
            if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) continue;
            const fbebx::Skelett sk = fbebx::LiesSkelett(e);
            if (!sk.gefunden) continue;
            for (size_t i = 0; i < sk.namen.size() && i < sk.lokal.size(); ++i) ruhe[sk.namen[i]] = sk.lokal[i];
            break;
        }
        L("  Ruhelage: %zu Knochen (walrus_humanmale)\n", ruhe.size());
        struct Erg { size_t n = 0, einheit = 0, mitRuhe = 0, nahe = 0; std::vector<double> winkel; };
        Erg erg[2][2];
        size_t geprueft = 0, anzahlPasst = 0, ohneNamen = 0;
        for (const VbrKopf& h : vk) {
            if (h.cq < 1) continue;
            if (std::fabs(4 * h.cq + 3 * h.cv + h.cf + h.map + 16 * h.aq + 12 * h.av + 4 * h.af + h.fo + h.vo + h.kt + h.bl - h.d) >= 0.5) continue;
            if (geprueft >= 1500) break;
            std::vector<std::string> namen;
            if (!namenVon(h.i, namen)) { ++ohneNamen; continue; }
            ++geprueft;
            const size_t kanaele = static_cast<size_t>(h.aq + h.av + h.af + h.cq + h.cv + h.cf);
            if (namen.size() != kanaele) continue;
            ++anzahlPasst;
            const fbgd::Datensatz& d = datensatz(h.i);
            const std::vector<uint8_t> b = Bytes(FeldBeide(d, "Data"));
            const fbgd::Wert* pal = FeldBeide(d, "ConstantPalette");
            if (pal == nullptr) continue;
            const double qmin = Zahl(d, "QuatMin", -1), qmax = Zahl(d, "QuatMax", 1);
            // Art je DOF aus dem Namen
            std::vector<char> art(kanaele);
            for (size_t k = 0; k < kanaele; ++k) {
                const std::string& n = namen[k];
                art[k] = (n.size() > 2 && n.compare(n.size() - 2, 2, ".q") == 0) ? 'q' : (n.size() > 2 && n.compare(n.size() - 2, 2, ".t") == 0) ? 't' : 'f';
            }
            // Karte: animiert/konstant je DOF
            const size_t mapAb = static_cast<size_t>(4 * h.cq + 3 * h.cv + h.cf + h.kt);
            std::vector<char> konst(kanaele, 0);
            size_t k = 0;
            for (size_t j = 0; j < static_cast<size_t>(h.map) && mapAb + j < b.size(); ++j) {
                const size_t lauf = b[mapAb + j];
                for (size_t r = 0; r < lauf && k < kanaele; ++r, ++k) konst[k] = (j % 2 == 1) ? 1 : 0;
            }
            size_t cqGezaehlt = 0;
            for (size_t i = 0; i < kanaele; ++i) if (konst[i] && art[i] == 'q') ++cqGezaehlt;
            // Anordnungen: Position jedes konstanten Drehkanals in der Tabelle
            for (int anord = 0; anord < 2; ++anord) {
                size_t posQ = 0, posVerschr = 0;
                for (size_t i = 0; i < kanaele; ++i) {
                    if (!konst[i]) continue;
                    const size_t breite = art[i] == 'q' ? 4 : (art[i] == 't' ? 3 : 1);
                    size_t pos;
                    if (anord == 0) { if (art[i] != 'q') continue; pos = posQ; posQ += 4; }
                    else { pos = posVerschr; posVerschr += breite; if (art[i] != 'q') continue; }
                    pos += static_cast<size_t>(h.kt);                                 // 0.56.0: Keyzeiten vorn
                    if (pos + 4 > b.size()) continue;
                    for (int abb = 0; abb < 2; ++abb) {
                        double qv[4];
                        bool gut = true;
                        for (int c = 0; c < 4; ++c) {
                            const uint8_t ix = b[pos + static_cast<size_t>(c)];
                            if (ix >= pal->werte.size()) { gut = false; break; }
                            const double pv = pal->werte[ix].gleit;
                            qv[c] = abb == 0 ? qmin + pv * (qmax - qmin) : 2.0 * pv - 1.0;
                        }
                        if (!gut) continue;
                        Erg& e2 = erg[anord][abb];
                        ++e2.n;
                        const double nn = std::sqrt(qv[0] * qv[0] + qv[1] * qv[1] + qv[2] * qv[2] + qv[3] * qv[3]);
                        if (std::fabs(nn - 1.0) < 0.01) ++e2.einheit;
                        const std::string knochen = namen[i].substr(0, namen[i].size() - 2);
                        const auto rt = ruhe.find(knochen);
                        if (rt == ruhe.end() || nn < 1e-6) continue;
                        const double x = qv[0] / nn, y = qv[1] / nn, z = qv[2] / nn, w = qv[3] / nn;
                        const double R[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                                                 { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                                                 { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
                        const double* Lz[3] = { rt->second.right, rt->second.up, rt->second.forward };
                        double spur = 0.0;
                        for (int r2 = 0; r2 < 3; ++r2) for (int c2 = 0; c2 < 3; ++c2) spur += Lz[r2][c2] * R[c2][r2];
                        const double wk = std::acos(std::max(-1.0, std::min(1.0, (spur - 1.0) / 2.0))) * 180.0 / 3.14159265358979;
                        ++e2.mitRuhe;
                        if (wk < 2.0) ++e2.nahe;
                        e2.winkel.push_back(wk);
                    }
                }
            }
            (void)cqGezaehlt;
        }
        L("  %zu Clips mit Namen geprueft (%zu ohne ausreichende Namen), Kanalzahl = Namenszahl bei %zu\n", geprueft, ohneNamen, anzahlPasst);
        const char* an[2] = { "A nach Art gruppiert  ", "B DOF-Reihenfolge     " };
        const char* ab[2] = { "Min+p*(Max-Min)", "2p-1          " };
        for (int a2 = 0; a2 < 2; ++a2)
            for (int b2 = 0; b2 < 2; ++b2) {
                Erg& e2 = erg[a2][b2];
                std::sort(e2.winkel.begin(), e2.winkel.end());
                L("    %s %s: Einheitslaenge %5.1f %% (%zu); mit Ruhelage %zu: Winkel Median %6.2f Grad, unter 2 Grad %5.1f %%\n", an[a2], ab[b2],
                  e2.n ? 100.0 * e2.einheit / e2.n : 0.0, e2.n, e2.mitRuhe, e2.winkel.empty() ? -1.0 : e2.winkel[e2.winkel.size() / 2],
                  e2.mitRuhe ? 100.0 * e2.nahe / e2.mitRuhe : 0.0);
            }
    }

    // B10 (0.55.0) - die Umrechnung AUS DEN DATEN lernen. B9 zeigte: direkt
    // gelesen liegen die konstanten Drehungen im Median 150 Grad neben der
    // Ruhelage - die Bedeutung der vier Tabellenbytes ist falsch angenommen.
    // Jetzt umgekehrt: fuer konstante Drehkanaele mit bekannter Ruhelage die
    // Paare (Palettenwert von Byte k, Ruhelage-Komponente c) sammeln und je
    // Paar die Korrelation messen. Die richtige Zuordnung Byte -> Komponente
    // und die Umrechnung (a + b*p) fallen dabei heraus, wenn die unbewegten
    // Knochen ihre Ruhelage behalten.
    L("\n== B10 UMRECHNUNG LERNEN: Palettenwert je Tabellenbyte gegen Ruhelage-Komponente (Korrelation) ==\n");
    {
        std::map<std::string, std::array<double, 4>> ruheQ;         // Knochen -> Quaternion (x, y, z, w), w >= 0
        for (const auto& kv : idx.ebx) {
            if (Klein(kv.first).find("characters/rigs/humanoids/walrus_humanmale") == std::string::npos) continue;
            std::vector<uint8_t> roh;
            fbebx::Datei e;
            std::string f2;
            if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) continue;
            const fbebx::Skelett sk = fbebx::LiesSkelett(e);
            if (!sk.gefunden) continue;
            for (size_t i = 0; i < sk.namen.size() && i < sk.lokal.size(); ++i) {
                // Gemessene Lesart: Zeilen der Lage = Spalten von R(q), also R = Lage^T.
                const fbebx::Lage& l = sk.lokal[i];
                const double R[3][3] = { { l.right[0], l.up[0], l.forward[0] }, { l.right[1], l.up[1], l.forward[1] }, { l.right[2], l.up[2], l.forward[2] } };
                double x, y, z, w;
                const double spurR = R[0][0] + R[1][1] + R[2][2];
                if (spurR > 0) { const double s2 = std::sqrt(spurR + 1.0) * 2; w = 0.25 * s2; x = (R[2][1] - R[1][2]) / s2; y = (R[0][2] - R[2][0]) / s2; z = (R[1][0] - R[0][1]) / s2; }
                else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) { const double s2 = std::sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2; w = (R[2][1] - R[1][2]) / s2; x = 0.25 * s2; y = (R[0][1] + R[1][0]) / s2; z = (R[0][2] + R[2][0]) / s2; }
                else if (R[1][1] > R[2][2]) { const double s2 = std::sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2; w = (R[0][2] - R[2][0]) / s2; x = (R[0][1] + R[1][0]) / s2; y = 0.25 * s2; z = (R[1][2] + R[2][1]) / s2; }
                else { const double s2 = std::sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2; w = (R[1][0] - R[0][1]) / s2; x = (R[0][2] + R[2][0]) / s2; y = (R[1][2] + R[2][1]) / s2; z = 0.25 * s2; }
                if (w < 0) { x = -x; y = -y; z = -z; w = -w; }
                ruheQ[sk.namen[i]] = { x, y, z, w };
            }
            break;
        }
        // Summen fuer Pearson-Korrelation je (Byte k, Komponente c), auch gegen |Komponente|
        double sx[4] = {}, sxx[4] = {}, sy[4] = {}, syy[4] = {}, sxy[4][4] = {}, sya[4] = {}, syya[4] = {}, sxya[4][4] = {};
        size_t n = 0, clipsB10 = 0;
        for (const VbrKopf& h : vk) {
            if (h.cq < 1) continue;
            if (std::fabs(4 * h.cq + 3 * h.cv + h.cf + h.map + 16 * h.aq + 12 * h.av + 4 * h.af + h.fo + h.vo + h.kt + h.bl - h.d) >= 0.5) continue;
            if (clipsB10 >= 1500) break;
            std::vector<std::string> namen;
            const size_t kanaele = static_cast<size_t>(h.aq + h.av + h.af + h.cq + h.cv + h.cf);
            if (!namenVon(h.i, namen) || namen.size() != kanaele) continue;
            ++clipsB10;
            const fbgd::Datensatz& d = datensatz(h.i);
            const std::vector<uint8_t> b = Bytes(FeldBeide(d, "Data"));
            const fbgd::Wert* pal = FeldBeide(d, "ConstantPalette");
            if (pal == nullptr) continue;
            const size_t mapAb = static_cast<size_t>(4 * h.cq + 3 * h.cv + h.cf + h.kt);
            std::vector<char> konst(kanaele, 0);
            {
                size_t k2 = 0;
                for (size_t j2 = 0; j2 < static_cast<size_t>(h.map) && mapAb + j2 < b.size(); ++j2)
                    for (size_t r = 0; r < b[mapAb + j2] && k2 < kanaele; ++r, ++k2) konst[k2] = (j2 % 2 == 1) ? 1 : 0;
            }
            size_t pos = 0;                                  // Anordnung A (nach Art gruppiert): Drehungen zuerst
            for (size_t i = 0; i < kanaele; ++i) {
                const std::string& nm = namen[i];
                if (!konst[i] || nm.size() < 3 || nm.compare(nm.size() - 2, 2, ".q") != 0) continue;
                const size_t hier = pos + static_cast<size_t>(h.kt);                 // 0.56.0: Keyzeiten vorn
                pos += 4;
                const auto rt = ruheQ.find(nm.substr(0, nm.size() - 2));
                if (rt == ruheQ.end() || hier + 4 > b.size()) continue;
                double p[4];
                bool gut = true;
                for (int k2 = 0; k2 < 4; ++k2) { const uint8_t ix = b[hier + static_cast<size_t>(k2)]; if (ix >= pal->werte.size()) { gut = false; break; } p[k2] = pal->werte[ix].gleit; }
                if (!gut) continue;
                ++n;
                for (int k2 = 0; k2 < 4; ++k2) {
                    sx[k2] += p[k2]; sxx[k2] += p[k2] * p[k2];
                    for (int c = 0; c < 4; ++c) { const double yv = rt->second[static_cast<size_t>(c)]; sxy[k2][c] += p[k2] * yv; sxya[k2][c] += p[k2] * std::fabs(yv); }
                }
                for (int c = 0; c < 4; ++c) { const double yv = rt->second[static_cast<size_t>(c)]; sy[c] += yv; syy[c] += yv * yv; sya[c] += std::fabs(yv); syya[c] += yv * yv; }
            }
        }
        L("  %zu konstante Drehkanaele mit Ruhelage aus %zu Clips. Korrelation r (Zeile = Tabellenbyte 0..3, Spalte = Ruhelage x y z w):\n", n, clipsB10);
        const double dn = static_cast<double>(n);
        for (int k2 = 0; k2 < 4 && n > 10; ++k2) {
            L("    Byte %d:", k2);
            for (int c = 0; c < 4; ++c) {
                const double vx = sxx[k2] - sx[k2] * sx[k2] / dn, vy = syy[c] - sy[c] * sy[c] / dn;
                const double r = (vx > 0 && vy > 0) ? (sxy[k2][c] - sx[k2] * sy[c] / dn) / std::sqrt(vx * vy) : 0.0;
                const double b1 = vx > 0 ? (sxy[k2][c] - sx[k2] * sy[c] / dn) / vx : 0.0;
                const double a1 = (sy[c] - b1 * sx[k2]) / dn;
                L("  %c: r=%+.3f (q=%+.3f%+.3f*p)", "xyzw"[c], r, a1, b1);
            }
            L("\n");
        }
        L("  gegen |Komponente| (falls das Vorzeichen woanders steckt):\n");
        for (int k2 = 0; k2 < 4 && n > 10; ++k2) {
            L("    Byte %d:", k2);
            for (int c = 0; c < 4; ++c) {
                const double vx = sxx[k2] - sx[k2] * sx[k2] / dn, vy = syya[c] - sya[c] * sya[c] / dn;
                const double r = (vx > 0 && vy > 0) ? (sxya[k2][c] - sx[k2] * sya[c] / dn) / std::sqrt(vx * vy) : 0.0;
                L("  |%c|: r=%+.3f", "xyzw"[c], r);
            }
            L("\n");
        }
    }

    // B8 (0.54.0 neu gefasst) Animierte Drehungen, Block 0. Unbekannt: Bitreihenfolge,
    // Vorzeichenform, Leserichtung (je Komponente / je Koeffizient), Reihenfolge der
    // Komponenten (nach Art gruppiert / DOF-Reihenfolge) und Abbildung. Bewertung
    // (neu): Schwankung von |q| im Verhaeltnis zur Bewegung der Komponenten -
    // richtig gelesen bewegen sich die Komponenten, |q| bleibt trotzdem stehen.
    // Fast unbewegte Drehungen zaehlen nicht (die sind bei jeder Variante ruhig).
    L("\n== B8 ANIMIERTE DREHUNGEN, Block 0: 96 Lesevarianten, bewertet an |q|-Schwankung / Bewegung ==\n");
    {
        struct Var { int lsb, vorz, folge, abb, ord; };
        std::vector<Var> var;
        for (int ord = 0; ord < 2; ++ord)
            for (int lsb = 0; lsb < 2; ++lsb)
                for (int vorz = 0; vorz < 3; ++vorz)
                    for (int folge = 0; folge < 2; ++folge)
                        for (int abb = 0; abb < 2; ++abb)
                            var.push_back({ lsb, vorz, folge, abb, ord });
        std::vector<std::vector<double>> guete(var.size());
        size_t clipsB8 = 0, kanaeleBewegt = 0;
        std::vector<std::pair<double, size_t>> kandidaten;
        for (const VbrKopf& h : vk) {
            if (h.aq < 1 || h.aq > 24) continue;
            if (std::fabs(4 * h.cq + 3 * h.cv + h.cf + h.map + 16 * h.aq + 12 * h.av + 4 * h.af + h.fo + h.vo + h.kt + h.bl - h.d) >= 0.5) continue;
            kandidaten.push_back({ h.d, static_cast<size_t>(&h - &vk[0]) });
        }
        std::sort(kandidaten.begin(), kandidaten.end());
        for (size_t kk = 0; kk < kandidaten.size() && clipsB8 < 80; ++kk) {
            const VbrKopf& h = vk[kandidaten[kk].second];
            const fbgd::Datensatz& d = datensatz(h.i);
            const std::vector<uint8_t> b = Bytes(FeldBeide(d, "Data"));
            std::vector<std::string> namen;
            const size_t kanaele = static_cast<size_t>(h.aq + h.av + h.af + h.cq + h.cv + h.cf);
            if (!namenVon(h.i, namen) || namen.size() != kanaele) continue;
            const size_t mapAb = static_cast<size_t>(4 * h.cq + 3 * h.cv + h.cf + h.kt);
            const size_t desc = mapAb + static_cast<size_t>(h.map);
            const size_t blockStart = desc + static_cast<size_t>(16 * h.aq + 12 * h.av + 4 * h.af + h.fo + h.vo);
            if (blockStart + 3 >= b.size()) continue;
            const int nKeys = static_cast<int>(std::min<double>(8.0, Zahl(d, "NumKeys", 8)));
            if (nKeys < 4) continue;
            std::vector<char> art(kanaele), konst(kanaele, 0);
            for (size_t k2 = 0; k2 < kanaele; ++k2) {
                const std::string& n = namen[k2];
                art[k2] = (n.size() > 2 && n.compare(n.size() - 2, 2, ".q") == 0) ? 'q' : (n.size() > 2 && n.compare(n.size() - 2, 2, ".t") == 0) ? 't' : 'f';
            }
            {
                size_t k2 = 0;
                for (size_t j2 = 0; j2 < static_cast<size_t>(h.map) && mapAb + j2 < b.size(); ++j2)
                    for (size_t r = 0; r < b[mapAb + j2] && k2 < kanaele; ++r, ++k2) konst[k2] = (j2 % 2 == 1) ? 1 : 0;
            }
            // Komponentenlisten in beiden Reihenfolgen: (Art, Kanal, Komponente)
            struct Komp { char a; size_t kanal; int c; };
            std::vector<Komp> listen[2];
            for (char typ : { 'q', 't', 'f' })
                for (size_t k2 = 0; k2 < kanaele; ++k2)
                    if (!konst[k2] && art[k2] == typ) for (int c = 0; c < (typ == 'q' ? 4 : typ == 't' ? 3 : 1); ++c) listen[0].push_back({ typ, k2, c });
            for (size_t k2 = 0; k2 < kanaele; ++k2)
                if (!konst[k2]) for (int c = 0; c < (art[k2] == 'q' ? 4 : art[k2] == 't' ? 3 : 1); ++c) listen[1].push_back({ art[k2], k2, c });
            const size_t nk = listen[0].size();
            if (nk != static_cast<size_t>(4 * h.aq + 3 * h.av + h.af)) continue;
            ++clipsB8;
            for (size_t vi = 0; vi < var.size(); ++vi) {
                const Var& v = var[vi];
                const std::vector<Komp>& li = listen[v.ord];
                std::vector<std::array<int, 8>> breite(nk);
                for (size_t c = 0; c < nk; ++c)
                    for (int k2 = 0; k2 < 8; ++k2) {
                        const uint8_t x = b[desc + c * 4 + static_cast<size_t>(k2 / 2)];
                        breite[c][static_cast<size_t>(k2)] = (k2 % 2 == 0) ? (x & 15) : (x >> 4);
                    }
                size_t bit = (blockStart + 3) * 8;
                bool ueberlauf = false;
                auto lies = [&](int w) -> double {
                    if (w <= 0) return 0.0;
                    const uint32_t r = LiesBits(b, bit, w, v.lsb == 1);
                    bit += static_cast<size_t>(w);
                    if (r == 0xFFFFFFFFu) { ueberlauf = true; return 0.0; }
                    if (v.vorz == 0) { const int64_t m = int64_t(1) << w; return static_cast<double>((r & (1u << (w - 1))) ? static_cast<int64_t>(r) - m : static_cast<int64_t>(r)); }
                    if (v.vorz == 1) { const uint32_t betrag = r & ((1u << (w - 1)) - 1u); return (r >> (w - 1)) ? -static_cast<double>(betrag) : static_cast<double>(betrag); }
                    return static_cast<double>(static_cast<int64_t>(r) - (int64_t(1) << (w - 1)));
                };
                std::vector<std::array<double, 8>> koef(nk);
                if (v.folge == 0) { for (size_t c = 0; c < nk; ++c) for (int k2 = 0; k2 < 8; ++k2) koef[c][static_cast<size_t>(k2)] = lies(breite[c][static_cast<size_t>(k2)]); }
                else { for (int k2 = 0; k2 < 8; ++k2) for (size_t c = 0; c < nk; ++c) koef[c][static_cast<size_t>(k2)] = lies(breite[c][static_cast<size_t>(k2)]); }
                if (ueberlauf) continue;
                for (size_t c0 = 0; c0 + 3 < nk; ++c0) {
                    if (li[c0].a != 'q' || li[c0].c != 0) continue;
                    std::array<std::vector<double>, 4> werte;
                    for (int c = 0; c < 4; ++c) {
                        const auto& kf = koef[c0 + static_cast<size_t>(c)];
                        for (int t = 0; t < nKeys; ++t) {
                            double sw = kf[0] / std::sqrt(8.0);
                            for (int k2 = 1; k2 < 8; ++k2) sw += std::sqrt(2.0 / 8.0) * kf[static_cast<size_t>(k2)] * std::cos(3.14159265358979 * (2 * t + 1) * k2 / 16.0);
                            if (v.abb == 1) {
                                const int w0 = breite[c0 + static_cast<size_t>(c)][0];
                                const double voll = w0 > 0 ? static_cast<double>((1u << w0) - 1u) : 1.0;
                                sw = 2.0 * (sw * std::sqrt(8.0) / 8.0) / voll - 1.0;
                            }
                            werte[static_cast<size_t>(c)].push_back(sw);
                        }
                    }
                    double bewegung = 0.0;
                    for (int c = 0; c < 4; ++c) {
                        double m = 0, qq = 0;
                        for (double x : werte[static_cast<size_t>(c)]) m += x;
                        m /= nKeys;
                        for (double x : werte[static_cast<size_t>(c)]) qq += (x - m) * (x - m);
                        bewegung += std::sqrt(qq / nKeys) / 4.0;
                    }
                    std::vector<double> normen;
                    for (int t = 0; t < nKeys; ++t) {
                        double sq = 0;
                        for (int c = 0; c < 4; ++c) sq += werte[static_cast<size_t>(c)][static_cast<size_t>(t)] * werte[static_cast<size_t>(c)][static_cast<size_t>(t)];
                        normen.push_back(std::sqrt(sq));
                    }
                    double m = 0, qq = 0;
                    for (double x : normen) m += x;
                    m /= nKeys;
                    for (double x : normen) qq += (x - m) * (x - m);
                    if (m < 1e-9) continue;
                    const double relBewegung = bewegung / m;
                    if (relBewegung < 0.01) continue;                 // fast unbewegt: zaehlt nicht
                    if (vi == 0) ++kanaeleBewegt;
                    guete[vi].push_back(std::sqrt(qq / nKeys) / bewegung);
                }
            }
        }
        std::vector<std::pair<double, size_t>> rang;
        for (size_t vi = 0; vi < var.size(); ++vi) {
            if (guete[vi].size() < 5) continue;
            std::sort(guete[vi].begin(), guete[vi].end());
            rang.push_back({ guete[vi][guete[vi].size() / 2], vi });
        }
        std::sort(rang.begin(), rang.end());
        const char* vz[3] = { "Zweierkompl.", "Vorz.+Betrag", "versetzt" };
        L("  %zu Clips (mit Kanalnamen), bewegte Drehkanaele je Variante bis %zu. Median |q|-Schwankung / Bewegung (kleiner = besser):\n", clipsB8, kanaeleBewegt);
        for (size_t r = 0; r < rang.size() && r < 12; ++r) {
            const Var& v = var[rang[r].second];
            L("    %.4f  %s, %s, %s, %s, %s  (%zu Kanaele)\n", rang[r].first, v.ord == 0 ? "gruppiert" : "DOF-Reihenfolge", v.lsb ? "LSB" : "MSB",
              vz[v.vorz], v.folge == 0 ? "je Komponente" : "je Koeffizient", v.abb == 0 ? "roh" : "DC->[0,1]->2p-1", guete[rang[r].second].size());
        }
        if (!rang.empty()) L("    ... schlechteste Variante: %.4f\n", rang.back().first);
    }

    // ==================================================================
    // Runde 8 (0.57.0): die Bloecke. Konstante Kanaele sind geloest (B7: 100 %
    // Einheitslaenge, B9: Median 5,8 Grad zur Ruhelage, 42,7 % unter 2 Grad).
    // Jetzt die Nur-Float-Clips (Sprach-/Gesichtskurven, gleichmaessige Keys):
    // einfachste Bloecke, bekannte Bitbreiten je Kanal.
    // C1: Tabelle je Block - Kopfbytes, Blockgroesse, Summe der Bitbreiten mit
    //     und ohne Vorzeichenbits - zum Ablesen der Blockgleichung.
    // C2: Lesevarianten, bewertet an der STETIGKEIT ueber die Blockgrenzen
    //     (letzter Wert von Block b gegen ersten von Block b+1, im Verhaeltnis
    //     zur Aenderung innerhalb der Bloecke). Varianten: Bitreihenfolge,
    //     Vorzeichen, Leserichtung, Kopfgroesse, DC absolut oder als Differenz
    //     zum vorigen Block (so kodiert z. B. JPEG den DC-Koeffizienten).
    // ==================================================================
    L("\n== C1 BLOCKTABELLE (Nur-Float-Clips): Kopf, Groesse, Bitbreiten-Summen ==\n");
    struct FloatClip { size_t i; std::vector<std::array<int, 8>> breite; std::vector<std::vector<uint8_t>> bloecke; int keys; };
    std::vector<FloatClip> fclips;
    for (const VbrKopf& h : vk) {
        if (h.aq + h.av + h.cq + h.cv > 0 || h.af < 1 || h.kt > 0) continue;
        if (std::fabs(h.cf + h.map + 4 * h.af + h.fo + h.vo + h.kt + h.bl - h.d) >= 0.5) continue;
        const fbgd::Datensatz& d = datensatz(h.i);
        const std::vector<uint8_t> b = Bytes(FeldBeide(d, "Data"));
        const fbgd::Wert* fb = FeldBeide(d, "FrameBlockSizes");
        if (fb == nullptr || fb->werte.size() < 2) continue;
        FloatClip fc;
        fc.i = h.i;
        fc.keys = static_cast<int>(Zahl(d, "NumKeys", 0));
        const size_t desc = static_cast<size_t>(h.cf + h.map);
        for (size_t c = 0; c < static_cast<size_t>(h.af); ++c) {
            std::array<int, 8> w{};
            for (int k2 = 0; k2 < 8; ++k2) { const uint8_t x = b[desc + c * 4 + static_cast<size_t>(k2 / 2)]; w[static_cast<size_t>(k2)] = (k2 % 2 == 0) ? (x & 15) : (x >> 4); }
            fc.breite.push_back(w);
        }
        size_t pos = static_cast<size_t>(h.cf + h.map + 4 * h.af + h.fo + h.vo);
        for (const auto& x : fb->werte) {
            const size_t g = static_cast<size_t>(x.ganz);
            if (pos + g > b.size()) break;
            fc.bloecke.emplace_back(b.begin() + static_cast<long>(pos), b.begin() + static_cast<long>(pos + g));
            pos += g;
        }
        fclips.push_back(std::move(fc));
        if (fclips.size() >= 400) break;
    }
    {
        size_t zeilen = 0;
        for (const FloatClip& fc : fclips) {
            if (zeilen >= 24) break;
            int summe = 0, nichtNull = 0;
            for (const auto& w : fc.breite) for (int x : w) { summe += x; if (x > 0) ++nichtNull; }
            for (size_t bi = 0; bi < fc.bloecke.size() && zeilen < 24; ++bi, ++zeilen) {
                const auto& bl = fc.bloecke[bi];
                L("  %-28s Block %zu: Kopf %02x %02x %02x  Groesse %3zu Byte = %4zu Bit nach Kopf | Summe Breiten %3d, + Vorzeichen %3d | Kanaele %zu, Keys %d\n",
                  Kurz(clips[fc.i].anzeige, 28).c_str(), bi, bl.size() > 0 ? bl[0] : 0, bl.size() > 1 ? bl[1] : 0, bl.size() > 2 ? bl[2] : 0, bl.size(),
                  bl.size() >= 3 ? (bl.size() - 3) * 8 : 0, summe, summe + nichtNull, fc.breite.size(), fc.keys);
            }
        }
    }
    L("\n== C2 BLOECKE LESEN (Nur-Float-Clips): Varianten, bewertet an der Stetigkeit ueber die Blockgrenzen ==\n");
    {
        struct Var { int lsb, vorz, folge, kopf, dc; };
        std::vector<Var> var;
        for (int lsb = 0; lsb < 2; ++lsb) for (int vorz = 0; vorz < 3; ++vorz) for (int folge = 0; folge < 2; ++folge)
            for (int kopf : { 3, 1, 0 }) for (int dc = 0; dc < 2; ++dc) var.push_back({ lsb, vorz, folge, kopf, dc });
        std::vector<std::vector<double>> guete(var.size());
        std::vector<size_t> passt(var.size(), 0), gezaehlt(var.size(), 0);
        for (size_t vi = 0; vi < var.size(); ++vi) {
            const Var& v = var[vi];
            for (const FloatClip& fc : fclips) {
                const size_t nk = fc.breite.size();
                std::vector<std::vector<double>> letzter(nk), werteJeBlock;
                std::vector<double> dcVorher(nk, 0.0);
                double sprungSumme = 0, innenSumme = 0;
                size_t sprungN = 0, innenN = 0;
                std::vector<std::vector<double>> vorige(nk);
                for (size_t bi = 0; bi < fc.bloecke.size(); ++bi) {
                    const auto& bl = fc.bloecke[bi];
                    if (bl.size() <= static_cast<size_t>(v.kopf)) break;
                    const std::vector<uint8_t> nutz(bl.begin() + v.kopf, bl.end());
                    size_t bit = 0;
                    auto lies = [&](int w) -> double {
                        if (w <= 0) return 0.0;
                        const uint32_t r = LiesBits(nutz, bit, w, v.lsb == 1);
                        bit += static_cast<size_t>(w);
                        if (r == 0xFFFFFFFFu) return 0.0;
                        if (v.vorz == 0) { const int64_t m = int64_t(1) << w; return static_cast<double>((r & (1u << (w - 1))) ? static_cast<int64_t>(r) - m : static_cast<int64_t>(r)); }
                        if (v.vorz == 1) { const uint32_t betrag = r & ((1u << (w - 1)) - 1u); return (r >> (w - 1)) ? -static_cast<double>(betrag) : static_cast<double>(betrag); }
                        return static_cast<double>(static_cast<int64_t>(r) - (int64_t(1) << (w - 1)));
                    };
                    std::vector<std::array<double, 8>> kf(nk);
                    if (v.folge == 0) { for (size_t c = 0; c < nk; ++c) for (int k2 = 0; k2 < 8; ++k2) kf[c][static_cast<size_t>(k2)] = lies(fc.breite[c][static_cast<size_t>(k2)]); }
                    else { for (int k2 = 0; k2 < 8; ++k2) for (size_t c = 0; c < nk; ++c) kf[c][static_cast<size_t>(k2)] = lies(fc.breite[c][static_cast<size_t>(k2)]); }
                    ++gezaehlt[vi];
                    if (bit <= nutz.size() * 8 && nutz.size() * 8 - bit < 8) ++passt[vi];
                    const int keysHier = std::max(1, std::min(8, fc.keys - static_cast<int>(bi) * 8));
                    for (size_t c = 0; c < nk; ++c) {
                        if (v.dc == 1) { kf[c][0] += dcVorher[c]; dcVorher[c] = kf[c][0]; }
                        std::vector<double> w2;
                        for (int t = 0; t < keysHier; ++t) {
                            double sw = kf[c][0] / std::sqrt(8.0);
                            for (int k2 = 1; k2 < 8; ++k2) sw += std::sqrt(2.0 / 8.0) * kf[c][static_cast<size_t>(k2)] * std::cos(3.14159265358979 * (2 * t + 1) * k2 / 16.0);
                            w2.push_back(sw);
                        }
                        for (size_t t = 1; t < w2.size(); ++t) { innenSumme += std::fabs(w2[t] - w2[t - 1]); ++innenN; }
                        if (!vorige[c].empty() && !w2.empty()) { sprungSumme += std::fabs(w2.front() - vorige[c].back()); ++sprungN; }
                        vorige[c] = w2;
                    }
                }
                if (sprungN > 0 && innenN > 0 && innenSumme > 1e-9) guete[vi].push_back((sprungSumme / sprungN) / (innenSumme / innenN));
            }
        }
        std::vector<std::pair<double, size_t>> rang;
        for (size_t vi = 0; vi < var.size(); ++vi) {
            if (guete[vi].size() < 5) continue;
            std::sort(guete[vi].begin(), guete[vi].end());
            rang.push_back({ guete[vi][guete[vi].size() / 2], vi });
        }
        std::sort(rang.begin(), rang.end());
        const char* vz[3] = { "Zweierkompl.", "Vorz.+Betrag", "versetzt" };
        L("  %zu Nur-Float-Clips. Median Sprung an der Blockgrenze / mittlere Aenderung im Block (richtig: um 1; Zufall: deutlich mehr):\n", fclips.size());
        for (size_t r = 0; r < rang.size() && r < 14; ++r) {
            const Var& v = var[rang[r].second];
            L("    %.3f  %s, %s, %s, Kopf %d Byte, DC %s  | Bloecke mit < 8 Restbits: %zu von %zu\n", rang[r].first, v.lsb ? "LSB" : "MSB", vz[v.vorz],
              v.folge == 0 ? "je Kanal" : "je Koeffizient", v.kopf, v.dc ? "als Differenz" : "absolut", passt[rang[r].second], gezaehlt[rang[r].second]);
        }
        if (!rang.empty()) L("    ... schlechteste Variante: %.3f\n", rang.back().first);
    }

    // ==================================================================
    // Runde 9 (0.58.0). Befund C1: bei gleichen Deskriptoren schwankt die
    // Blockgroesse stark (z. B. 1296/1120/1000/768 Bit) und liegt oft UNTER
    // der Summe der Bitbreiten - die Werte sind also nicht mit fester Breite
    // gespeichert, sondern mit variabler Laenge. Und: je groesser das Kopfbyte
    // XX (ff XX XX), desto kleiner der Block (5d->1296, 70->1120, 81->1000,
    // 8c->768 Bit).
    // C3: Zusammenhang Kopfbyte XX <-> Blockgroesse, je Clip gemessen.
    // C5: Codes variabler Laenge (Rice, Exp-Golomb, Kennbit+Wert, feste Breite)
    //     mit dem Halbbyte als Parameter - bewertet an der AUFGEHENDEN
    //     Blocklaenge: der richtige Code endet in jedem Block im letzten Byte.
    // ==================================================================
    L("\n== C3 KOPFBYTE XX gegen BLOCKGROESSE (je Clip, Nur-Float) ==\n");
    {
        std::vector<double> rJeClip;
        size_t negativ = 0;
        for (const FloatClip& fc : fclips) {
            if (fc.bloecke.size() < 4) continue;
            double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
            double n = 0;
            for (size_t bi = 0; bi + 1 < fc.bloecke.size(); ++bi) {           // letzten (kuerzeren) Block auslassen
                const auto& bl = fc.bloecke[bi];
                if (bl.size() < 3) continue;
                const double x = bl[1], y = static_cast<double>(bl.size());
                sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y; n += 1;
            }
            if (n < 3) continue;
            const double vx = sxx - sx * sx / n, vy = syy - sy * sy / n;
            if (vx <= 0 || vy <= 0) continue;
            const double r = (sxy - sx * sy / n) / std::sqrt(vx * vy);
            rJeClip.push_back(r);
            if (r < 0) ++negativ;
        }
        std::sort(rJeClip.begin(), rJeClip.end());
        if (!rJeClip.empty())
            L("  %zu Clips: Korrelation XX<->Groesse Median %+.3f (10 %% %+.3f, 90 %% %+.3f); negativ bei %zu\n", rJeClip.size(),
              rJeClip[rJeClip.size() / 2], rJeClip[rJeClip.size() / 10], rJeClip[rJeClip.size() * 9 / 10], negativ);
    }
    L("\n== C5 CODES VARIABLER LAENGE: geht die Blocklaenge auf? (Nur-Float, alle Bloecke ausser dem letzten) ==\n");
    {
        // code: 0 feste Breite w, 1 Kennbit (0 = Null) + w Bit, 2 Rice(k=w) Einsen-dann-Null,
        //       3 Rice(k=w) Nullen-dann-Eins, 4 Exp-Golomb(k=w) Nullen-Praefix, 5 Exp-Golomb(k=w) Einsen-Praefix
        struct Var { int lsb, code, nullAuslassen, folge, kopf; };
        std::vector<Var> var;
        for (int lsb = 0; lsb < 2; ++lsb) for (int code = 0; code < 6; ++code) for (int na = 0; na < 2; ++na)
            for (int folge = 0; folge < 2; ++folge) for (int kopf : { 1, 2, 3 }) var.push_back({ lsb, code, na, folge, kopf });
        std::vector<size_t> passt(var.size(), 0), geprueft(var.size(), 0);
        for (size_t vi = 0; vi < var.size(); ++vi) {
            const Var& v = var[vi];
            for (const FloatClip& fc : fclips) {
                for (size_t bi = 0; bi + 1 < fc.bloecke.size(); ++bi) {
                    const auto& bl = fc.bloecke[bi];
                    if (bl.size() <= static_cast<size_t>(v.kopf)) continue;
                    const std::vector<uint8_t> nutz(bl.begin() + v.kopf, bl.end());
                    const size_t gesamt = nutz.size() * 8;
                    size_t bit = 0;
                    bool kaputt = false;
                    auto einBit = [&]() -> int {
                        if (bit >= gesamt) { kaputt = true; return 0; }
                        const size_t byte = bit >> 3;
                        const int pos = v.lsb ? static_cast<int>(bit & 7) : 7 - static_cast<int>(bit & 7);
                        ++bit;
                        return (nutz[byte] >> pos) & 1;
                    };
                    auto bits = [&](int w) { for (int i = 0; i < w && !kaputt; ++i) einBit(); };
                    auto wert = [&](int w) {
                        if (w == 0 && v.nullAuslassen) return;
                        switch (v.code) {
                            case 0: bits(w); break;
                            case 1: if (einBit()) bits(w); break;
                            case 2: { int un = 0; while (!kaputt && einBit() == 1 && un < 64) ++un; bits(w); break; }
                            case 3: { int un = 0; while (!kaputt && einBit() == 0 && un < 64) ++un; bits(w); break; }
                            case 4: { int z = 0; while (!kaputt && einBit() == 0 && z < 32) ++z; bits(z + w); break; }
                            default: { int z = 0; while (!kaputt && einBit() == 1 && z < 32) ++z; bits(z + w); break; }
                        }
                    };
                    const size_t nk = fc.breite.size();
                    if (v.folge == 0) { for (size_t c = 0; c < nk && !kaputt; ++c) for (int k2 = 0; k2 < 8 && !kaputt; ++k2) wert(fc.breite[c][static_cast<size_t>(k2)]); }
                    else { for (int k2 = 0; k2 < 8 && !kaputt; ++k2) for (size_t c = 0; c < nk && !kaputt; ++c) wert(fc.breite[c][static_cast<size_t>(k2)]); }
                    ++geprueft[vi];
                    if (!kaputt && gesamt - bit < 8) ++passt[vi];
                }
            }
        }
        std::vector<std::pair<double, size_t>> rang;
        for (size_t vi = 0; vi < var.size(); ++vi) if (geprueft[vi]) rang.push_back({ -static_cast<double>(passt[vi]) / geprueft[vi], vi });
        std::sort(rang.begin(), rang.end());
        const char* cn[6] = { "feste Breite", "Kennbit+Wert", "Rice 1..10", "Rice 0..01", "ExpGolomb 0-Praefix", "ExpGolomb 1-Praefix" };
        L("  Anteil der Bloecke, deren Lesung im letzten Byte endet (richtig: nahe 100 %%):\n");
        for (size_t r = 0; r < rang.size() && r < 14; ++r) {
            const Var& v = var[rang[r].second];
            L("    %5.1f %%  %s, %-20s Breite 0 %s, %s, Kopf %d Byte  (%zu von %zu)\n", -100.0 * rang[r].first, v.lsb ? "LSB" : "MSB", cn[v.code],
              v.nullAuslassen ? "ausgelassen" : "mitgelesen ", v.folge == 0 ? "je Kanal" : "je Koeffizient", v.kopf, passt[rang[r].second], geprueft[rang[r].second]);
        }
    }

    // ==================================================================
    // Runde 10 (0.59.0) - DCT. An drei Hexdumps (WalkFwdTwistEnd,
    // T_1p_Pistol_CrouchToStand, A_1p_ST_Rifle_AimDwn) abgelesen: das obere
    // Halbbyte von DofTableDescBytes je Kanal (0x30, 0x60, 0x70, 0x80 ...) ist
    // die ANZAHL der UInt16-Eintraege, die dieser Kanal in BitsPerSubblock
    // belegt (3, 6, 7, 8) - bei beiden Clips lueckenlos der Reihe nach. Jeder
    // Eintrag hat 4 Halbbytes, die wie Bitbreiten je Komponente aussehen
    // (x y z w; bei Drehungen w meist 0 - w wird vermutlich aus |q| = 1
    // zurueckgerechnet) und von Eintrag zu Eintrag fallen.
    // D1: Summe der oberen Halbbytes = Laenge von BitsPerSubblock?
    // D2: Bitbudget - Summe aller Halbbytes (mit/ohne Vorzeichenbit) mal
    //     Faktor (1, Bloecke zu 8/16 Keys, Keys) gegen Data*8.
    // D3: Halbbytes je Position fuer Drehungen und Vektoren getrennt.
    // C6: VBR - je Block die Verschiebung s, mit der Summe max(0, w - s)
    //     (+ Vorzeichenbits) am besten auf die Blockgroesse passt; haengt s
    //     vom Kopfbyte XX ab?
    // ==================================================================
    L("\n== D1 DCT: oberes Halbbyte von DofTableDescBytes = Eintraege in BitsPerSubblock? ==\n");
    {
        const auto it = jeCodec.find("DctAnimationAsset");
        size_t geprueft = 0, gleich = 0, gleichUnten = 0;
        std::string beispiel;
        std::map<int, size_t> hoch, tief;
        // D2-Sammlung
        struct D2 { double s, sv, keys, bits, quats, vec3; };
        std::vector<D2> d2;
        double pos[2][4] = {}, posN[2] = {};
        if (it != jeCodec.end()) {
            for (size_t k = 0; k < it->second.size(); ++k) {
                const fbgd::Datensatz& d = datensatz(it->second[k]);
                const fbgd::Wert* dt = FeldBeide(d, "DofTableDescBytes");
                const fbgd::Wert* bps = FeldBeide(d, "BitsPerSubblock");
                if (dt == nullptr || bps == nullptr) continue;
                ++geprueft;
                size_t summe = 0, summeU = 0;
                for (const auto& x : dt->werte) { summe += static_cast<size_t>((x.ganz >> 4) & 15); summeU += static_cast<size_t>(x.ganz & 15); ++hoch[static_cast<int>((x.ganz >> 4) & 15)]; ++tief[static_cast<int>(x.ganz & 15)]; }
                if (summe == bps->werte.size()) ++gleich;
                if (summeU == bps->werte.size()) ++gleichUnten;
                if (summe != bps->werte.size() && beispiel.size() < 600) {
                    char t[200];
                    std::snprintf(t, sizeof t, "    abweichend %s: Summe oben %zu, BitsPerSubblock %zu, Kanaele %zu\n", Kurz(clips[it->second[k]].anzeige, 40).c_str(),
                                  summe, bps->werte.size(), dt->werte.size());
                    beispiel += t;
                }
                // D2/D3: Halbbytes je Eintrag (oberstes zuerst), Kanal fuer Kanal
                const size_t nq = static_cast<size_t>(Zahl(d, "NumQuats", 0));
                double sw = 0, sv = 0;
                size_t e = 0;
                for (size_t c = 0; c < dt->werte.size(); ++c) {
                    const size_t n = static_cast<size_t>((dt->werte[c].ganz >> 4) & 15);
                    for (size_t j = 0; j < n && e < bps->werte.size(); ++j, ++e) {
                        const uint32_t w = static_cast<uint32_t>(bps->werte[e].ganz) & 0xFFFFu;
                        for (int nb = 0; nb < 4; ++nb) {
                            const int bitsHier = static_cast<int>((w >> (12 - 4 * nb)) & 15u);
                            sw += bitsHier;
                            if (bitsHier > 0) sv += bitsHier + 1;
                            pos[c < nq ? 0 : 1][nb] += bitsHier;
                        }
                        posN[c < nq ? 0 : 1] += 1;
                    }
                }
                const double bitsDaten = static_cast<double>(Bytes(FeldBeide(d, "Data")).size()) * 8.0;
                d2.push_back({ sw, sv, Zahl(d, "NumKeys", 0), bitsDaten, static_cast<double>(nq), Zahl(d, "NumVec3", 0) });
            }
        }
        L("  %zu DCT-Clips: Summe oberer Halbbytes = Laenge BitsPerSubblock bei %zu, Summe unterer bei %zu\n%s", geprueft, gleich, gleichUnten, beispiel.c_str());
        L("  obere Halbbytes:");
        for (const auto& kv : hoch) L(" %d:%zu", kv.first, kv.second);
        L("\n  untere Halbbytes:");
        for (const auto& kv : tief) L(" %d:%zu", kv.first, kv.second);
        L("\n");
        L("\n== D2 DCT-BITBUDGET: Summe der Halbbytes (S, mit Vorzeichenbit SV) mal Faktor gegen Data*8 ==\n");
        const char* fn[6] = { "x1", "x Bloecke(8 Keys)", "x Bloecke(16 Keys)", "x Keys", "x (Keys-1)", "x Bloecke(4 Keys)" };
        for (int mitV = 0; mitV < 2; ++mitV) {
            for (int f = 0; f < 6; ++f) {
                size_t nah = 0, n = 0;
                std::vector<double> quot;
                for (const D2& x : d2) {
                    if (x.bits < 64 || x.s <= 0) continue;
                    const double fk = f == 0 ? 1 : f == 1 ? std::ceil(x.keys / 8) : f == 2 ? std::ceil(x.keys / 16) : f == 3 ? x.keys : f == 4 ? x.keys - 1 : std::ceil(x.keys / 4);
                    const double soll = (mitV ? x.sv : x.s) * fk;
                    ++n;
                    quot.push_back(x.bits / soll);
                    if (x.bits >= soll && x.bits < soll + 256) ++nah;
                }
                std::sort(quot.begin(), quot.end());
                if (!quot.empty())
                    L("    %-8s %-18s Data*8 / Soll: Median %.3f (10 %% %.3f, 90 %% %.3f); knapp darueber (< 256 Bit) bei %zu von %zu\n", mitV ? "S+Vorz." : "S", fn[f],
                      quot[quot.size() / 2], quot[quot.size() / 10], quot[quot.size() * 9 / 10], nah, n);
            }
        }
        L("\n== D3 DCT: mittlere Bitbreite je Halbbyte-Position (oberstes zuerst) ==\n");
        for (int t = 0; t < 2; ++t)
            if (posN[t] > 0) L("    %s: %.2f %.2f %.2f %.2f  (%.0f Eintraege)\n", t == 0 ? "Drehungen" : "Vektoren ", pos[t][0] / posN[t], pos[t][1] / posN[t],
                               pos[t][2] / posN[t], pos[t][3] / posN[t], posN[t]);
    }
    L("\n== C6 VBR: Verschiebung s je Block (Summe max(0, w - s) mit/ohne Vorzeichenbits) gegen Kopfbyte XX ==\n");
    {
        size_t bloecke = 0, passtO = 0, passtV = 0;
                std::vector<std::pair<int, int>> xxPaare;
        for (const FloatClip& fc : fclips) {
            for (size_t bi = 0; bi + 1 < fc.bloecke.size(); ++bi) {
                const auto& bl = fc.bloecke[bi];
                if (bl.size() < 4) continue;
                const int nutz = static_cast<int>((bl.size() - 3) * 8);
                int bestS = 0, bestRest = 1 << 30, bestArt = 0;
                for (int sh = -8; sh <= 12; ++sh) {
                    int o = 0, v = 0;
                    for (const auto& w : fc.breite) for (int x : w) { const int e2 = std::max(0, x - sh); o += e2; v += e2 + (e2 > 0 ? 1 : 0); }
                    for (int art = 0; art < 2; ++art) {
                        const int soll = art == 0 ? o : v;
                        const int rest = nutz - soll;
                        if (rest >= 0 && rest < bestRest) { bestRest = rest; bestS = sh; bestArt = art; }
                    }
                }
                ++bloecke;
                if (bestRest < 8) { (bestArt == 0 ? passtO : passtV)++; xxPaare.push_back({ bl[1], bestS }); }
            }
        }
        L("  %zu Bloecke: Rest < 8 Bit mit passender Verschiebung bei %zu (ohne Vorzeichen) + %zu (mit)\n", bloecke, passtO, passtV);
        if (xxPaare.size() > 10) {
            double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
            for (const auto& p2 : xxPaare) { sx += p2.first; sy += p2.second; sxx += p2.first * p2.first; syy += p2.second * p2.second; sxy += p2.first * p2.second; }
            const double n = static_cast<double>(xxPaare.size());
            const double vx = sxx - sx * sx / n, vy = syy - sy * sy / n;
            L("  Korrelation XX <-> s ueber die passenden Bloecke: %+.3f (%zu Paare)\n", (vx > 0 && vy > 0) ? (sxy - sx * sy / n) / std::sqrt(vx * vy) : 0.0, xxPaare.size());
        }
    }

    // ==================================================================
    // Runde 11 (0.60.0) - DCT lesen. Befund Runde 10: D1 4016/4016 (oberes
    // Halbbyte = Eintraege je Kanal), D2 Data*8 / (S x Bloecke zu 8 Keys) im
    // Median 0,971 - also knapp: Bloecke zu 8 Keys, alle Eintraege, der letzte
    // (kuerzere) Block wohl mit weniger Koeffizienten.
    // D4: exakte Bitsumme je Variante gegen Data (Byte-/16-Byte-Rundung).
    // D5: Lesen und zurueckrechnen; Pruefstein Einheitslaenge der Drehungen
    //     (Grundwert DeltaBase/16384 bekannt) - Varianten: Bitreihenfolge,
    //     Vorzeichen, Reihenfolge (je Koeffizient/je Komponente), Halbbyte-
    //     Reihenfolge, Grundwert addiert oder nicht, Skala 2^k.
    // ==================================================================
    struct DctClip { size_t i; int keys; size_t nq, nv; std::vector<int> n; std::vector<std::array<int, 4>> eintr; std::vector<size_t> ersterEintrag;
                     std::vector<std::array<double, 4>> basis; std::vector<uint8_t> daten; };
    std::vector<DctClip> dclips;
    {
        const auto it = jeCodec.find("DctAnimationAsset");
        if (it != jeCodec.end()) {
            for (size_t k = 0; k < it->second.size(); ++k) {
                const fbgd::Datensatz& d = datensatz(it->second[k]);
                const fbgd::Wert* dt = FeldBeide(d, "DofTableDescBytes");
                const fbgd::Wert* bps = FeldBeide(d, "BitsPerSubblock");
                const fbgd::Wert* bx = FeldBeide(d, "DeltaBaseX"); const fbgd::Wert* by = FeldBeide(d, "DeltaBaseY");
                const fbgd::Wert* bz = FeldBeide(d, "DeltaBaseZ"); const fbgd::Wert* bw = FeldBeide(d, "DeltaBaseW");
                if (!dt || !bps || !bx || !by || !bz || !bw) continue;
                DctClip dc;
                dc.i = it->second[k];
                dc.keys = static_cast<int>(Zahl(d, "NumKeys", 0));
                dc.nq = static_cast<size_t>(Zahl(d, "NumQuats", 0));
                dc.nv = static_cast<size_t>(Zahl(d, "NumVec3", 0));
                size_t e = 0;
                for (size_t c = 0; c < dt->werte.size(); ++c) {
                    const int n = static_cast<int>((dt->werte[c].ganz >> 4) & 15);
                    dc.n.push_back(n);
                    dc.ersterEintrag.push_back(e);
                    e += static_cast<size_t>(n);
                    const auto wert = [&](const fbgd::Wert* w) { return c < w->werte.size() ? static_cast<double>(w->werte[c].ganz) : 0.0; };
                    dc.basis.push_back({ wert(bx), wert(by), wert(bz), wert(bw) });
                }
                for (const auto& x : bps->werte) {
                    const uint32_t w = static_cast<uint32_t>(x.ganz) & 0xFFFFu;
                    dc.eintr.push_back({ static_cast<int>((w >> 12) & 15u), static_cast<int>((w >> 8) & 15u), static_cast<int>((w >> 4) & 15u), static_cast<int>(w & 15u) });
                }
                dc.daten = Bytes(FeldBeide(d, "Data"));
                dclips.push_back(std::move(dc));
            }
        }
    }
    L("\n== D4 DCT-BITSUMME exakt: Bloecke zu 8 Keys, Varianten fuer den letzten Block, Rundung ==\n");
    {
        // v: 0 alle Eintraege in jedem Block, 1 im letzten Block nur min(n, Keys dort),
        //    2 in jedem Block min(n, Keys dort) und je Block auf Byte gerundet
        const char* vn[3] = { "alle Eintraege je Block", "letzter Block min(n, Keys)", "min(n, Keys) + Byte je Block" };
        for (int v = 0; v < 3; ++v) {
            size_t genau = 0, auf16 = 0, n = 0;
            std::vector<double> diff;
            for (const DctClip& dc : dclips) {
                if (dc.keys < 1 || dc.daten.size() < 16) continue;
                const int nb = (dc.keys + 7) / 8;
                long long bits = 0;
                for (int b = 0; b < nb; ++b) {
                    const int m = (b == nb - 1) ? dc.keys - 8 * (nb - 1) : 8;
                    long long hier = 0;
                    for (size_t c = 0; c < dc.n.size(); ++c) {
                        const int anz = v == 0 ? dc.n[c] : std::min(dc.n[c], m);
                        for (int j = 0; j < anz; ++j) { const auto& w = dc.eintr[dc.ersterEintrag[c] + static_cast<size_t>(j)]; hier += w[0] + w[1] + w[2] + w[3]; }
                    }
                    if (v == 2) hier = (hier + 7) / 8 * 8;
                    bits += hier;
                }
                ++n;
                const long long bytes = (bits + 7) / 8;
                if (bytes == static_cast<long long>(dc.daten.size())) ++genau;
                if ((bytes + 15) / 16 * 16 == static_cast<long long>(dc.daten.size())) ++auf16;
                diff.push_back(static_cast<double>(dc.daten.size()) - static_cast<double>(bytes));
            }
            std::sort(diff.begin(), diff.end());
            if (!diff.empty())
                L("    %-32s Data = Bytes genau bei %zu, = auf 16 Byte gerundet bei %zu von %zu; Data - Bytes: Median %.0f (10 %% %.0f, 90 %% %.0f)\n", vn[v], genau, auf16, n,
                  diff[diff.size() / 2], diff[diff.size() / 10], diff[diff.size() * 9 / 10]);
        }
    }
    L("\n== D5 DCT LESEN: Einheitslaenge der Drehungen (Grundwert DeltaBase/16384) ueber die Keys ==\n");
    {
        struct Var { int lsb, vorz, folge, nibRev, basisDazu; };
        std::vector<Var> var;
        for (int lsb = 0; lsb < 2; ++lsb) for (int vorz = 0; vorz < 2; ++vorz) for (int folge = 0; folge < 2; ++folge)
            for (int nr = 0; nr < 2; ++nr) for (int bd = 0; bd < 2; ++bd) var.push_back({ lsb, vorz, folge, nr, bd });
        // Skalen 2^k fuer die Koeffizienten (relativ zu 1/16384)
        const int kMin = -6, kMax = 8;
        std::vector<std::vector<std::vector<double>>> abweichung(var.size(), std::vector<std::vector<double>>(kMax - kMin + 1));
        size_t clipsD5 = 0;
        for (const DctClip& dc : dclips) {
            if (clipsD5 >= 150) break;
            if (dc.keys < 8 || dc.daten.size() < 16) continue;
            bool drehAnim = false;
            for (size_t c = 0; c < dc.nq && c < dc.n.size(); ++c) if (dc.n[c] > 0) drehAnim = true;
            if (!drehAnim) continue;
            ++clipsD5;
            for (size_t vi = 0; vi < var.size(); ++vi) {
                const Var& v = var[vi];
                size_t bit = 0;
                bool kaputt = false;
                auto lies = [&](int w) -> double {
                    if (w <= 0) return 0.0;
                    const uint32_t r = LiesBits(dc.daten, bit, w, v.lsb == 1);
                    bit += static_cast<size_t>(w);
                    if (r == 0xFFFFFFFFu) { kaputt = true; return 0.0; }
                    if (v.vorz == 0) { const int64_t m = int64_t(1) << w; return static_cast<double>((r & (1u << (w - 1))) ? static_cast<int64_t>(r) - m : static_cast<int64_t>(r)); }
                    const uint32_t betrag = r & ((1u << (w - 1)) - 1u);
                    return (r >> (w - 1)) ? -static_cast<double>(betrag) : static_cast<double>(betrag);
                };
                // nur Block 0 (8 Keys)
                std::vector<std::array<std::array<double, 8>, 4>> koef(dc.n.size());
                for (size_t c = 0; c < dc.n.size() && !kaputt; ++c) {
                    for (auto& kk : koef[c]) kk.fill(0.0);
                    const int anz = std::min(dc.n[c], 8);
                    auto breite = [&](int j, int comp) { const auto& w = dc.eintr[dc.ersterEintrag[c] + static_cast<size_t>(j)]; return v.nibRev ? w[static_cast<size_t>(3 - comp)] : w[static_cast<size_t>(comp)]; };
                    if (v.folge == 0) { for (int j = 0; j < anz; ++j) for (int comp = 0; comp < 4; ++comp) koef[c][static_cast<size_t>(comp)][static_cast<size_t>(j)] = lies(breite(j, comp)); }
                    else { for (int comp = 0; comp < 4; ++comp) for (int j = 0; j < anz; ++j) koef[c][static_cast<size_t>(comp)][static_cast<size_t>(j)] = lies(breite(j, comp)); }
                }
                if (kaputt) continue;
                for (size_t c = 0; c < dc.nq && c < dc.n.size(); ++c) {
                    if (dc.n[c] == 0) continue;
                    for (int kk = kMin; kk <= kMax; ++kk) {
                        const double skala = std::ldexp(1.0, kk) / 16384.0;
                        double summe = 0;
                        for (int t = 0; t < 8; ++t) {
                            double qv[4];
                            for (int comp = 0; comp < 4; ++comp) {
                                const auto& kf = koef[c][static_cast<size_t>(comp)];
                                double sw = kf[0] / std::sqrt(8.0);
                                for (int j = 1; j < 8; ++j) sw += std::sqrt(2.0 / 8.0) * kf[static_cast<size_t>(j)] * std::cos(3.14159265358979 * (2 * t + 1) * j / 16.0);
                                qv[comp] = (v.basisDazu ? dc.basis[c][static_cast<size_t>(comp)] / 16384.0 : 0.0) + sw * skala;
                            }
                            summe += std::fabs(1.0 - std::sqrt(qv[0] * qv[0] + qv[1] * qv[1] + qv[2] * qv[2] + qv[3] * qv[3]));
                        }
                        abweichung[vi][static_cast<size_t>(kk - kMin)].push_back(summe / 8.0);
                    }
                }
            }
        }
        std::vector<std::tuple<double, size_t, int>> rang;
        for (size_t vi = 0; vi < var.size(); ++vi)
            for (int kk = kMin; kk <= kMax; ++kk) {
                auto& f = abweichung[vi][static_cast<size_t>(kk - kMin)];
                if (f.size() < 5) continue;
                std::sort(f.begin(), f.end());
                rang.push_back({ f[f.size() / 2], vi, kk });
            }
        std::sort(rang.begin(), rang.end());
        L("  %zu DCT-Clips mit bewegten Drehungen, Block 0. Median |1 - |q|| (richtig: nahe 0):\n", clipsD5);
        for (size_t r = 0; r < rang.size() && r < 14; ++r) {
            const Var& v = var[std::get<1>(rang[r])];
            L("    %.4f  %s, %s, %s, Halbbytes %s, Grundwert %s, Skala 2^%d/16384\n", std::get<0>(rang[r]), v.lsb ? "LSB" : "MSB", v.vorz ? "Vorz.+Betrag" : "Zweierkompl.",
              v.folge == 0 ? "je Koeffizient" : "je Komponente", v.nibRev ? "unten zuerst" : "oben zuerst", v.basisDazu ? "addiert" : "nicht addiert", std::get<2>(rang[r]));
        }
        if (!rang.empty()) L("    ... schlechteste: %.4f\n", std::get<0>(rang.back()));
    }

    // ==================================================================
    // Runde 12 (0.61.0). D5 in Runde 11 war nicht aussagekraeftig: alle
    // Varianten gewannen mit der KLEINSTEN Skala - dort zaehlt nur noch der
    // Grundwert, und der hat ohnehin Laenge 1. Neu, skalenfrei: die
    // "Tangentenprobe". q(t) = Grundwert b + s*u(t). Eine echte Drehbewegung
    // bleibt auf der Einheitskugel, ihre Aenderung u(t) steht also (fuer
    // kleine s) SENKRECHT auf b: die Schwankung von |q| ist dann klein im
    // Verhaeltnis zur Bewegung der Komponenten. Falsch gelesene Bits zeigen in
    // beliebige Richtungen - dort ist das Verhaeltnis gross (um 0,5).
    // D5b: 64 Varianten (wie D5, dazu Koeffizient 0 mitgerechnet oder nicht).
    // ==================================================================
    L("\n== D5b DCT LESEN, Tangentenprobe: Schwankung |q| / Bewegung der Komponenten (richtig: klein, falsch: ~0,5) ==\n");
    {
        struct Var { int lsb, vorz, folge, nibRev, ohneDC, blockweise; };
        std::vector<Var> var;
        for (int lsb = 0; lsb < 2; ++lsb) for (int vorz = 0; vorz < 2; ++vorz) for (int folge = 0; folge < 2; ++folge)
            for (int nr = 0; nr < 2; ++nr) for (int od = 0; od < 2; ++od) for (int bw = 0; bw < 2; ++bw) var.push_back({ lsb, vorz, folge, nr, od, bw });
        std::vector<std::vector<double>> guete(var.size());
        size_t clipsD5b = 0;
        for (const DctClip& dc : dclips) {
            if (clipsD5b >= 200) break;
            if (dc.keys < 8 || dc.daten.size() < 16) continue;
            bool drehAnim = false;
            for (size_t c = 0; c < dc.nq && c < dc.n.size(); ++c) if (dc.n[c] > 0) drehAnim = true;
            if (!drehAnim) continue;
            ++clipsD5b;
            for (size_t vi = 0; vi < var.size(); ++vi) {
                const Var& v = var[vi];
                size_t bit = 0;
                bool kaputt = false;
                auto lies = [&](int w) -> double {
                    if (w <= 0) return 0.0;
                    const uint32_t r = LiesBits(dc.daten, bit, w, v.lsb == 1);
                    bit += static_cast<size_t>(w);
                    if (r == 0xFFFFFFFFu) { kaputt = true; return 0.0; }
                    if (v.vorz == 0) { const int64_t m = int64_t(1) << w; return static_cast<double>((r & (1u << (w - 1))) ? static_cast<int64_t>(r) - m : static_cast<int64_t>(r)); }
                    const uint32_t betrag = r & ((1u << (w - 1)) - 1u);
                    return (r >> (w - 1)) ? -static_cast<double>(betrag) : static_cast<double>(betrag);
                };
                // blockweise = 0: Kanal fuer Kanal alle Eintraege (Block 0);
                // blockweise = 1: erst alle Kanaele Eintrag 0, dann alle Eintrag 1, ...
                std::vector<std::array<std::array<double, 8>, 4>> koef(dc.n.size());
                for (auto& kk : koef) for (auto& a2 : kk) a2.fill(0.0);
                auto breite = [&](size_t c, int j, int comp) { const auto& w = dc.eintr[dc.ersterEintrag[c] + static_cast<size_t>(j)]; return v.nibRev ? w[static_cast<size_t>(3 - comp)] : w[static_cast<size_t>(comp)]; };
                if (v.blockweise == 0) {
                    for (size_t c = 0; c < dc.n.size() && !kaputt; ++c) {
                        const int anz = std::min(dc.n[c], 8);
                        if (v.folge == 0) { for (int j = 0; j < anz; ++j) for (int comp = 0; comp < 4; ++comp) koef[c][static_cast<size_t>(comp)][static_cast<size_t>(j)] = lies(breite(c, j, comp)); }
                        else { for (int comp = 0; comp < 4; ++comp) for (int j = 0; j < anz; ++j) koef[c][static_cast<size_t>(comp)][static_cast<size_t>(j)] = lies(breite(c, j, comp)); }
                    }
                } else {
                    for (int j = 0; j < 8 && !kaputt; ++j)
                        for (size_t c = 0; c < dc.n.size() && !kaputt; ++c) {
                            if (j >= dc.n[c]) continue;
                            for (int comp = 0; comp < 4; ++comp) koef[c][static_cast<size_t>(comp)][static_cast<size_t>(j)] = lies(breite(c, j, comp));
                        }
                }
                if (kaputt) continue;
                for (size_t c = 0; c < dc.nq && c < dc.n.size(); ++c) {
                    if (dc.n[c] == 0) continue;
                    const double bn = std::sqrt(dc.basis[c][0] * dc.basis[c][0] + dc.basis[c][1] * dc.basis[c][1] + dc.basis[c][2] * dc.basis[c][2] + dc.basis[c][3] * dc.basis[c][3]);
                    if (bn < 1.0) continue;
                    std::array<std::vector<double>, 4> u;
                    for (int comp = 0; comp < 4; ++comp) {
                        const auto& kf = koef[c][static_cast<size_t>(comp)];
                        for (int t = 0; t < 8; ++t) {
                            double sw = v.ohneDC ? 0.0 : kf[0] / std::sqrt(8.0);
                            for (int j = 1; j < 8; ++j) sw += std::sqrt(2.0 / 8.0) * kf[static_cast<size_t>(j)] * std::cos(3.14159265358979 * (2 * t + 1) * j / 16.0);
                            u[static_cast<size_t>(comp)].push_back(sw);
                        }
                    }
                    // kleine Skala: Aenderung 1 % der Grundwertlaenge
                    double umax = 0;
                    for (int comp = 0; comp < 4; ++comp) for (double x : u[static_cast<size_t>(comp)]) umax = std::max(umax, std::fabs(x));
                    if (umax < 1e-9) continue;
                    const double sk = 0.01 / umax;
                    double bewegung = 0;
                    for (int comp = 0; comp < 4; ++comp) {
                        double m = 0, qq = 0;
                        for (double x : u[static_cast<size_t>(comp)]) m += x;
                        m /= 8.0;
                        for (double x : u[static_cast<size_t>(comp)]) qq += (x - m) * (x - m);
                        bewegung += std::sqrt(qq / 8.0) * sk / 4.0;
                    }
                    if (bewegung < 1e-12) continue;
                    std::vector<double> normen;
                    for (int t = 0; t < 8; ++t) {
                        double sq = 0;
                        for (int comp = 0; comp < 4; ++comp) { const double x = dc.basis[c][static_cast<size_t>(comp)] / bn + sk * u[static_cast<size_t>(comp)][static_cast<size_t>(t)]; sq += x * x; }
                        normen.push_back(std::sqrt(sq));
                    }
                    double m = 0, qq = 0;
                    for (double x : normen) m += x;
                    m /= 8.0;
                    for (double x : normen) qq += (x - m) * (x - m);
                    guete[vi].push_back(std::sqrt(qq / 8.0) / bewegung);
                }
            }
        }
        std::vector<std::pair<double, size_t>> rang;
        for (size_t vi = 0; vi < var.size(); ++vi) {
            if (guete[vi].size() < 5) continue;
            std::sort(guete[vi].begin(), guete[vi].end());
            rang.push_back({ guete[vi][guete[vi].size() / 2], vi });
        }
        std::sort(rang.begin(), rang.end());
        L("  %zu DCT-Clips, Block 0:\n", clipsD5b);
        for (size_t r = 0; r < rang.size() && r < 16; ++r) {
            const Var& v = var[rang[r].second];
            L("    %.4f  %s, %s, %s, Halbbytes %s, Koeff. 0 %s, %s  (%zu Kanaele)\n", rang[r].first, v.lsb ? "LSB" : "MSB", v.vorz ? "Vorz.+Betrag" : "Zweierkompl.",
              v.folge == 0 ? "je Koeffizient" : "je Komponente", v.nibRev ? "unten zuerst" : "oben zuerst", v.ohneDC ? "ohne" : "mit", v.blockweise ? "Eintrag fuer Eintrag ueber alle Kanaele" : "Kanal fuer Kanal",
              guete[rang[r].second].size());
        }
        if (!rang.empty()) L("    ... schlechteste: %.4f\n", rang.back().first);
    }

    // ==================================================================
    // Runde 13 (0.62.0). D5b: alle 64 Varianten 0,56-0,71 (Zufall ~0,5-1) -
    // die Reihenfolge der Kanaele im Strom stimmt nicht, oder es liegt etwas
    // vor den Werten. Deshalb jetzt die EINFACHSTEN Faelle, bei denen die
    // Kanal-Reihenfolge keine Rolle spielt:
    // D5c: DCT-Clips mit genau EINER bewegten Drehung - Varianten ueber
    //      Bloecke/Eintraege/Komponenten, Tangentenprobe ueber alle Bloecke.
    // D6:  die drei kleinsten dieser Clips komplett als Hex (Eintraege,
    //      Grundwert, Data) fuer die Auswertung von Hand.
    // C7:  VBR-Clips mit genau EINER bewegten Float-Spur: Bloecke als Hex.
    // ==================================================================
    L("\n== D5c DCT, nur der ERSTE bewegte Kanal (eine Drehung): Reihenfolge ueber Bloecke, Tangentenprobe ==\n");
    std::vector<size_t> einzeln;                 // Index in dclips
    for (size_t k = 0; k < dclips.size(); ++k) {
        const DctClip& dc = dclips[k];
        if (dc.keys < 9) continue;
        // 0.63.0: in Runde 13 gab es KEINEN Clip mit genau einer bewegten Drehung.
        // Jetzt reicht: der ERSTE bewegte Kanal ist eine Drehung. Bei kanalweiser
        // Ordnung stehen seine Daten vorn, bei blockweiser seine Block-0-Daten -
        // bewertet wird nur dieser Kanal, die Reihenfolge der uebrigen stoert nicht.
        size_t erster = dc.n.size();
        for (size_t c = 0; c < dc.n.size(); ++c) if (dc.n[c] > 0) { erster = c; break; }
        if (erster < dc.nq) einzeln.push_back(k);
        if (einzeln.size() >= 400) break;
    }
    L("  %zu Clips, deren erster bewegter Kanal eine Drehung ist (>= 9 Keys)\n", einzeln.size());
    {
        // ord: 0 Block -> Eintrag -> Komponente, 1 Block -> Komponente -> Eintrag,
        //      2 Komponente -> Block -> Eintrag, 3 Eintrag -> Block -> Komponente
        struct Var { int lsb, vorz, ord, nibRev, start; };
        std::vector<Var> var;
        for (int lsb = 0; lsb < 2; ++lsb) for (int vorz = 0; vorz < 2; ++vorz) for (int ord = 0; ord < 4; ++ord)
            for (int nr = 0; nr < 2; ++nr) for (int st = 0; st < 2; ++st) var.push_back({ lsb, vorz, ord, nr, st });
        std::vector<std::vector<double>> guete(var.size());
        for (size_t vi = 0; vi < var.size(); ++vi) {
            const Var& v = var[vi];
            for (size_t k : einzeln) {
                const DctClip& dc = dclips[k];
                size_t c0 = 0;
                for (size_t c = 0; c < dc.n.size(); ++c) if (dc.n[c] > 0) { c0 = c; break; }
                const int nE = dc.n[c0];
                // ord 0/1 (blockweise): nur Block 0 dieses Kanals steht sicher vorn
                const int nb = (v.ord <= 1) ? 1 : (dc.keys + 7) / 8;
                // Start: 0 = Byte 0, 1 = hinter einem Sammelbereich am Anfang (Data - Summe der Breiten)
                long long summe = 0;
                const int nbAlle = (dc.keys + 7) / 8;
                for (size_t c = 0; c < dc.n.size(); ++c)
                    for (int j = 0; j < dc.n[c]; ++j) { const auto& w = dc.eintr[dc.ersterEintrag[c] + static_cast<size_t>(j)]; summe += static_cast<long long>(w[0] + w[1] + w[2] + w[3]) * nbAlle; }
                const long long rest = static_cast<long long>(dc.daten.size()) * 8 - summe;
                size_t bit = (v.start == 1 && rest > 0) ? static_cast<size_t>(rest) : 0;
                bool kaputt = false;
                auto lies = [&](int w) -> double {
                    if (w <= 0) return 0.0;
                    const uint32_t r = LiesBits(dc.daten, bit, w, v.lsb == 1);
                    bit += static_cast<size_t>(w);
                    if (r == 0xFFFFFFFFu) { kaputt = true; return 0.0; }
                    if (v.vorz == 0) { const int64_t m = int64_t(1) << w; return static_cast<double>((r & (1u << (w - 1))) ? static_cast<int64_t>(r) - m : static_cast<int64_t>(r)); }
                    const uint32_t betrag = r & ((1u << (w - 1)) - 1u);
                    return (r >> (w - 1)) ? -static_cast<double>(betrag) : static_cast<double>(betrag);
                };
                auto breite = [&](int j, int comp) { const auto& w = dc.eintr[dc.ersterEintrag[c0] + static_cast<size_t>(j)]; return v.nibRev ? w[static_cast<size_t>(3 - comp)] : w[static_cast<size_t>(comp)]; };
                std::vector<std::array<std::array<double, 8>, 4>> kf(static_cast<size_t>(nb));
                for (auto& x : kf) for (auto& y : x) y.fill(0.0);
                auto setze = [&](int b, int j, int comp) { kf[static_cast<size_t>(b)][static_cast<size_t>(comp)][static_cast<size_t>(j)] = lies(breite(j, comp)); };
                if (v.ord == 0) { for (int b = 0; b < nb; ++b) for (int j = 0; j < nE; ++j) for (int comp = 0; comp < 4; ++comp) setze(b, j, comp); }
                else if (v.ord == 1) { for (int b = 0; b < nb; ++b) for (int comp = 0; comp < 4; ++comp) for (int j = 0; j < nE; ++j) setze(b, j, comp); }
                else if (v.ord == 2) { for (int comp = 0; comp < 4; ++comp) for (int b = 0; b < nb; ++b) for (int j = 0; j < nE; ++j) setze(b, j, comp); }
                else { for (int j = 0; j < nE; ++j) for (int b = 0; b < nb; ++b) for (int comp = 0; comp < 4; ++comp) setze(b, j, comp); }
                if (kaputt) continue;
                const auto& ba = dc.basis[c0];
                const double bn = std::sqrt(ba[0] * ba[0] + ba[1] * ba[1] + ba[2] * ba[2] + ba[3] * ba[3]);
                if (bn < 1.0) continue;
                std::array<std::vector<double>, 4> u;
                for (int b = 0; b < nb; ++b)
                    for (int t = 0; t < 8; ++t)
                        for (int comp = 0; comp < 4; ++comp) {
                            const auto& k2 = kf[static_cast<size_t>(b)][static_cast<size_t>(comp)];
                            double sw = k2[0] / std::sqrt(8.0);
                            for (int j = 1; j < 8; ++j) sw += std::sqrt(2.0 / 8.0) * k2[static_cast<size_t>(j)] * std::cos(3.14159265358979 * (2 * t + 1) * j / 16.0);
                            u[static_cast<size_t>(comp)].push_back(sw);
                        }
                double umax = 0;
                for (int comp = 0; comp < 4; ++comp) for (double x : u[static_cast<size_t>(comp)]) umax = std::max(umax, std::fabs(x));
                if (umax < 1e-9) continue;
                const double sk = 0.01 / umax;
                const size_t n = u[0].size();
                double bewegung = 0;
                for (int comp = 0; comp < 4; ++comp) {
                    double m = 0, qq = 0;
                    for (double x : u[static_cast<size_t>(comp)]) m += x;
                    m /= static_cast<double>(n);
                    for (double x : u[static_cast<size_t>(comp)]) qq += (x - m) * (x - m);
                    bewegung += std::sqrt(qq / static_cast<double>(n)) * sk / 4.0;
                }
                if (bewegung < 1e-12) continue;
                std::vector<double> normen;
                for (size_t t = 0; t < n; ++t) {
                    double sq = 0;
                    for (int comp = 0; comp < 4; ++comp) { const double x = ba[static_cast<size_t>(comp)] / bn + sk * u[static_cast<size_t>(comp)][t]; sq += x * x; }
                    normen.push_back(std::sqrt(sq));
                }
                double m = 0, qq = 0;
                for (double x : normen) m += x;
                m /= static_cast<double>(n);
                for (double x : normen) qq += (x - m) * (x - m);
                guete[vi].push_back(std::sqrt(qq / static_cast<double>(n)) / bewegung);
            }
        }
        std::vector<std::pair<double, size_t>> rang;
        for (size_t vi = 0; vi < var.size(); ++vi) {
            if (guete[vi].size() < 3) continue;
            std::sort(guete[vi].begin(), guete[vi].end());
            rang.push_back({ guete[vi][guete[vi].size() / 2], vi });
        }
        std::sort(rang.begin(), rang.end());
        const char* on[4] = { "Block>Eintrag>Komponente", "Block>Komponente>Eintrag", "Komponente>Block>Eintrag", "Eintrag>Block>Komponente" };
        for (size_t r = 0; r < rang.size() && r < 12; ++r) {
            const Var& v = var[rang[r].second];
            L("    %.4f  %s, %s, %s, Halbbytes %s, Start %s  (%zu Clips)\n", rang[r].first, v.lsb ? "LSB" : "MSB", v.vorz ? "Vorz.+Betrag" : "Zweierkompl.", on[v.ord],
              v.nibRev ? "unten zuerst" : "oben zuerst", v.start ? "hinter Sammelbereich" : "Byte 0", guete[rang[r].second].size());
        }
        if (!rang.empty()) L("    ... schlechteste: %.4f\n", rang.back().first);
    }
    // D5d (0.64.0): Runde 14 - beste Lesart 0,383 (MSB, Zweierkomplement,
    // Block > Eintrag > Komponente, Halbbytes oben zuerst), deutlich besser als
    // Zufall (bis 0,92), aber weit weg von ~0,02. Jetzt um diese Lesart herum:
    // alle 24 Zuordnungen der vier Halbbytes zu X/Y/Z/W, Koeffizient 0 mit
    // oder ohne, nur Clips ohne Sammelbereich (CatchAllBitCount = 0).
    L("\n== D5d DCT um die beste Lesart: 24 Komponenten-Zuordnungen x Koeffizient 0, nur CatchAll = 0 ==\n");
    {
        int perm[24][4];
        {
            int p4[4] = { 0, 1, 2, 3 }, np = 0;
            std::sort(p4, p4 + 4);
            do { for (int i = 0; i < 4; ++i) perm[np][i] = p4[i]; ++np; } while (std::next_permutation(p4, p4 + 4) && np < 24);
        }
        struct Var { int lsb, vorz, ord, perm, ohneDC; };
        std::vector<Var> var;
        for (int lsb = 0; lsb < 2; ++lsb) for (int vorz = 0; vorz < 2; ++vorz) for (int ord = 0; ord < 2; ++ord)
            for (int pm = 0; pm < 24; ++pm) for (int od = 0; od < 2; ++od) var.push_back({ lsb, vorz, ord, pm, od });
        std::vector<std::vector<double>> guete(var.size());
        size_t clipsD5d = 0;
        for (size_t k : einzeln) {
            const DctClip& dc = dclips[k];
            if (Zahl(datensatz(dc.i), "CatchAllBitCount", -1) != 0) continue;
            ++clipsD5d;
            size_t c0 = 0;
            for (size_t c = 0; c < dc.n.size(); ++c) if (dc.n[c] > 0) { c0 = c; break; }
            const int nE = std::min(dc.n[c0], 8);
            const auto& ba = dc.basis[c0];
            const double bn = std::sqrt(ba[0] * ba[0] + ba[1] * ba[1] + ba[2] * ba[2] + ba[3] * ba[3]);
            if (bn < 1.0) continue;
            for (size_t vi = 0; vi < var.size(); ++vi) {
                const Var& v = var[vi];
                size_t bit = 0;
                bool kaputt = false;
                auto lies = [&](int w) -> double {
                    if (w <= 0) return 0.0;
                    const uint32_t r = LiesBits(dc.daten, bit, w, v.lsb == 1);
                    bit += static_cast<size_t>(w);
                    if (r == 0xFFFFFFFFu) { kaputt = true; return 0.0; }
                    if (v.vorz == 0) { const int64_t m = int64_t(1) << w; return static_cast<double>((r & (1u << (w - 1))) ? static_cast<int64_t>(r) - m : static_cast<int64_t>(r)); }
                    const uint32_t betrag = r & ((1u << (w - 1)) - 1u);
                    return (r >> (w - 1)) ? -static_cast<double>(betrag) : static_cast<double>(betrag);
                };
                // Block 0 des ersten bewegten Kanals; Halbbyte i (oben zuerst) gehoert zu Komponente perm[i]
                std::array<std::array<double, 8>, 4> kf{};
                for (auto& a2 : kf) a2.fill(0.0);
                const auto& e0 = dc.eintr;
                auto nib = [&](int j, int i) { return e0[dc.ersterEintrag[c0] + static_cast<size_t>(j)][static_cast<size_t>(i)]; };
                if (v.ord == 0) { for (int j = 0; j < nE; ++j) for (int i = 0; i < 4; ++i) kf[static_cast<size_t>(perm[v.perm][i])][static_cast<size_t>(j)] = lies(nib(j, i)); }
                else { for (int i = 0; i < 4; ++i) for (int j = 0; j < nE; ++j) kf[static_cast<size_t>(perm[v.perm][i])][static_cast<size_t>(j)] = lies(nib(j, i)); }
                if (kaputt) continue;
                std::array<std::vector<double>, 4> u;
                for (int t = 0; t < 8; ++t)
                    for (int comp = 0; comp < 4; ++comp) {
                        const auto& k2 = kf[static_cast<size_t>(comp)];
                        double sw = v.ohneDC ? 0.0 : k2[0] / std::sqrt(8.0);
                        for (int j = 1; j < 8; ++j) sw += std::sqrt(2.0 / 8.0) * k2[static_cast<size_t>(j)] * std::cos(3.14159265358979 * (2 * t + 1) * j / 16.0);
                        u[static_cast<size_t>(comp)].push_back(sw);
                    }
                double umax = 0;
                for (int comp = 0; comp < 4; ++comp) for (double x : u[static_cast<size_t>(comp)]) umax = std::max(umax, std::fabs(x));
                if (umax < 1e-9) continue;
                const double sk = 0.01 / umax;
                double bewegung = 0;
                for (int comp = 0; comp < 4; ++comp) {
                    double m = 0, qq = 0;
                    for (double x : u[static_cast<size_t>(comp)]) m += x;
                    m /= 8.0;
                    for (double x : u[static_cast<size_t>(comp)]) qq += (x - m) * (x - m);
                    bewegung += std::sqrt(qq / 8.0) * sk / 4.0;
                }
                if (bewegung < 1e-12) continue;
                std::vector<double> normen;
                for (int t = 0; t < 8; ++t) {
                    double sq = 0;
                    for (int comp = 0; comp < 4; ++comp) { const double x = ba[static_cast<size_t>(comp)] / bn + sk * u[static_cast<size_t>(comp)][static_cast<size_t>(t)]; sq += x * x; }
                    normen.push_back(std::sqrt(sq));
                }
                double m = 0, qq = 0;
                for (double x : normen) m += x;
                m /= 8.0;
                for (double x : normen) qq += (x - m) * (x - m);
                guete[vi].push_back(std::sqrt(qq / 8.0) / bewegung);
            }
        }
        std::vector<std::pair<double, size_t>> rang;
        for (size_t vi = 0; vi < var.size(); ++vi) {
            if (guete[vi].size() < 3) continue;
            std::sort(guete[vi].begin(), guete[vi].end());
            rang.push_back({ guete[vi][guete[vi].size() / 2], vi });
        }
        std::sort(rang.begin(), rang.end());
        L("  %zu Clips ohne Sammelbereich\n", clipsD5d);
        for (size_t r = 0; r < rang.size() && r < 12; ++r) {
            const Var& v = var[rang[r].second];
            L("    %.4f  %s, %s, %s, Halbbytes -> %c%c%c%c, Koeff. 0 %s  (%zu Clips)\n", rang[r].first, v.lsb ? "LSB" : "MSB", v.vorz ? "Vorz.+Betrag" : "Zweierkompl.",
              v.ord == 0 ? "Eintrag>Komponente" : "Komponente>Eintrag", "XYZW"[perm[v.perm][0]], "XYZW"[perm[v.perm][1]], "XYZW"[perm[v.perm][2]], "XYZW"[perm[v.perm][3]],
              v.ohneDC ? "ohne" : "mit", guete[rang[r].second].size());
        }
        if (!rang.empty()) L("    ... schlechteste: %.4f\n", rang.back().first);
    }

    // D6: die kleinsten Einzel-Drehungs-Clips komplett als Hex in die Hexdatei
    std::string d6Text;
    {
        // 0.63.0: die DCT-Clips mit den WENIGSTEN bewegten Kanaelen (>= 9 Keys)
        std::vector<std::pair<size_t, size_t>> gr;
        for (size_t k = 0; k < dclips.size(); ++k) {
            const DctClip& dc = dclips[k];
            if (dc.keys < 9 || dc.daten.size() < 16) continue;
            size_t bewegt = 0;
            for (int x : dc.n) if (x > 0) ++bewegt;
            if (bewegt == 0) continue;                         // 0.64.0: Runde 14 zeigte nur Clips ganz ohne Bewegung
            gr.push_back({ bewegt * 100000 + dc.daten.size(), k });
        }
        std::sort(gr.begin(), gr.end());
        for (size_t g = 0; g < gr.size() && g < 3; ++g) {
            const DctClip& dc = dclips[gr[g].second];
            char t[300];
            std::snprintf(t, sizeof t, "==== D6 DCT %s  Keys %d  Drehungen %zu Vektoren %zu  CatchAll %.0f  QuantizeMultBlock %.0f  Data %zu Byte\n",
                          clips[dc.i].anzeige.c_str(), dc.keys, dc.nq, dc.nv, Zahl(datensatz(dc.i), "CatchAllBitCount", -1),
                          Zahl(datensatz(dc.i), "QuantizeMultBlock", -1), dc.daten.size());
            d6Text += t;
            for (size_t c = 0; c < dc.n.size(); ++c) {
                if (dc.n[c] == 0) continue;
                std::snprintf(t, sizeof t, "  Kanal %zu (%s): Grundwert %.0f %.0f %.0f %.0f, Eintraege (Halbbytes oben zuerst):", c, c < dc.nq ? "Drehung" : "Vektor",
                              dc.basis[c][0], dc.basis[c][1], dc.basis[c][2], dc.basis[c][3]);
                d6Text += t;
                for (int j = 0; j < dc.n[c]; ++j) { const auto& w = dc.eintr[dc.ersterEintrag[c] + static_cast<size_t>(j)]; std::snprintf(t, sizeof t, " %d/%d/%d/%d", w[0], w[1], w[2], w[3]); d6Text += t; }
                d6Text += "\n";
            }
            d6Text += "  KeyTimes:";
            if (const fbgd::Wert* kt = FeldBeide(datensatz(dc.i), "KeyTimes")) for (const auto& x : kt->werte) { std::snprintf(t, sizeof t, " %lld", static_cast<long long>(x.ganz)); d6Text += t; }
            d6Text += "\n";
            for (size_t i2 = 0; i2 < dc.daten.size(); i2 += 16) {
                std::snprintf(t, sizeof t, "  %06zx ", i2);
                d6Text += t;
                for (size_t j2 = 0; j2 < 16 && i2 + j2 < dc.daten.size(); ++j2) { std::snprintf(t, sizeof t, " %02x", dc.daten[i2 + j2]); d6Text += t; }
                d6Text += "\n";
            }
            d6Text += "\n";
        }
    }
    // C7: VBR mit genau einer bewegten Float-Spur - Bloecke als Hex
    std::string c7Text;
    {
        size_t gezeigt = 0;
        std::vector<std::pair<size_t, size_t>> wenig;
        for (size_t k = 0; k < fclips.size(); ++k) wenig.push_back({ fclips[k].breite.size() * 100000 + fclips[k].bloecke.size(), k });
        std::sort(wenig.begin(), wenig.end());
        for (const auto& wk : wenig) {
            const FloatClip& fc = fclips[wk.second];
            if (gezeigt >= 4) break;
            ++gezeigt;
            char t[300];
            const fbgd::Datensatz& d = datensatz(fc.i);
            std::snprintf(t, sizeof t, "==== C7 VBR %s  Keys %d  Spuren %zu  FloatMin %g FloatMax %g Dct %g FloatOffsetScale %g\n",
                          clips[fc.i].anzeige.c_str(), fc.keys, fc.breite.size(), Zahl(d, "FloatMin", 0), Zahl(d, "FloatMax", 0), Zahl(d, "Dct", 0), Zahl(d, "FloatOffsetScale", 0));
            c7Text += t;
            for (size_t c = 0; c < fc.breite.size(); ++c) {
                std::snprintf(t, sizeof t, "  Spur %zu Breiten: %d %d %d %d %d %d %d %d\n", c, fc.breite[c][0], fc.breite[c][1], fc.breite[c][2], fc.breite[c][3],
                              fc.breite[c][4], fc.breite[c][5], fc.breite[c][6], fc.breite[c][7]);
                c7Text += t;
            }
            for (size_t bi = 0; bi < fc.bloecke.size(); ++bi) {
                std::snprintf(t, sizeof t, "  Block %2zu (%3zu Byte):", bi, fc.bloecke[bi].size());
                c7Text += t;
                for (uint8_t x : fc.bloecke[bi]) { std::snprintf(t, sizeof t, " %02x", x); c7Text += t; }
                c7Text += "\n";
            }
            c7Text += "\n";
        }
        L("\n== C7 VBR, Float-Clips mit den wenigsten Spuren: %zu Clips als Hex in die Hexdatei ==\n", gezeigt);
    }

    // ==================================================================
    // E1 (0.65.0): die NEUEN Decoder (fbanim: VbrLesen, DctLesen) an allen
    // VBR- und DCT-Clips. Pruefsteine ohne Vergleichsdaten: gelingt das Lesen
    // (Bloecke gehen auf), haben animierte Drehungen Laenge 1, springen sie
    // zwischen zwei Keys nicht (Winkel), und liegen konstante Drehungen nahe
    // der Ruhelage (nur nicht-additive Clips).
    // ==================================================================
    L("\n== E1 NEUE DECODER an allen VBR- und DCT-Clips ==\n");
    {
        for (const char* kl : { "VbrAnimationAsset", "DctAnimationAsset" }) {
            const auto it = jeCodec.find(kl);
            if (it == jeCodec.end()) continue;
            size_t gut = 0, schlecht = 0;
            std::map<std::string, size_t> gruende;
            std::vector<double> laenge, sprung;
            size_t additiv = 0;
            const auto t1 = std::chrono::steady_clock::now();
            size_t nr = 0;
            for (size_t k : it->second) {
                // 0.65.2: jeder dritte Clip reicht fuer die Statistik (Runde 17: 26 Minuten fuer alle)
                if (nr++ % 3 != 0) continue;
                fbanim::Clip cl;
                std::string f2;
                if (!q.Entpacke(clips[k], cl, f2)) { ++schlecht; ++gruende[f2.substr(0, 40)]; continue; }
                ++gut;
                if (cl.additiv) ++additiv;
                for (const fbanim::Kanal& kn : cl.kanaele) {
                    if (kn.art != 'q' || kn.konstant) continue;
                    const size_t n = kn.werte.size() / 4;
                    double vorher[4] = {};
                    for (size_t i = 0; i < n; ++i) {
                        const float* v = &kn.werte[i * 4];
                        const double l = std::sqrt(double(v[0]) * v[0] + double(v[1]) * v[1] + double(v[2]) * v[2] + double(v[3]) * v[3]);
                        if (laenge.size() < 2000000) laenge.push_back(std::fabs(1.0 - l));
                        if (i > 0 && l > 1e-6) {
                            double dot = 0, lv = 0;
                            for (int c = 0; c < 4; ++c) { dot += vorher[c] * v[c]; lv += vorher[c] * vorher[c]; }
                            if (lv > 1e-12) {
                                const double cw = std::min(1.0, std::fabs(dot) / (std::sqrt(lv) * l));
                                if (sprung.size() < 2000000) sprung.push_back(2.0 * std::acos(cw) * 180.0 / 3.14159265358979);
                            }
                        }
                        for (int c = 0; c < 4; ++c) vorher[c] = v[c];
                    }
                }
            }
            const double sek = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
            std::sort(laenge.begin(), laenge.end());
            std::sort(sprung.begin(), sprung.end());
            L("  %s: gelesen %zu, nicht gelesen %zu (additiv %zu), %.1f s\n", kl, gut, schlecht, additiv, sek);
            for (const auto& g : gruende) L("    Grund: %-40s %zu\n", g.first.c_str(), g.second);
            if (!laenge.empty())
                L("    animierte Drehungen: |1 - |q|| Median %.5f, 99 %% %.5f, max %.4f (%zu Werte)\n", laenge[laenge.size() / 2],
                  laenge[laenge.size() * 99 / 100], laenge.back(), laenge.size());
            if (!sprung.empty())
                L("    Winkel zwischen zwei Keys: Median %.3f Grad, 99 %% %.2f Grad, max %.1f Grad\n", sprung[sprung.size() / 2],
                  sprung[sprung.size() * 99 / 100], sprung.back());
        }
    }

    // ImHex (0.52.0): die kleinsten Clips als .bin plus eine .hexpat mit der
    // bekannten Aufteilung - in ImHex farbig aufgeschluesselt ansehen.
    {
        std::string ordner = hexPfad;
        const size_t sl = ordner.find_last_of("/\\");
        ordner = sl == std::string::npos ? std::string() : ordner.substr(0, sl + 1);
        size_t geschrieben = 0;
        std::vector<std::pair<double, size_t>> klein;
        for (const VbrKopf& h : vk) {
            if (std::fabs(4 * h.cq + 3 * h.cv + h.cf + h.map + 16 * h.aq + 12 * h.av + 4 * h.af + h.fo + h.vo + h.kt + h.bl - h.d) >= 0.5) continue;
            klein.push_back({ h.d + (h.aq > 0 ? 0.0 : 1e6), static_cast<size_t>(&h - &vk[0]) });   // Drehungs-Clips zuerst
        }
        std::sort(klein.begin(), klein.end());
        for (size_t kk = 0; kk < klein.size() && geschrieben < 3; ++kk) {
            const VbrKopf& h = vk[klein[kk].second];
            const fbgd::Datensatz& d = datensatz(h.i);
            const std::vector<uint8_t> b = Bytes(FeldBeide(d, "Data"));
            const std::string basis = ordner + "vbr_" + clips[h.i].key;
            FILE* fb2 = std::fopen((basis + ".bin").c_str(), "wb");
            if (fb2 == nullptr) continue;
            std::fwrite(b.data(), 1, b.size(), fb2);
            std::fclose(fb2);
            FILE* fp = std::fopen((basis + ".hexpat").c_str(), "wb");
            if (fp == nullptr) continue;
            std::fprintf(fp, "// %s  (VBR, %zu Byte) - SWBF2 Import Codec-Labor 0.81.0\n", clips[h.i].anzeige.c_str(), b.size());
            std::fprintf(fp, "// Aufteilung gemessen an 11 637 von 11 720 VBR-Clips; Bloecke beginnen meist mit ff XX XX.\n");
            std::fprintf(fp, "bitfield Breiten { k0 : 4; k1 : 4; k2 : 4; k3 : 4; k4 : 4; k5 : 4; k6 : 4; k7 : 4; };\n");
            std::fprintf(fp, "struct Blockkopf { u8 maske; u8 a; u8 b; };\n");
            // 0.56.0: Keyzeiten GANZ VORN (Abstaende in Bildern), dann die Palettentabelle -
            // an der Erst-Verwendungs-Reihenfolge der Indizes bestaetigt.
            const size_t kt0 = static_cast<size_t>(h.kt);
            std::fprintf(fp, "u8 keyZeiten[%d] @ 0x00;           // Abstaende in Bildern\n", static_cast<int>(h.kt));
            std::fprintf(fp, "u8 konstDrehungen[%d] @ 0x%zX;      // 4 Palettenindizes je Drehung (xyzw)\n", static_cast<int>(4 * h.cq), kt0);
            std::fprintf(fp, "u8 konstVektoren[%d] @ 0x%zX;       // 3 je Vektor\n", static_cast<int>(3 * h.cv), kt0 + static_cast<size_t>(4 * h.cq));
            std::fprintf(fp, "u8 konstFloats[%d] @ 0x%zX;\n", static_cast<int>(h.cf), kt0 + static_cast<size_t>(4 * h.cq + 3 * h.cv));
            size_t o = kt0 + static_cast<size_t>(4 * h.cq + 3 * h.cv + h.cf);
            std::fprintf(fp, "u8 lauflaengen[%d] @ 0x%zX;        // abwechselnd animiert/konstant\n", static_cast<int>(h.map), o);
            o += static_cast<size_t>(h.map);
            std::fprintf(fp, "Breiten animDrehung[%d] @ 0x%zX;   // je Komponente 8 Halbbytes\n", static_cast<int>(4 * h.aq), o);
            o += static_cast<size_t>(16 * h.aq);
            std::fprintf(fp, "Breiten animVektor[%d] @ 0x%zX;\n", static_cast<int>(3 * h.av), o);
            o += static_cast<size_t>(12 * h.av);
            std::fprintf(fp, "Breiten animFloat[%d] @ 0x%zX;\n", static_cast<int>(h.af), o);
            o += static_cast<size_t>(4 * h.af);
            std::fprintf(fp, "u8 floatOffsets[%d] @ 0x%zX;\n", static_cast<int>(h.fo), o);
            o += static_cast<size_t>(h.fo);
            std::fprintf(fp, "u8 vektorOffsets[%d] @ 0x%zX;\n", static_cast<int>(h.vo), o);
            o += static_cast<size_t>(h.vo);

            if (const fbgd::Wert* fbs = FeldBeide(d, "FrameBlockSizes")) {
                size_t nr = 0;
                for (const auto& x : fbs->werte) {
                    std::fprintf(fp, "Blockkopf kopf%zu @ 0x%zX;\nu8 block%zu[%lld] @ 0x%zX;\n", nr, o, nr, static_cast<long long>(x.ganz) - 3, o + 3);
                    o += static_cast<size_t>(x.ganz);
                    ++nr;
                }
            }
            std::fclose(fp);
            ++geschrieben;
            L("  IMHEX %s.bin + .hexpat  (%s, %zu Byte)\n", basis.c_str(), clips[h.i].anzeige.c_str(), b.size());
        }
    }

    // ------------------------------------------------------------------
    // A7 Hexdateien
    // ------------------------------------------------------------------
    FILE* hx = std::fopen(hexPfad.c_str(), "wb");
    if (hx != nullptr) {
        std::fputs(d6Text.c_str(), hx);          // 0.62.0: Einzeldrehungen (DCT) und Einzelspuren (VBR) zuerst
        std::fputs(c7Text.c_str(), hx);
        for (const char* kl : unbekannt) {
            const auto it = jeCodec.find(kl);
            if (it == jeCodec.end()) continue;
            // Je Codec die 3 kleinsten Clips - und (0.51.0) die 3 kleinsten mit
            // Drehungen und mehr als 2 Keys: die Nur-Float-Clips allein zeigen
            // die Drehspuren nicht.
            std::vector<std::pair<size_t, size_t>> gr, grDreh;
            for (size_t k = 0; k < it->second.size() && k < probe * 5; ++k) {
                const fbgd::Datensatz& dd = datensatz(it->second[k]);
                const std::vector<uint8_t> b = Bytes(FeldBeide(dd, "Data"));
                if (b.size() < 16) continue;
                gr.push_back({ b.size(), it->second[k] });
                const double drehungen = std::max(Zahl(dd, "QuaternionCount", 0), Zahl(dd, "NumQuats", 0));
                if (drehungen > 0 && Zahl(dd, "NumKeys", 0) > 2) grDreh.push_back({ b.size(), it->second[k] });
            }
            std::sort(gr.begin(), gr.end());
            std::sort(grDreh.begin(), grDreh.end());
            for (size_t g = 0; g < 3 && g < grDreh.size(); ++g) gr.insert(gr.begin() + static_cast<long>(std::min<size_t>(3 + g, gr.size())), grDreh[g]);
            for (size_t g = 0; g < gr.size() && g < 6; ++g) {
                const fbanim::ClipEintrag& c = clips[gr[g].second];
                const fbgd::Datensatz& d = datensatz(gr[g].second);
                std::fprintf(hx, "==== %s  %s  key %s  bank %s\n", kl, c.anzeige.c_str(), c.key.c_str(), q.BankName(c.bank).c_str());
                for (const auto* fs : { &d.basis, &d.felder }) {
                    for (const auto& fp : *fs) {
                        const fbgd::Wert& w = fp.second;
                        if (w.art == fbgd::Wert::Art::Feld) {
                            std::fprintf(hx, "  %s: Array<%s> n=%u:", fp.first.c_str(), w.typ.c_str(), w.anzahl);
                            for (size_t i = 0; i < w.werte.size() && i < 48; ++i) {
                                if (w.werte[i].art == fbgd::Wert::Art::Gleit) std::fprintf(hx, " %g", static_cast<double>(w.werte[i].gleit));
                                else std::fprintf(hx, " %lld", static_cast<long long>(w.werte[i].ganz));
                            }
                            std::fprintf(hx, "%s\n", w.werte.size() > 48 ? " ..." : "");
                        } else if (w.art == fbgd::Wert::Art::Gleit) std::fprintf(hx, "  %s: %g\n", fp.first.c_str(), static_cast<double>(w.gleit));
                        else if (w.art == fbgd::Wert::Art::Hex || w.art == fbgd::Wert::Art::Text) std::fprintf(hx, "  %s: %s\n", fp.first.c_str(), w.text.c_str());
                        else std::fprintf(hx, "  %s: %lld\n", fp.first.c_str(), static_cast<long long>(w.ganz));
                    }
                }
                const std::vector<uint8_t> b = Bytes(FeldBeide(d, "Data"));
                for (size_t i = 0; i < b.size() && i < 2048; i += 16) {
                    std::fprintf(hx, "  %06zx ", i);
                    for (size_t j = 0; j < 16 && i + j < b.size(); ++j) std::fprintf(hx, " %02x", b[i + j]);
                    std::fprintf(hx, "\n");
                }
                std::fprintf(hx, "\n");
            }
        }
        std::fclose(hx);
        L("\n== A7 HEXDATEIEN: je Codec die 3 kleinsten Clips komplett in %s ==\n", hexPfad.c_str());
    }

    // Empfohlene Referenzclips fuer eine Gegenprobe mit einem fremden Exporter
    // (nur die AUSGABE als Vergleich - kein fremder Code).
    L("\n== REFERENZCLIPS fuer eine Gegenprobe (klein, mit RAW-Zwilling) ==\n");
    for (size_t i = 0; i < zw.size() && i < 6; ++i)
        L("  %s  (%s, %zu Byte, Bank %s)\n", clips[zw[i].p.komp].anzeige.c_str(), clips[zw[i].p.komp].codec.c_str(), zw[i].daten.size(),
          q.BankName(clips[zw[i].p.komp].bank).c_str());
    const double sek = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    L("\nCODECLAB fertig in %.1f s\n", sek);
    if (L.f != nullptr) std::fclose(L.f);
    return 0;
}
