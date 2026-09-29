// ============================================================
//  clipcheck.cpp - castool --clipcheck (1.42.0)
//
//  Stichprobe fuer Animationen: Clips nach Stichworten (alle muessen in
//  "Anzeige|Name|Bank" vorkommen) suchen, entpacken und die Kurven pruefen.
//  Ein Dekodierfehler zeigt sich als Sprung: bei einem Laufclip dreht sich
//  ein Bone von Bild zu Bild um wenige Grad - ein Ausreisser von 40+ Grad
//  zwischen zwei Keys ist kein Gehen mehr, sondern kaputte Daten.
//
//    castool --clipcheck <spiel> "<wort> <wort> ..." [--cache f] [--clipcache f] [--max n] [--alle]
// ============================================================
#include "fbanim.h"
#include "fbebx.h"
#include "fbgd.h"
#include "fbgame.h"
#include "fbindex.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

std::string Klein3(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

double Winkel(const float* a, const float* b) {        // Grad zwischen zwei Quaternionen (Vorzeichen egal)
    const double na = std::sqrt(double(a[0]) * a[0] + double(a[1]) * a[1] + double(a[2]) * a[2] + double(a[3]) * a[3]);
    const double nb = std::sqrt(double(b[0]) * b[0] + double(b[1]) * b[1] + double(b[2]) * b[2] + double(b[3]) * b[3]);
    if (na < 1e-9 || nb < 1e-9) return 180.0;
    double d = std::fabs((double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2] + double(a[3]) * b[3]) / (na * nb));
    if (d > 1.0) d = 1.0;
    return 2.0 * std::acos(d) * 57.29577951308232;
}

} // namespace

int ClipCheck(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::string spielordner = argv[2];
    std::vector<std::string> woerter;
    {
        const std::string m = Klein3(argv[3]);
        size_t a = 0;
        for (size_t i = 0; i <= m.size(); ++i)
            if (i == m.size() || m[i] == ' ') { if (i > a) woerter.push_back(m.substr(a, i - a)); a = i + 1; }
    }
    std::string cache, clipcache;
    size_t hoechstens = 40;
    bool alle = false;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--alle") == 0) alle = true;
        if (i + 1 >= argc) continue;
        if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
        if (std::strcmp(argv[i], "--clipcache") == 0) clipcache = argv[i + 1];
        if (std::strcmp(argv[i], "--max") == 0) hoechstens = static_cast<size_t>(std::atoi(argv[i + 1]));
    }
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund))
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbanim::Quelle q;
    if (clipcache.empty() || !q.Lade(spiel, clipcache, spiel.Kopfnummer(), grund))
        if (!q.Baue(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }

    std::map<std::string, bool> gesehen;         // gleicher Clip (Key) in vielen Baenken nur einmal
    size_t gezeigt = 0, verdaechtig = 0;
    for (const fbanim::ClipEintrag& ce : q.Clips()) {
        const std::string text = Klein3(ce.anzeige + "|" + ce.name + "|" + q.BankName(ce.bank));
        bool passt = true;
        for (const std::string& w : woerter) if (text.find(w) == std::string::npos) { passt = false; break; }
        if (!passt || gesehen[ce.key]) continue;
        gesehen[ce.key] = true;
        fbanim::Clip c;
        std::string f;
        if (!q.Entpacke(ce, c, f)) {
            std::printf("CLIP %-60s %-5s NICHT ENTPACKBAR: %s\n", (ce.anzeige.empty() ? ce.name : ce.anzeige).c_str(), ce.codec.c_str(), f.c_str());
            // 1.43.0: Namensendungen der Kanaele zaehlen (".q", ".t", sonst) -
            // die VBR-Zuordnung haengt an ihnen.
            {
                std::vector<std::string> namen;
                std::string rig;
                size_t benannt = 0;
                if (q.Kanalnamen(ce, namen, rig, benannt)) {
                    std::map<std::string, size_t> endung;
                    std::vector<std::string> beispiel;
                    for (const std::string& n : namen) {
                        const size_t pu = n.find_last_of('.');
                        const std::string e2 = pu == std::string::npos ? std::string("(ohne Punkt)") : n.substr(pu);
                        ++endung[e2];
                        if (e2 != ".q" && e2 != ".t" && beispiel.size() < 12) beispiel.push_back(n.empty() ? std::string("(leer)") : n);
                    }
                    std::printf("     Namen %zu, benannt %zu, Rig %s, Endungen:", namen.size(), benannt, rig.c_str());
                    for (const auto& kv : endung) std::printf(" %s=%zu", kv.first.c_str(), kv.second);
                    std::printf("\n     Beispiele ohne .q/.t:");
                    for (const std::string& b2 : beispiel) std::printf(" %s", b2.c_str());
                    std::printf("\n");
                }
            }
            ++verdaechtig;
            if (++gezeigt >= hoechstens) break;
            continue;
        }
        const size_t keys = c.zeiten.size();
        size_t nan = 0;
        double normFehler = 0.0;
        struct Sprung { double grad; std::string spur; size_t bei; double median; };
        std::vector<Sprung> spruenge;
        double tSprung = 0.0; std::string tSpur;
        // Zeitachse: Luecken und Rueckspruenge?
        size_t zeitFehler = 0;
        for (size_t i = 1; i < keys; ++i) if (!(c.zeiten[i] > c.zeiten[i - 1])) ++zeitFehler;
        for (const fbanim::Kanal& k : c.kanaele) {
            for (float v : k.werte) if (!std::isfinite(v)) ++nan;
            if (k.art == 'q' && k.komponenten == 4) {
                const size_t n = k.konstant ? 1 : k.werte.size() / 4;
                for (size_t i = 0; i < n; ++i) {
                    const float* x = &k.werte[i * 4];
                    const double nn = std::sqrt(double(x[0]) * x[0] + double(x[1]) * x[1] + double(x[2]) * x[2] + double(x[3]) * x[3]);
                    normFehler = std::max(normFehler, std::fabs(nn - 1.0));
                }
                if (k.konstant || n < 3) continue;
                std::vector<double> schritte;
                double groesster = 0.0; size_t bei = 0;
                for (size_t i = 1; i < n; ++i) {
                    const double w = Winkel(&k.werte[(i - 1) * 4], &k.werte[i * 4]);
                    schritte.push_back(w);
                    if (w > groesster) { groesster = w; bei = i; }
                }
                std::vector<double> s2 = schritte;
                std::sort(s2.begin(), s2.end());
                spruenge.push_back({ groesster, k.name, bei, s2[s2.size() / 2] });
            } else if (k.art == 't' && k.komponenten == 3 && !k.konstant) {
                const size_t n = k.werte.size() / 3;
                for (size_t i = 1; i < n; ++i) {
                    const double dx = k.werte[i * 3] - k.werte[(i - 1) * 3], dy = k.werte[i * 3 + 1] - k.werte[(i - 1) * 3 + 1],
                                 dz = k.werte[i * 3 + 2] - k.werte[(i - 1) * 3 + 2];
                    const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (d > tSprung) { tSprung = d; tSpur = k.name; }
                }
            }
        }
        std::sort(spruenge.begin(), spruenge.end(), [](const Sprung& a, const Sprung& b) { return a.grad > b.grad; });
        // Verdaechtig: ein Schritt von mehr als 30 Grad UND mehr als das Zehnfache des Medians dieser Spur.
        size_t ausreisser = 0;
        for (const Sprung& s : spruenge) if (s.grad > 30.0 && s.grad > 10.0 * std::max(0.5, s.median)) ++ausreisser;
        size_t unbenannt = 0;
        for (const fbanim::Kanal& k : c.kanaele) if (k.name.compare(0, 5, "kanal") == 0) ++unbenannt;
        const bool schlecht = nan > 0 || normFehler > 0.02 || ausreisser > 0 || zeitFehler > 0 || unbenannt > 0;
        if (schlecht) ++verdaechtig;
        if (schlecht || alle) {
            std::printf("CLIP %-60s %-5s additiv %d  Keys %4zu  fps %4.0f  Kanaele %3zu  NaN %zu  Norm %.4f  Zeitfehler %zu  Ausreisser %zu  max t-Schritt %.3f (%s)%s\n",
                        (ce.anzeige.empty() ? ce.name : ce.anzeige).c_str(), c.codec.c_str(), c.additiv ? 1 : 0, keys, ce.fps, c.kanaele.size(), nan,
                        normFehler, zeitFehler, ausreisser, tSprung, tSpur.c_str(), schlecht ? "  <-- VERDAECHTIG" : "");
            std::printf("     Namen: %zu von %zu Kanaelen unbenannt, benannt %zu/%zu DofIds, Rig %s (%zu Treffer), Bank %s\n", unbenannt, c.kanaele.size(),
                        c.benannt, c.dofIds, c.rig.c_str(), c.rigTreffer, q.BankName(ce.bank).c_str());
            if (unbenannt > 0) {
                // Die DofIds der Kanaele: unbenannte und zum Vergleich die ersten benannten,
                // dazu der djb2-Hash des Namens (fbebx::HashText) - ist die DofId ein Namenshash?
                fbgd::Datensatz ds, cd;
                std::string kl;
                std::vector<long long> dofs;
                if (q.Felder(ce, ds)) {
                    const fbgd::Wert* w = fbgd::Feld(ds.basis, "ChannelToDofAsset");
                    if (w != nullptr && q.LiesAsset(w->text, cd, kl))
                        if (const fbgd::Wert* d = fbgd::Feld(cd.felder, "DofIds")) for (const fbgd::Wert& x : d->werte) dofs.push_back(x.ganz);
                }
                std::vector<std::string> namen; std::string rig; size_t ben = 0;
                q.Kanalnamen(ce, namen, rig, ben);
                size_t gezeigtN = 0;
                for (size_t k = 0; k < dofs.size() && k < namen.size(); ++k) {
                    const bool leer = namen[k].empty();
                    if (!leer && gezeigtN >= 6) continue;
                    if (!leer) ++gezeigtN;
                    std::string basis = namen[k];
                    if (basis.size() > 2 && basis[basis.size() - 2] == '.') basis.resize(basis.size() - 2);
                    std::printf("     DofId[%3zu] = %12lld (0x%08llx)  %-26s Hash(Name) 0x%08x  Hash(Name.q) 0x%08x\n", k, dofs[k],
                                static_cast<unsigned long long>(dofs[k]) & 0xFFFFFFFFull, leer ? "(unbenannt)" : namen[k].c_str(),
                                static_cast<uint32_t>(fbebx::HashText(basis)), static_cast<uint32_t>(fbebx::HashText(namen[k])));
                }
            }
            for (size_t i = 0; i < spruenge.size() && i < 3; ++i)
                std::printf("     Sprung %6.1f Grad bei Key %4zu in %-28s (Median dieser Spur %.2f Grad)\n", spruenge[i].grad, spruenge[i].bei,
                            spruenge[i].spur.c_str(), spruenge[i].median);
        }
        if (++gezeigt >= hoechstens) break;
    }
    std::printf("CLIPCHECK \"%s\": %zu Clips geprueft, %zu verdaechtig\n", argv[3], gezeigt, verdaechtig);
    return 0;
}

// castool --suchewert <spiel> <zahl> [<zahl> ...] [--cache f] (1.42.0)
// Sucht 32-Bit-Werte (little endian) roh in allen AssetBanks und nennt Bank und Versatz.
int SucheWert(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::string spielordner = argv[2];
    std::string cache;
    std::vector<uint32_t> werte;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cache") == 0 && i + 1 < argc) { cache = argv[++i]; continue; }
        werte.push_back(static_cast<uint32_t>(std::strtoull(argv[i], nullptr, 0)));
    }
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund))
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    size_t banken = 0, treffer = 0;
    for (const auto& kv : idx.res) {
        if (kv.second.resType != 0x51A3C853u) continue;
        std::vector<uint8_t> roh;
        std::string f;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f)) continue;
        ++banken;
        for (size_t p = 0; p + 4 <= roh.size(); ++p) {
            const uint32_t v = static_cast<uint32_t>(roh[p]) | (static_cast<uint32_t>(roh[p + 1]) << 8) |
                               (static_cast<uint32_t>(roh[p + 2]) << 16) | (static_cast<uint32_t>(roh[p + 3]) << 24);
            for (uint32_t w : werte) if (v == w) {
                ++treffer;
                // In welchem Eintrag der Bank?
                static std::map<std::string, fbgd::Bank> banken2;
                auto it = banken2.find(kv.first);
                if (it == banken2.end()) { fbgd::Bank bank; std::string f2; fbgd::LiesBank(roh, bank, f2); it = banken2.emplace(kv.first, bank).first; }
                std::string klasse = "?";
                for (const fbgd::Eintrag& e : it->second.eintraege) if (p >= e.pos && p < e.pos + e.groesse) { klasse = e.klassenName; break; }
                if (treffer <= 80) std::printf("WERT 0x%08x in %s bei %zu, Eintrag %s\n", w, kv.first.c_str(), p, klasse.c_str());
            }
        }
    }
    std::printf("SUCHEWERT %zu Banken, %zu Treffer\n", banken, treffer);
    return 0;
}

// castool --skeletttest <spiel> <name> [<name> ...] [--cache f] (1.42.0)
// Welche Ruhelage bekaeme eine Figur? Fahrzeugregel (fbfahrzeug::SucheSkelett)
// gegen die Figurenregel des Animationsfensters (characters/..._ske ohne _1p).
#include "fbauswahl.h"
#include "fbfahrzeug.h"
int SkelettTest(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::string spielordner = argv[2];
    std::string cache;
    std::vector<std::string> namen;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cache") == 0 && i + 1 < argc) { cache = argv[++i]; continue; }
        namen.push_back(Klein3(argv[i]));
    }
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund))
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    // 1.43.0: "--alle" - fuer jede Figur der Liste die Skelettwahl des Imports
    // (fbauswahl::SkelettFuer) und woher sie kommt; so ist zu sehen, welche
    // Figuren eine neue Regel trifft.
    if (!namen.empty() && namen[0] == "--alle") {
        std::map<std::string, size_t> jeHerkunft;
        for (const fbauswahl::Figur& fi : fbauswahl::Figuren(idx)) {
            if (fi.erstePerson) continue;
            std::string herkunft;
            const std::string sk = fbauswahl::SkelettFuer(idx, fi.name, herkunft);
            const std::string art = herkunft.substr(0, herkunft.find(" ("));
            ++jeHerkunft[art];
            if (herkunft.find("Figurmeshes") != std::string::npos)
                std::printf("STUFE3 %-90s -> %s\n", fi.name.c_str(), sk.c_str());
        }
        for (const auto& kv : jeHerkunft) std::printf("HERKUNFT %-60s %zu\n", kv.first.c_str(), kv.second);
        return 0;
    }
    for (const std::string& figur : namen) {
        const std::string fz = fbfahrzeug::SucheSkelett(spiel, idx, figur);
        std::string ch;
        for (const auto& kv : idx.ebx) {
            const std::string n = Klein3(kv.first);
            if (n.compare(0, 11, "characters/") != 0 || n.size() < 5 || n.compare(n.size() - 4, 4, "_ske") != 0) continue;
            if (n.find("_1p") != std::string::npos) continue;
            if (n.find("/" + figur + "/") == std::string::npos) continue;   // wie RuheFuer seit 1.42.0
            ch = kv.first;
            break;
        }
        std::printf("SKELETT %-12s Fahrzeugregel: %-70s Figurenregel: %s\n", figur.c_str(), fz.empty() ? "-" : fz.c_str(), ch.empty() ? "- (walrus)" : ch.c_str());
    }
    return 0;
}
