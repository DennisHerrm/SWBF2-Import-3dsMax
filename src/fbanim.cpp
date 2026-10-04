// ============================================================
//  fbanim.cpp - siehe fbanim.h
// ============================================================
#include "fbanim.h"

#include <array>
#include <cmath>
#include "fbdatei.h"

#include <algorithm>
#include <cstring>

namespace fbanim {

namespace {

bool EndetAuf(const std::string& s, const char* x) {
    const size_t l = std::strlen(x);
    return s.size() >= l && s.compare(s.size() - l, l, x) == 0;
}

long long Ganz(const fbgd::Felder& f, const char* n, long long vorgabe = 0) {
    const fbgd::Wert* w = fbgd::Feld(f, n);
    return (w != nullptr && (w->art == fbgd::Wert::Art::Ganz || w->art == fbgd::Wert::Art::Bool)) ? w->ganz : vorgabe;
}

std::vector<float> Floats(const fbgd::Felder& f, const char* n) {
    std::vector<float> v;
    if (const fbgd::Wert* w = fbgd::Feld(f, n)) for (const fbgd::Wert& x : w->werte) v.push_back(x.gleit);
    return v;
}

std::vector<long long> Ganze(const fbgd::Felder& f, const char* n) {
    std::vector<long long> v;
    if (const fbgd::Wert* w = fbgd::Feld(f, n)) for (const fbgd::Wert& x : w->werte) v.push_back(x.ganz);
    return v;
}

// fb_anim._eindeutig: zwei Kanaele duerfen nicht denselben Namen tragen.
std::string Eindeutig(const std::set<std::string>& da, const std::string& n) {
    if (!da.count(n)) return n;
    for (int i = 2;; ++i) {
        const std::string k = n + "#" + std::to_string(i);
        if (!da.count(k)) return k;
    }
}

} // namespace

const fbgd::Eintrag* Quelle::Satz(const Ort& o, fbgd::Datensatz& ds) {
    auto it = cache_.find(o.bank);
    if (it == cache_.end()) {
        if (cache_.size() > 32) cache_.clear();
        std::pair<std::vector<uint8_t>, fbgd::Bank> neu;
        std::string f;
        if (!spiel_->HoleNachSha1(bankSha_[o.bank], neu.first, f) || !fbgd::LiesBank(neu.first, neu.second, f)) return nullptr;
        it = cache_.emplace(o.bank, std::move(neu)).first;
    }
    if (o.eintrag >= it->second.second.eintraege.size()) return nullptr;
    const fbgd::Eintrag& e = it->second.second.eintraege[o.eintrag];
    fbgd::LiesDatensatz(it->second.first, it->second.second, e, ds, 0);
    return &e;
}

bool Quelle::Baue(fbgame::Spiel& spiel, const fbindex::Index& idx, std::string& fehler) {
    spiel_ = &spiel;
    for (const auto& kv : idx.res) {
        if (kv.second.resType != 0x51A3C853u) continue;
        std::vector<uint8_t> roh;
        std::string f;
        fbgd::Bank b;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f) || !fbgd::LiesBank(roh, b, f)) continue;
        const size_t bi = bankName_.size();
        bankName_.push_back(kv.first);
        bankSha_.push_back(kv.second.sha1);
        for (size_t ei = 0; ei < b.eintraege.size(); ++ei) {
            const fbgd::Eintrag& e = b.eintraege[ei];
            fbgd::Datensatz ds;
            fbgd::LiesDatensatz(roh, b, e, ds, 1);
            if (const fbgd::Wert* k = fbgd::Feld(ds.felder, "__key")) nachKey_[k->text] = Ort{ bi, ei };
            if (e.klassenName == "RigAsset") rigs_.push_back(Ort{ bi, ei });
            if (e.klassenName == "ClipControllerAsset") {
                const fbgd::Wert* anim = fbgd::Feld(ds.felder, "Anim");
                const fbgd::Wert* fps = fbgd::Feld(ds.felder, "FPS");
                if (anim != nullptr && !anim->text.empty()) {
                    auto& z = controller_[anim->text];
                    if (z.first.empty() && ds.nameDa) z.first = ds.name;
                    if (z.second == 0.0f && fps != nullptr && fps->art == fbgd::Wert::Art::Gleit) z.second = fps->gleit;
                    ++controllerZahl_[anim->text];
                }
            }
            if (EndetAuf(e.klassenName, "AnimationAsset")) {
                ClipEintrag c;
                c.bank = bi;
                c.eintrag = ei;
                c.name = ds.nameDa ? ds.name : std::string();
                if (const fbgd::Wert* k = fbgd::Feld(ds.felder, "__key")) c.key = k->text;
                c.klasse = e.klassenName;
                if (const fbgd::Wert* w = fbgd::Feld(ds.basis, "CodecType")) c.codec = fbgd::Codec(w->ganz);
                c.endFrame = static_cast<int32_t>(Ganz(ds.basis, "EndFrame", -1));
                clips_.push_back(c);
            }
        }
    }
    if (bankName_.empty()) { fehler = "keine lesbare AssetBank"; return false; }
    for (ClipEintrag& c : clips_) {
        const auto it = controller_.find(c.key);
        if (it == controller_.end()) continue;
        c.anzeige = it->second.first;
        c.fps = it->second.second;
        c.controller = controllerZahl_[c.key];
    }
    controller_.clear();
    controllerZahl_.clear();
    return true;
}

void Quelle::SlotsVon(const std::string& key, int tiefe, std::set<std::string>& gesehen, std::vector<std::string>& namen) {
    if (tiefe > 6 || gesehen.count(key)) return;
    gesehen.insert(key);
    const auto it = nachKey_.find(key);
    if (it == nachKey_.end()) return;
    fbgd::Datensatz ds;
    const fbgd::Eintrag* e = Satz(it->second, ds);
    if (e == nullptr) return;
    if (e->klassenName == "LayoutAsset") {
        if (const fbgd::Wert* sl = fbgd::Feld(ds.felder, "Slots")) {
            for (const auto& satz : sl->saetze) {
                std::string n;
                for (const auto& p : satz) if (p.first == "Name" && p.second.textDa) n = p.second.text;
                namen.push_back(n);
            }
        }
    }
    for (const char* feld : { "LayoutAssets", "Children" }) {
        if (const fbgd::Wert* w = fbgd::Feld(ds.felder, feld)) {
            for (const fbgd::Wert& k : w->werte) SlotsVon(k.text, tiefe + 1, gesehen, namen);
        }
    }
}

const std::vector<long long>& Quelle::RigDofs(size_t r) {
    auto it = rigDofs_.find(r);
    if (it != rigDofs_.end()) return it->second;
    fbgd::Datensatz ds;
    std::vector<long long> v;
    if (Satz(rigs_[r], ds) != nullptr) v = Ganze(ds.felder, "DofIds");
    return rigDofs_.emplace(r, std::move(v)).first->second;
}

const std::pair<std::vector<std::string>, std::string>& Quelle::RigNamen(size_t r) {
    auto it = rigNamen_.find(r);
    if (it != rigNamen_.end()) return it->second;
    std::pair<std::vector<std::string>, std::string> aus;
    fbgd::Datensatz ds;
    if (Satz(rigs_[r], ds) != nullptr) {
        const std::vector<long long> dofs = Ganze(ds.felder, "DofIds");
        aus.first.assign(dofs.size(), std::string());
        if (const fbgd::Wert* k = fbgd::Feld(ds.felder, "__key")) aus.second = k->text;
        const std::vector<long long> starts = Ganze(ds.felder, "DofSetIdIndices");
        const fbgd::Wert* sets = fbgd::Feld(ds.felder, "RigDofSets");
        if (sets != nullptr) {
            for (size_t k = 0; k < sets->werte.size() && k < starts.size(); ++k) {
                std::vector<std::string> namen;
                std::set<std::string> gesehen;
                SlotsVon(sets->werte[k].text, 0, gesehen, namen);
                for (size_t i = 0; i < namen.size(); ++i) {
                    const long long pl = starts[k] + static_cast<long long>(i);
                    if (pl >= 0 && static_cast<size_t>(pl) < aus.first.size()) aus.first[static_cast<size_t>(pl)] = namen[i];
                }
            }
        }
    }
    return rigNamen_.emplace(r, std::move(aus)).first->second;
}

// ============================================================
//  VBR und DCT (0.65.0) - eigene Umsetzung.
//
//  Die Formatkenntnis stammt aus drei Quellen: unserem Codec-Labor (Aufbau
//  des VBR-Stroms, Palette, Lauflaengen-Karte, DCT-Eintraege je Kanal - an
//  allen Clips gemessen) und dem Lesen des AssetBankPlugin im FrostyToolsuite-
//  Fork von Virjoinga (CC BY-NC-ND 4.0) fuer die Teile, die sich ohne
//  Vergleichswerte nicht erschliessen liessen: VBR-Bloecke werden von hinten
//  gelesen (Bytes rueckwaerts, Bits je Byte vom niedrigsten an), je Koeffizient
//  ein Anwesenheits- und ein Vorzeichenbit; die drei Kopfbytes sind
//  Quantisierer fuer Drehung/Trajektorie/Verschiebung; DCT-Bloecke lesen im
//  ersten Block den Koeffizienten 0 nicht (er ist der Grundwert), Breite 15
//  bedeutet CatchAllBitCount. Kein Code uebernommen - nur diese Fakten.
//  1.44.0: DCT-Basis fuer i > 0 ist 0,5*cos (nicht 0,25*cos), Koeffizient 0
//  plus Grundwert laeuft wie im Spiel in 16 Bit ueber (Quelle wie oben).
// ============================================================
namespace {

double Zahl1(const fbgd::Felder& f, const char* n, double vorgabe) {
    const fbgd::Wert* w = fbgd::Feld(f, n);
    if (w == nullptr) return vorgabe;
    if (w->art == fbgd::Wert::Art::Gleit) return w->gleit;
    if (w->art == fbgd::Wert::Art::Ganz || w->art == fbgd::Wert::Art::Bool) return static_cast<double>(w->ganz);
    return vorgabe;
}

std::vector<uint8_t> Bytes1(const fbgd::Felder& f, const char* n) {
    std::vector<uint8_t> b;
    const fbgd::Wert* w = fbgd::Feld(f, n);
    if (w == nullptr || w->art != fbgd::Wert::Art::Feld) return b;
    for (const fbgd::Wert& x : w->werte) b.push_back(static_cast<uint8_t>(x.ganz & 0xFF));
    return b;
}

// Basisfunktion der Blocktransformation (8 Keys): Gewicht von Koeffizient i
// fuer Key t. dc = Gewicht des Koeffizienten 0 (VBR 1/8, DCT 1/4), ac = Gewicht
// der uebrigen (VBR 1/4, DCT 1/2).
// 1.44.0: DCT mit ac 1/4 halbierte die Bewegung innerhalb jedes 8er-Blocks;
// am naechsten Block sprang die Kurve auf dessen Grundwert (Vader Run_Bwd:
// Becken springt alle 8 Bilder, 70 von 87 DCT-Clips Vaders betroffen).
double Basis(int t, int i, double dc, double ac = 0.25) {
    if (i == 0) return dc;
    return ac * std::cos(3.14159265358979323846 * (2.0 * t + 1.0) * i / 16.0);
}

char ArtAusName(const std::string& n) {
    if (n.size() > 2 && n.compare(n.size() - 2, 2, ".q") == 0) return 'q';
    if (n.size() > 2 && n.compare(n.size() - 2, 2, ".t") == 0) return 't';
    // 1.43.0: Skalierung ist ein Vector3 wie die Verschiebung. Grievous traegt
    // 315 ".s"-Kanaele; als Float gezaehlt passte keiner seiner VBR-Clips
    // (Clip t 698 / f 8, Namen t 383 / f 323). Wie beim RAW-Decoder bleibt die
    // Art 't' und der Name ".s" - die Max-Seite erkennt Skalierung am Namen.
    if (n.size() > 2 && n.compare(n.size() - 2, 2, ".s") == 0) return 't';
    return 'f';
}

// VBR-Blockleser: vom Ende des Blocks rueckwaerts, je Byte vom niedrigsten Bit an.
struct RueckLeser {
    const uint8_t* d = nullptr;
    size_t n = 0, bit = 0;
    bool ueber = false;
    int Bit() {
        if (bit >= n * 8) { ueber = true; return 0; }
        const size_t byte = n - 1 - (bit >> 3);
        const int b = (d[byte] >> (bit & 7)) & 1;
        ++bit;
        return b;
    }
    uint32_t Wert(int w) { uint32_t v = 0; for (int i = 0; i < w; ++i) v |= static_cast<uint32_t>(Bit()) << i; return v; }
};

bool VbrLesen(const fbgd::Datensatz& ds, const std::vector<std::string>& namen, Clip& aus, std::string& fehler) {
    const fbgd::Felder& f = ds.felder;
    const int aq = static_cast<int>(Zahl1(f, "QuaternionCount", 0)), av = static_cast<int>(Zahl1(f, "Vector3Count", 0));
    const int af = static_cast<int>(std::max(Zahl1(f, "FloatCount", 0), Zahl1(f, "NumFloat", 0)));
    const int cq = static_cast<int>(Zahl1(f, "ConstQuaternionCount", 0)), cv = static_cast<int>(Zahl1(f, "ConstVector3Count", 0));
    const int cf = static_cast<int>(Zahl1(f, "ConstFloatCount", 0));
    const int keys = static_cast<int>(Zahl1(f, "NumKeys", 0));
    const size_t kt = static_cast<size_t>(Zahl1(f, "KeyTimeSize", 0)), karte = static_cast<size_t>(Zahl1(f, "ConstChanMapSize", 0));
    const size_t palGroesse = static_cast<size_t>(Zahl1(f, "ConstPaletteSize", 0));
    const size_t vo = static_cast<size_t>(Zahl1(f, "VectorOffsetSize", 0)), fo = static_cast<size_t>(Zahl1(f, "FloatOffsetSize", 0));
    const std::vector<uint8_t> d = Bytes1(f, "Data");
    const std::vector<float> palette = Floats(f, "ConstantPalette");
    const std::vector<long long> bloecke = Ganze(f, "FrameBlockSizes");
    const int anim = 4 * aq + 3 * av + af, konst = 4 * cq + 3 * cv + cf;
    if (keys < 1) { fehler = "VBR ohne Keys"; return false; }
    // Diagnose (1.43.0): Rohdaten schreiben, wenn SWBF2_VBR_ROH auf eine Datei zeigt.
    if (const char* roh = std::getenv("SWBF2_VBR_ROH")) {
        if (std::FILE* rf = std::fopen(roh, "wb")) {
            std::fprintf(rf, "aq %d av %d af %d cq %d cv %d cf %d keys %d kt %zu karte %zu pal %zu vo %zu fo %zu bloecke %zu\n",
                         aq, av, af, cq, cv, cf, keys, kt, karte, palGroesse, vo, fo, bloecke.size());
            std::string felder;                                   // alle Felder: Name=Wert, Listen mit Laenge
            for (const auto& fw : f) {
                const fbgd::Wert& w = fw.second;
                char z[96];
                if (w.art == fbgd::Wert::Art::Gleit) std::snprintf(z, sizeof z, "%g", static_cast<double>(w.gleit));
                else if (w.art == fbgd::Wert::Art::Feld) std::snprintf(z, sizeof z, "[%zu]", w.werte.size());
                else if (w.art == fbgd::Wert::Art::Hex || w.art == fbgd::Wert::Art::Text) std::snprintf(z, sizeof z, "%s", w.text.substr(0, 40).c_str());
                else std::snprintf(z, sizeof z, "%lld", static_cast<long long>(w.ganz));
                felder += fw.first + "=" + z + " ";
            }
            std::fprintf(rf, "FELDER %s\n", felder.c_str());
            std::fwrite(d.data(), 1, d.size(), rf);
            std::fclose(rf);
        }
    }
    size_t summeBloecke = 0;
    for (long long x : bloecke) summeBloecke += static_cast<size_t>(x);
    const size_t schritt = palGroesse > 256 ? 2 : 1;
    const size_t kopf = kt + static_cast<size_t>(konst) * schritt + karte + static_cast<size_t>(anim) * 4 + vo + fo;
    if (kopf + summeBloecke != d.size()) { fehler = "VBR-Aufteilung passt nicht (" + std::to_string(kopf + summeBloecke) + " statt " + std::to_string(d.size()) + " Byte)"; return false; }
    if (static_cast<int>(bloecke.size()) < (keys + 7) / 8 && anim > 0) { fehler = "VBR: zu wenige Bloecke"; return false; }
    // Keyzeiten: Abstaende in Bildern (fehlen sie, liegt jeder Key auf einem Bild)
    size_t p = 0;
    aus.zeiten.clear();
    {
        double t = 0;
        aus.zeiten.push_back(0.0f);
        for (size_t i = 0; i < kt && static_cast<int>(aus.zeiten.size()) < keys; ++i) { t += d[p + i]; aus.zeiten.push_back(static_cast<float>(t)); }
        while (static_cast<int>(aus.zeiten.size()) < keys) aus.zeiten.push_back(aus.zeiten.back() + 1.0f);
    }
    p += kt;
    std::vector<size_t> palIdx(static_cast<size_t>(konst));
    for (int i = 0; i < konst; ++i) palIdx[static_cast<size_t>(i)] = schritt == 1 ? d[p + static_cast<size_t>(i)] : (d[p + 2 * static_cast<size_t>(i)] | (d[p + 2 * static_cast<size_t>(i) + 1] << 8));
    // 1.43.0: Ein Index hinter der Palette heisst, der Bereich ist anders aufgebaut
    // als hier gelesen (gemessen: nur Clips mit Palette > 256 UND Versatzkurven,
    // dazu 0f151470b16c6ca8 ab Index 943). Bisher wurde der Wert still zu 0 - der
    // Clip "ging", sah aber falsch aus. Jetzt ehrlich: nicht entpackbar.
    {
        size_t draussen = 0, erster = 0;
        for (size_t i = 0; i < palIdx.size(); ++i)
            if (palIdx[i] >= palette.size()) { if (draussen++ == 0) erster = i; }
        if (draussen > 0) {
            fehler = "VBR: " + std::to_string(draussen) + " von " + std::to_string(palIdx.size()) + " Paletten-Indizes hinter der Palette (" +
                     std::to_string(palette.size()) + " Werte, erster bei " + std::to_string(erster) + ") - Aufbau mit grosser Palette noch nicht bekannt";
            return false;
        }
    }
    p += static_cast<size_t>(konst) * schritt;
    const size_t kartenAb = p;
    p += karte;
    const size_t deskAb = p;
    p += static_cast<size_t>(anim) * 4;
    const size_t offsetAb = p;
    p += vo + fo;
    // Bereiche
    const double qMin = Zahl1(f, "QuatMin", -1), qMax = Zahl1(f, "QuatMax", 1);
    const double tMin = Zahl1(f, "TrajMin", 0), tMax = Zahl1(f, "TrajMax", 0);
    const double vMin = Zahl1(f, "Vec3Min", 0), vMax = Zahl1(f, "Vec3Max", 0);
    const double flMin = Zahl1(f, "FloatMin", 0), flMax = Zahl1(f, "FloatMax", 0);
    double dct = Zahl1(f, "Dct", 4);
    if (dct <= 0) dct = 4;
    // Animierte Komponenten: Koeffizienten je Block
    const int nb = (keys + 7) / 8;
    std::vector<double> werte(static_cast<size_t>(keys) * static_cast<size_t>(anim), 0.0);
    if (anim > 0) {
        std::vector<std::array<int, 8>> breite(static_cast<size_t>(anim));
        for (int c = 0; c < anim; ++c)
            for (int j = 0; j < 8; ++j) { const uint8_t x = d[deskAb + static_cast<size_t>(c) * 4 + static_cast<size_t>(j / 2)]; breite[static_cast<size_t>(c)][static_cast<size_t>(j)] = (j % 2 == 0) ? (x & 15) : (x >> 4); }
        std::vector<std::array<double, 8>> koef(static_cast<size_t>(anim) * static_cast<size_t>(nb));
        std::vector<std::array<double, 3>> quant(static_cast<size_t>(nb));
        size_t ab = p;
        for (int b = 0; b < nb; ++b) {
            const size_t g = static_cast<size_t>(bloecke[static_cast<size_t>(b)]);
            if (g < 3 || ab + g > d.size()) { fehler = "VBR-Block " + std::to_string(b) + " kaputt"; return false; }
            for (int k = 0; k < 3; ++k) quant[static_cast<size_t>(b)][static_cast<size_t>(k)] = d[ab + static_cast<size_t>(k)];
            RueckLeser r;
            r.d = d.data() + ab + 3;
            r.n = g - 3;
            for (int c = 0; c < anim; ++c) {
                const auto& w = breite[static_cast<size_t>(c)];
                int da[8] = {}, vz[8] = {};
                for (int j = 0; j < 8; ++j) if (w[static_cast<size_t>(j)] > 0) da[j] = r.Bit();
                for (int j = 0; j < 8; ++j) if (da[j]) vz[j] = r.Bit();
                auto& k2 = koef[static_cast<size_t>(b) * static_cast<size_t>(anim) + static_cast<size_t>(c)];
                for (int j = 0; j < 8; ++j) {
                    k2[static_cast<size_t>(j)] = 0.0;
                    if (!da[j]) continue;
                    const double v = static_cast<double>(r.Wert(w[static_cast<size_t>(j)]));
                    k2[static_cast<size_t>(j)] = vz[j] ? v : -v;
                }
            }
            if (r.ueber) { fehler = "VBR-Block " + std::to_string(b) + " zu kurz"; return false; }
            ab += g;
        }
        const int rest = keys % 8, letzterStart = keys - rest;
        for (int key = 0; key < keys; ++key) {
            const int b = key / 8;
            const int t = (key >= letzterStart && key > 7) ? key - (keys - 8) : key % 8;
            for (int c = 0; c < anim; ++c) {
                int art = 2;                                             // 0 Drehung, 1 Trajektorie, 2 Verschiebung/Float
                double lo = flMin, spanne = flMax - flMin;
                if (c < 4 * aq) { art = 0; lo = qMin; spanne = qMax - qMin; }
                else if (av > 0 && c < 4 * aq + 3) { art = 1; lo = tMin; spanne = tMax - tMin; }
                else if (c < 4 * aq + 3 * av) { art = 2; lo = vMin; spanne = vMax - vMin; }
                const double skala = (quant[static_cast<size_t>(b)][static_cast<size_t>(art)] + 1.0) * 0.2 * dct;
                const auto& k2 = koef[static_cast<size_t>(b) * static_cast<size_t>(anim) + static_cast<size_t>(c)];
                double s = 0.5;
                for (int i = 0; i < 8; ++i) s += k2[static_cast<size_t>(i)] * Basis(t, i, 0.125) * ((skala * std::log(i + 2.0) + 1.0) * dct / 32768.0);
                werte[static_cast<size_t>(key) * static_cast<size_t>(anim) + static_cast<size_t>(c)] = s * spanne + lo;
            }
        }
        // Versatzkurven (Vektoren, Floats): stueckweise lineare Zuschlaege je 8 Keys
        auto kurven = [&](size_t ab2, size_t laenge, double roh, int startKanal) {
            if (laenge == 0 || roh == 0.0) return;
            const double sk = roh / 127.0;
            size_t q = ab2;
            const size_t ende = ab2 + laenge;
            auto byte = [&]() -> int { return q < ende ? d[q++] : 0; };
            const int anzahl = byte();
            int kanal = startKanal;
            const int vecAb = 4 * aq, floatAb = vecAb + 4 * av;
            for (int i = 0; i < anzahl && q < ende; ++i) {
                kanal += byte();
                const int punkte = byte();
                if (punkte == 0) { ++kanal; continue; }
                int ziel = -1;
                if (kanal >= floatAb) ziel = 4 * aq + 3 * av + (kanal - floatAb);
                else if (kanal >= vecAb && (kanal - vecAb) % 4 < 3) ziel = 4 * aq + ((kanal - vecAb) / 4) * 3 + (kanal - vecAb) % 4;
                double y = static_cast<int8_t>(byte()) * sk;
                int key0 = 0;
                for (int pt = 0; pt < punkte - 1 && q < ende; ++pt) {
                    const int spanne2 = byte() * 8;
                    const double y2 = static_cast<int8_t>(byte()) * sk;
                    for (int k = 0; k < spanne2 && key0 + k < keys; ++k)
                        if (ziel >= 0 && ziel < anim) werte[static_cast<size_t>(key0 + k) * static_cast<size_t>(anim) + static_cast<size_t>(ziel)] += y + (y2 - y) * k / spanne2;
                    key0 += spanne2;
                    y = y2;
                }
                if (ziel >= 0 && ziel < anim) for (int k = key0; k < keys; ++k) werte[static_cast<size_t>(k) * static_cast<size_t>(anim) + static_cast<size_t>(ziel)] += y;
            }
        };
        kurven(offsetAb, vo, Zahl1(f, "VectorOffsetScale", 0), 4 * aq);
        kurven(offsetAb + vo, fo, Zahl1(f, "FloatOffsetScale", 0), 4 * aq + 4 * av);
    }
    // Kanaele in DOF-Reihenfolge: Karte = abwechselnd animiert/konstant (beginnt animiert)
    const size_t kanaele = static_cast<size_t>(aq + av + af + cq + cv + cf);
    std::vector<char> istKonst(kanaele, 0);
    {
        size_t k = 0;
        for (size_t j = 0; j < karte; ++j)
            for (int r2 = 0; r2 < d[kartenAb + j] && k < kanaele; ++r2, ++k) istKonst[k] = (j % 2 == 1) ? 1 : 0;
    }
    // Art je DOF: aus den Namen (.q/.t). Passen die Namen nicht zu den Zaehlern
    // (Clip ohne Rig-Namen: dann heissen alle "kanalN"), gilt die Reihenfolge
    // Drehungen, Vektoren, Floats. 0.65.1: vorher ueberlief hier der Wertepuffer
    // (Absturz des Labors in Runde 16) - jetzt vor jedem Zugriff geprueft.
    std::vector<char> arten(kanaele, 'f');
    {
        size_t zq[2] = {}, zt[2] = {}, zf[2] = {};
        for (size_t k = 0; k < kanaele; ++k) {
            const char a2 = k < namen.size() ? ArtAusName(namen[k]) : 'f';
            arten[k] = a2;
            const int ko = istKonst[k] ? 1 : 0;
            if (a2 == 'q') ++zq[ko]; else if (a2 == 't') ++zt[ko]; else ++zf[ko];
        }
        const bool passt = zq[0] == static_cast<size_t>(aq) && zq[1] == static_cast<size_t>(cq) && zt[0] == static_cast<size_t>(av) &&
                           zt[1] == static_cast<size_t>(cv) && zf[0] == static_cast<size_t>(af) && zf[1] == static_cast<size_t>(cf);
        if (!passt) {
            for (size_t k = 0; k < kanaele; ++k)
                arten[k] = k < static_cast<size_t>(aq + cq) ? 'q' : (k < static_cast<size_t>(aq + cq + av + cv) ? 't' : 'f');
            size_t yq[2] = {}, yt[2] = {}, yf[2] = {};
            for (size_t k = 0; k < kanaele; ++k) {
                const int ko = istKonst[k] ? 1 : 0;
                if (arten[k] == 'q') ++yq[ko]; else if (arten[k] == 't') ++yt[ko]; else ++yf[ko];
            }
            if (yq[0] != static_cast<size_t>(aq) || yq[1] != static_cast<size_t>(cq) || yt[0] != static_cast<size_t>(av) ||
                yt[1] != static_cast<size_t>(cv) || yf[0] != static_cast<size_t>(af) || yf[1] != static_cast<size_t>(cf)) {
                // 1.43.0: mit den gemessenen Zahlen (animiert/konstant je Art)
                char t[300];
                std::snprintf(t, sizeof t, "VBR: Kanalarten nicht bestimmbar (Namen fehlen oder passen nicht) - Clip q %d/%d t %d/%d f %d/%d, "
                              "Namen q %zu/%zu t %zu/%zu f %zu/%zu, Kanaele %zu, Namen %zu, Karte %zu Byte",
                              aq, cq, av, cv, af, cf, zq[0], zq[1], zt[0], zt[1], zf[0], zf[1], kanaele, namen.size(), karte);
                fehler = t;
                fehler += " | Daten " + std::to_string(d.size()) + ": Keyzeiten " + std::to_string(kt) + ", konst " + std::to_string(konst) + " x " +
                          std::to_string(schritt) + " (Palette " + std::to_string(palGroesse) + "), Karte " + std::to_string(karte) + ", anim " +
                          std::to_string(anim) + " x 4, Versatz " + std::to_string(vo) + "+" + std::to_string(fo) + ", Bloecke " + std::to_string(summeBloecke);
                return false;
            }
        }
    }
    int naq = 0, nav = 0, naf = 0, ncq = 0, ncv = 0, ncf = 0;
    std::set<std::string> vergeben;
    for (size_t k = 0; k < kanaele; ++k) {
        std::string n = (k < namen.size() && !namen[k].empty()) ? namen[k] : "kanal" + std::to_string(k);
        char art = arten[k];
        Kanal kn;
        kn.art = art;
        kn.komponenten = art == 'q' ? 4 : (art == 't' ? 3 : 1);
        kn.konstant = istKonst[k] != 0;
        if (kn.konstant) {
            size_t tab = 0;
            double lo = flMin, sp = flMax - flMin;
            if (art == 'q') { tab = static_cast<size_t>(ncq++) * 4; lo = qMin; sp = qMax - qMin; }
            else if (art == 't') { tab = static_cast<size_t>(4 * cq) + static_cast<size_t>(ncv++) * 3; lo = vMin; sp = vMax - vMin; }
            else tab = static_cast<size_t>(4 * cq + 3 * cv) + static_cast<size_t>(ncf++);
            for (int c = 0; c < kn.komponenten; ++c) {
                const size_t ix = tab + static_cast<size_t>(c) < palIdx.size() ? palIdx[tab + static_cast<size_t>(c)] : 0;
                const double pv = ix < palette.size() ? palette[ix] : 0.0;
                kn.werte.push_back(static_cast<float>(lo + pv * sp));
            }
        } else {
            int basis = 0;
            if (art == 'q') basis = 4 * naq++;
            else if (art == 't') basis = 4 * aq + 3 * nav++;
            else basis = 4 * aq + 3 * av + naf++;
            if (basis + kn.komponenten > anim) { fehler = "VBR: Kanalzuordnung ueberlaeuft"; return false; }
            for (int key = 0; key < keys; ++key)
                for (int c = 0; c < kn.komponenten; ++c)
                    kn.werte.push_back(static_cast<float>(werte[static_cast<size_t>(key) * static_cast<size_t>(anim) + static_cast<size_t>(basis + c)]));
        }
        n = Eindeutig(vergeben, n);
        vergeben.insert(n);
        kn.name = n;
        aus.kanaele.push_back(std::move(kn));
    }
    if (naq != aq || nav != av || naf != af || ncq != cq || ncv != cv || ncf != cf) {
        fehler = "VBR: Kanalarten aus den Namen passen nicht zu den Zaehlern";
        return false;
    }
    return true;
}

// DCT-Leser: Bits vom hoechsten an, Byte fuer Byte in Stromreihenfolge.
struct VorLeser {
    const std::vector<uint8_t>* d = nullptr;
    size_t bit = 0;
    bool ueber = false;
    int64_t Vz(int w) {
        if (w <= 0) return 0;
        uint64_t v = 0;
        for (int i = 0; i < w; ++i) {
            const size_t byte = bit >> 3;
            if (byte >= d->size()) { ueber = true; return 0; }
            v = (v << 1) | static_cast<uint64_t>(((*d)[byte] >> (7 - (bit & 7))) & 1u);
            ++bit;
        }
        if (v & (uint64_t(1) << (w - 1))) return static_cast<int64_t>(v) - (int64_t(1) << w);
        return static_cast<int64_t>(v);
    }
};

bool DctLesen(const fbgd::Datensatz& ds, const std::vector<std::string>& namen, Clip& aus, std::string& fehler) {
    const fbgd::Felder& f = ds.felder;
    const int keys = static_cast<int>(Zahl1(f, "NumKeys", 0));
    const int nq = static_cast<int>(Zahl1(f, "NumQuats", 0)), nv = static_cast<int>(Zahl1(f, "NumVec3", 0));
    const int nfv = static_cast<int>(Zahl1(f, "NumFloatVec", 0)), nfl = static_cast<int>(Zahl1(f, "NumFloat", 0));
    const int catchAll = static_cast<int>(Zahl1(f, "CatchAllBitCount", 0));
    const double qmb = Zahl1(f, "QuantizeMultBlock", 1), qms = Zahl1(f, "QuantizeMultSubblock", 0);
    const std::vector<long long> desc = Ganze(f, "DofTableDescBytes"), bps = Ganze(f, "BitsPerSubblock");
    const std::vector<long long> bx = Ganze(f, "DeltaBaseX"), by = Ganze(f, "DeltaBaseY"), bz = Ganze(f, "DeltaBaseZ"), bw = Ganze(f, "DeltaBaseW");
    const std::vector<long long> kz = Ganze(f, "KeyTimes");
    const std::vector<uint8_t> d = Bytes1(f, "Data");
    const size_t dofs = static_cast<size_t>(nq + nv + nfv);
    if (keys < 1 || desc.size() < dofs || bx.size() < dofs || by.size() < dofs || bz.size() < dofs || bw.size() < dofs || qmb == 0) { fehler = "DCT-Kopf unvollstaendig"; return false; }
    std::vector<size_t> erster(dofs);
    size_t e = 0;
    for (size_t i = 0; i < dofs; ++i) { erster[i] = e; e += static_cast<size_t>((desc[i] >> 4) & 15); }
    if (e != bps.size()) { fehler = "DCT: BitsPerSubblock passt nicht"; return false; }
    const int nb = (keys + 7) / 8;
    std::vector<std::array<std::array<double, 4>, 8>> koef(static_cast<size_t>(nb) * dofs);
    VorLeser r;
    r.d = &d;
    auto breite = [&](size_t eintrag, int comp) {
        const int w = static_cast<int>((static_cast<uint32_t>(bps[eintrag]) >> (12 - 4 * comp)) & 15u);
        return w == 15 ? catchAll : w;
    };
    for (int b = 0; b < nb; ++b)
        for (size_t i = 0; i < dofs; ++i) {
            auto& k2 = koef[static_cast<size_t>(b) * dofs + i];
            for (auto& x : k2) x = { 0, 0, 0, 0 };
            const int n = static_cast<int>((desc[i] >> 4) & 15);
            for (int j = (b == 0 ? 1 : 0); j < n; ++j)
                for (int comp = 0; comp < 4; ++comp) k2[static_cast<size_t>(j)][static_cast<size_t>(comp)] = static_cast<double>(r.Vz(breite(erster[i] + static_cast<size_t>(j), comp)));
            // Koeffizient 0 + Grundwert in 16 Bit, wie im Spiel gespeichert
            const long long basis[4] = { bx[i], by[i], bz[i], bw[i] };
            for (size_t comp = 0; comp < 4; ++comp)
                k2[0][comp] = static_cast<double>(static_cast<int16_t>(static_cast<uint16_t>(static_cast<long long>(k2[0][comp]) + basis[comp])));
        }
    if (r.ueber) { fehler = "DCT-Daten zu kurz"; return false; }
    aus.zeiten.clear();
    for (int k = 0; k < keys; ++k) aus.zeiten.push_back(k < static_cast<int>(kz.size()) ? static_cast<float>(kz[static_cast<size_t>(k)]) : static_cast<float>(k));
    auto wert = [&](size_t dof, int key, int comp) {
        const int b = key / 8, t = key % 8;
        const auto& k2 = koef[static_cast<size_t>(b) * dofs + dof];
        double s = 0;
        for (int i = 0; i < 8; ++i) s += k2[static_cast<size_t>(i)][static_cast<size_t>(comp)] * Basis(t, i, 0.25, 0.5) * ((qms * 0.1 * i + 1.0) / qmb);
        return s;
    };
    std::set<std::string> vergeben;
    size_t floatNr = 0;
    for (size_t i = 0; i < dofs; ++i) {
        const bool dreh = static_cast<int>(i) < nq, vek = !dreh && static_cast<int>(i) < nq + nv;
        const int komp = dreh ? 4 : (vek ? 3 : 4);
        const bool konst = ((desc[i] >> 4) & 15) == 0;
        if (dreh || vek) {
            Kanal kn;
            kn.art = dreh ? 'q' : 't';
            kn.komponenten = komp;
            kn.konstant = konst;
            for (int key = 0; key < (konst ? 1 : keys); ++key)
                for (int c = 0; c < komp; ++c) kn.werte.push_back(static_cast<float>(wert(i, key, c)));
            std::string n = (i < namen.size() && !namen[i].empty()) ? namen[i] : "kanal" + std::to_string(i);
            n = Eindeutig(vergeben, n);
            vergeben.insert(n);
            kn.name = n;
            aus.kanaele.push_back(std::move(kn));
        } else {
            // FloatVec: vier Floats je DOF, bis NumFloat erreicht
            for (int c = 0; c < 4 && static_cast<int>(floatNr) < nfl; ++c, ++floatNr) {
                Kanal kn;
                kn.art = 'f';
                kn.komponenten = 1;
                kn.konstant = konst;
                for (int key = 0; key < (konst ? 1 : keys); ++key) kn.werte.push_back(static_cast<float>(wert(i, key, c)));
                const size_t ni = static_cast<size_t>(nq + nv) + floatNr;
                std::string n = (ni < namen.size() && !namen[ni].empty()) ? namen[ni] : "float" + std::to_string(floatNr);
                n = Eindeutig(vergeben, n);
                vergeben.insert(n);
                kn.name = n;
                aus.kanaele.push_back(std::move(kn));
            }
        }
    }
    return true;
}

} // namespace

namespace {
// Alle 16-stelligen Hex-Texte (ANT-Schluessel) in einem Datensatz, auch in
// Feldern von Unterstrukturen.
void SammleKeys(const fbgd::Wert& w, std::vector<std::string>& aus) {
    if (w.art == fbgd::Wert::Art::Hex && w.text.size() == 16) aus.push_back(w.text);
    for (const fbgd::Wert& x : w.werte) SammleKeys(x, aus);
    for (const auto& satz : w.saetze) for (const auto& p : satz) SammleKeys(p.second, aus);
}
} // namespace

Quelle::Reichweite Quelle::FolgeFuerHeld(const std::vector<std::string>& startKeys, const std::string& heldGS, int held, size_t maxKnoten) {
    // 0.73.0: jeder Knoten traegt mit, ob er ueber eine Heldenweiche erreicht
    // wurde (Helden-Auswahl an einem IndexChooser oder eine fuer diesen Helden
    // WAHRE EnumBool-Bedingung). Nur solche Clips gelten als "vom Spiel
    // diesem Helden zugeordnet" - Z5 zeigte, dass Blaster-Helden sonst den
    // ganzen gemeinsamen Waffen-Automaten (503 Clips, meist 1p) bekommen.
    Reichweite r;
    std::set<std::string> gesehen, gesehenHeld;
    std::vector<std::pair<std::string, bool>> offen;
    std::map<std::string, int> enumBool;                       // 1 wahr, 0 falsch, -1 nicht heldbezogen
    auto heldBool = [&](const std::string& k) -> int {
        const auto da = enumBool.find(k);
        if (da != enumBool.end()) return da->second;
        int erg = -1;
        const auto o = nachKey_.find(k);
        if (o != nachKey_.end()) {
            fbgd::Datensatz d;
            const fbgd::Eintrag* e = Satz(o->second, d);
            if (e != nullptr && e->klassenName == "EnumBoolAsset") {
                const fbgd::Wert* quelle = fbgd::Feld(d.felder, "Source");
                const fbgd::Wert* wert = fbgd::Feld(d.felder, "Value");
                if (quelle != nullptr && wert != nullptr && quelle->text == heldGS) erg = (wert->ganz == held) ? 1 : 0;
            }
        }
        enumBool[k] = erg;
        return erg;
    };
    // 0 = frei, 1 = gesperrt (Bedingung gilt nicht), 2 = heldbezogen erfuellt
    auto pruefe = [&](const fbgd::Wert* wahr, const fbgd::Wert* falsch) {
        bool heldWahr = false;
        if (wahr != nullptr) for (const fbgd::Wert& x : wahr->werte) { if (x.text.size() != 16) continue; const int b = heldBool(x.text); if (b == 0) return 1; if (b == 1) heldWahr = true; }
        if (falsch != nullptr) for (const fbgd::Wert& x : falsch->werte) { if (x.text.size() != 16) continue; if (heldBool(x.text) == 1) return 1; }
        return heldWahr ? 2 : 0;
    };
    auto schiebe = [&](const std::string& k, bool heldPfad) {
        if (!nachKey_.count(k)) return;
        if (heldPfad) { if (gesehenHeld.insert(k).second) { gesehen.insert(k); offen.push_back({ k, true }); } }
        else if (!gesehenHeld.count(k) && gesehen.insert(k).second) offen.push_back({ k, false });
    };
    for (const std::string& k : startKeys) if (nachKey_.count(k) && !gesehen.count(k)) { schiebe(k, false); ++r.startGefunden; }
    while (!offen.empty()) {
        if (r.knoten >= maxKnoten) { r.abgebrochen = true; break; }
        const std::string key = offen.back().first;
        bool heldPfad = offen.back().second;
        offen.pop_back();
        ++r.knoten;
        const auto it = nachKey_.find(key);
        if (it == nachKey_.end()) continue;
        fbgd::Datensatz ds;
        const fbgd::Eintrag* e = Satz(it->second, ds);
        if (e == nullptr) continue;
        const std::string& kl = e->klassenName;
        ++r.typen[kl];
        if (EndetAuf(kl, "AnimationAsset")) { r.clips.insert(key); if (heldPfad) r.clipsHeld.insert(key); continue; }
        if (kl == "ClipControllerAsset") {
            if (const fbgd::Wert* a = fbgd::Feld(ds.felder, "Anim")) if (!a->text.empty()) { r.clips.insert(a->text); if (heldPfad) r.clipsHeld.insert(a->text); }
        }
        if (kl == "StateFlowTransitionAsset") {
            const int p = pruefe(fbgd::Feld(ds.felder, "ConditionsRequiredTrue"), fbgd::Feld(ds.felder, "ConditionsRequiredFalse"));
            if (p == 1) continue;
            if (p == 2) heldPfad = true;
        }
        if (kl == "SignalChooserEntryAsset") {
            const int p = pruefe(fbgd::Feld(ds.felder, "TrueSignals"), fbgd::Feld(ds.felder, "FalseSignals"));
            if (p == 1) continue;
            if (p == 2) heldPfad = true;
        }
        std::vector<std::string> keys;
        bool heldIndex = false;
        const fbgd::Wert* auswahl = kl == "IndexChooserControllerAsset" ? fbgd::Feld(ds.felder, "ChoiceAssetList") : nullptr;
        for (const auto* fs : { &ds.basis, &ds.felder })
            for (const auto& p : *fs) {
                if (p.first == "__key" || p.first == "__base") continue;
                if (auswahl != nullptr && &p.second == auswahl) continue;
                std::vector<std::string> teil;
                SammleKeys(p.second, teil);
                for (const std::string& k : teil) { if (k == heldGS) heldIndex = true; keys.push_back(k); }
            }
        for (const std::string& k : keys) schiebe(k, heldPfad);
        if (auswahl != nullptr) {
            if (heldIndex) {
                if (held >= 0 && static_cast<size_t>(held) < auswahl->werte.size()) schiebe(auswahl->werte[static_cast<size_t>(held)].text, true);
            } else {
                for (const fbgd::Wert& x : auswahl->werte) schiebe(x.text, heldPfad);
            }
        }
    }
    return r;
}

Quelle::Reichweite Quelle::FolgeFuerZustaende(const std::vector<std::string>& startKeys,
                                             const std::map<std::string, std::vector<int>>& zustaende, size_t maxKnoten) {
    Reichweite r;
    std::set<std::string> gesehen, gesehenTreffer;
    std::vector<std::pair<std::string, bool>> offen;
    std::map<std::string, int> enumBool;                       // 1 passt, 0 passt nicht, -1 unbekannte Groesse
    auto erlaubt = [&](const std::string& gs, int wert) {
        const auto it = zustaende.find(gs);
        if (it == zustaende.end()) return -1;
        for (int x : it->second) if (x == wert) return 1;
        return 0;
    };
    auto bedingung = [&](const std::string& k) -> int {
        const auto da = enumBool.find(k);
        if (da != enumBool.end()) return da->second;
        int erg = -1;
        const auto o = nachKey_.find(k);
        if (o != nachKey_.end()) {
            fbgd::Datensatz d;
            const fbgd::Eintrag* e = Satz(o->second, d);
            if (e != nullptr && e->klassenName == "EnumBoolAsset") {
                const fbgd::Wert* quelle = fbgd::Feld(d.felder, "Source");
                const fbgd::Wert* wert = fbgd::Feld(d.felder, "Value");
                if (quelle != nullptr && wert != nullptr) erg = erlaubt(quelle->text, static_cast<int>(wert->ganz));
            }
        }
        enumBool[k] = erg;
        return erg;
    };
    auto pruefe = [&](const fbgd::Wert* wahr, const fbgd::Wert* falsch) {
        bool trifft = false;
        if (wahr != nullptr) for (const fbgd::Wert& x : wahr->werte) { if (x.text.size() != 16) continue; const int b = bedingung(x.text); if (b == 0) return 1; if (b == 1) trifft = true; }
        if (falsch != nullptr) for (const fbgd::Wert& x : falsch->werte) { if (x.text.size() != 16) continue; if (bedingung(x.text) == 1) return 1; }
        return trifft ? 2 : 0;
    };
    auto schiebe = [&](const std::string& k, bool treffer) {
        if (!nachKey_.count(k)) return;
        if (treffer) { if (gesehenTreffer.insert(k).second) { gesehen.insert(k); offen.push_back({ k, true }); } }
        else if (!gesehenTreffer.count(k) && gesehen.insert(k).second) offen.push_back({ k, false });
    };
    for (const std::string& k : startKeys) if (nachKey_.count(k) && !gesehen.count(k)) { schiebe(k, false); ++r.startGefunden; }
    while (!offen.empty()) {
        if (r.knoten >= maxKnoten) { r.abgebrochen = true; break; }
        const std::string key = offen.back().first;
        bool treffer = offen.back().second;
        offen.pop_back();
        ++r.knoten;
        const auto it = nachKey_.find(key);
        if (it == nachKey_.end()) continue;
        fbgd::Datensatz ds;
        const fbgd::Eintrag* e = Satz(it->second, ds);
        if (e == nullptr) continue;
        const std::string& kl = e->klassenName;
        ++r.typen[kl];
        if (EndetAuf(kl, "AnimationAsset")) { r.clips.insert(key); if (treffer) r.clipsHeld.insert(key); continue; }
        if (kl == "ClipControllerAsset") {
            if (const fbgd::Wert* a = fbgd::Feld(ds.felder, "Anim")) if (!a->text.empty()) { r.clips.insert(a->text); if (treffer) r.clipsHeld.insert(a->text); }
        }
        if (kl == "StateFlowTransitionAsset" || kl == "SignalChooserEntryAsset") {
            const bool uebergang = kl == "StateFlowTransitionAsset";
            const int p = pruefe(fbgd::Feld(ds.felder, uebergang ? "ConditionsRequiredTrue" : "TrueSignals"),
                                 fbgd::Feld(ds.felder, uebergang ? "ConditionsRequiredFalse" : "FalseSignals"));
            if (p == 1) continue;
            if (p == 2) treffer = true;
        }
        std::vector<std::string> keys;
        std::string weiche;
        const fbgd::Wert* auswahl = kl == "IndexChooserControllerAsset" ? fbgd::Feld(ds.felder, "ChoiceAssetList") : nullptr;
        for (const auto* fs : { &ds.basis, &ds.felder })
            for (const auto& p : *fs) {
                if (p.first == "__key" || p.first == "__base") continue;
                if (auswahl != nullptr && &p.second == auswahl) continue;
                if (auswahl != nullptr && p.second.art == fbgd::Wert::Art::Hex && p.second.text.size() == 16 && zustaende.count(p.second.text)) weiche = p.second.text;
                std::vector<std::string> teil;
                SammleKeys(p.second, teil);
                for (const std::string& k : teil) keys.push_back(k);
            }
        for (const std::string& k : keys) schiebe(k, treffer);
        if (auswahl != nullptr) {
            if (!weiche.empty()) {
                // 1.05.0: Steht auf dem Platz der Figur derselbe Clip wie auf den
                // meisten anderen Plaetzen, ist das die VORGABE des Automaten und
                // nicht ihre eigene Animation. Gemessen an Boba Fett: er hat keine
                // eigenen Nahkampf-Clips, deshalb stand auf seinem Platz Lukes
                // bzw. Reys Clip - im Fenster sah es aus, als benutze er Lukes
                // Animationen. Solche Vorgaben werden weiter verfolgt, aber nicht
                // mehr der Figur zugerechnet.
                std::map<std::string, size_t> haeufig;
                for (const fbgd::Wert& x : auswahl->werte) if (!x.text.empty()) ++haeufig[x.text];
                size_t groesste = 0;
                std::string vorgabe;
                for (const auto& h : haeufig) if (h.second > groesste) { groesste = h.second; vorgabe = h.first; }
                const bool vieleGleich = groesste * 2 >= auswahl->werte.size();      // Mehrheit = Vorgabe
                const auto zw = zustaende.find(weiche);
                for (int v : zw->second) {
                    if (v < 0 || static_cast<size_t>(v) >= auswahl->werte.size()) continue;
                    const std::string& ziel = auswahl->werte[static_cast<size_t>(v)].text;
                    schiebe(ziel, !(vieleGleich && ziel == vorgabe));
                }
            } else {
                for (const fbgd::Wert& x : auswahl->werte) schiebe(x.text, treffer);
            }
        }
    }
    return r;
}

std::vector<Quelle::Fund> Quelle::SucheAssets(const std::string& bankTeil, const std::string& klasseTeil, const std::string& nameTeil, size_t maxTreffer) {
    std::vector<Fund> aus;
    for (const auto& kv : nachKey_) {
        if (aus.size() >= maxTreffer) break;
        const std::string& bank = BankName(kv.second.bank);
        if (!bankTeil.empty() && bank.find(bankTeil) == std::string::npos) continue;
        fbgd::Datensatz ds;
        const fbgd::Eintrag* e = Satz(kv.second, ds);
        if (e == nullptr) continue;
        if (!klasseTeil.empty() && e->klassenName.find(klasseTeil) == std::string::npos) continue;
        const std::string name = ds.nameDa ? ds.name : std::string();
        if (!nameTeil.empty() && name.find(nameTeil) == std::string::npos) continue;
        aus.push_back({ kv.first, name, e->klassenName, bank });
    }
    return aus;
}

bool Quelle::LiesAsset(const std::string& key, fbgd::Datensatz& ds, std::string& klasse) {
    const auto it = nachKey_.find(key);
    if (it == nachKey_.end()) return false;
    const fbgd::Eintrag* e = Satz(it->second, ds);
    if (e == nullptr) return false;
    klasse = e->klassenName;
    return true;
}

Quelle::Reichweite Quelle::Folge(const std::vector<std::string>& startKeys, size_t maxKnoten) {
    Reichweite r;
    std::set<std::string> gesehen;
    std::vector<std::string> offen;
    for (const std::string& k : startKeys) if (nachKey_.count(k) && gesehen.insert(k).second) { offen.push_back(k); ++r.startGefunden; }
    while (!offen.empty()) {
        if (r.knoten >= maxKnoten) { r.abgebrochen = true; break; }
        const std::string key = offen.back();
        offen.pop_back();
        ++r.knoten;
        const auto it = nachKey_.find(key);
        if (it == nachKey_.end()) continue;
        fbgd::Datensatz ds;
        const fbgd::Eintrag* e = Satz(it->second, ds);
        if (e == nullptr) continue;
        ++r.typen[e->klassenName];
        if (EndetAuf(e->klassenName, "AnimationAsset")) { r.clips.insert(key); continue; }   // Clip selbst: nicht weiter
        if (e->klassenName == "ClipControllerAsset") {
            if (const fbgd::Wert* a = fbgd::Feld(ds.felder, "Anim")) if (!a->text.empty()) r.clips.insert(a->text);
        }
        std::vector<std::string> keys;
        for (const auto* fs : { &ds.basis, &ds.felder })
            for (const auto& p : *fs) {
                if (p.first == "__key" || p.first == "__base") continue;
                SammleKeys(p.second, keys);
            }
        for (const std::string& k : keys)
            if (nachKey_.count(k) && gesehen.insert(k).second) offen.push_back(k);
    }
    return r;
}

const std::set<long long>& Quelle::RigDofSet(size_t rig) {
    const auto da = rigDofSet_.find(rig);
    if (da != rigDofSet_.end()) return da->second;
    const std::vector<long long>& d = RigDofs(rig);
    return rigDofSet_.emplace(rig, std::set<long long>(d.begin(), d.end())).first->second;
}

const Quelle::NamenErgebnis& Quelle::NamenFuer(const fbgd::Felder& basis) {
    const fbgd::Wert* w = fbgd::Feld(basis, "ChannelToDofAsset");
    const std::string schl = w != nullptr ? w->text : std::string();
    const auto da = namenCache_.find(schl);
    if (da != namenCache_.end()) return da->second;
    NamenErgebnis ne;
    std::vector<long long> dofIds;
    const auto it = nachKey_.find(schl);
    if (it != nachKey_.end()) {
        fbgd::Datensatz cd;
        if (Satz(it->second, cd) != nullptr) dofIds = Ganze(cd.felder, "DofIds");
    }
    ne.dofs = dofIds.size();
    ne.namen.assign(dofIds.size(), std::string());
    if (!dofIds.empty()) {
        const std::set<long long> gesucht(dofIds.begin(), dofIds.end());
        size_t bester = rigs_.size(), besteDeckung = 0;
        for (size_t r = 0; r < rigs_.size(); ++r) {
            const std::set<long long>& vorrat = RigDofSet(r);              // 1.12.0: einmal gebaut
            if (vorrat.empty()) continue;
            size_t drin = 0;
            for (long long x : gesucht) if (vorrat.count(x)) ++drin;
            if (drin > besteDeckung) { besteDeckung = drin; bester = r; }
            if (besteDeckung == gesucht.size()) break;                     // vollstaendig, Suche beenden
        }
        if (bester < rigs_.size() && besteDeckung >= std::max<size_t>(1, gesucht.size() * 9 / 10)) {
            const auto& rn = RigNamen(bester);
            const std::vector<long long>& d = RigDofs(bester);
            std::map<long long, size_t> stelle;
            for (size_t i = 0; i < d.size(); ++i) stelle[d[i]] = i;
            ne.rig = rn.second;
            ne.rigTreffer = besteDeckung;
            for (size_t k = 0; k < dofIds.size(); ++k) {
                const auto st = stelle.find(dofIds[k]);
                if (st != stelle.end() && st->second < rn.first.size() && !rn.first[st->second].empty()) {
                    ne.namen[k] = rn.first[st->second];
                    ++ne.benannt;
                }
            }
            // 1.42.0: NACHBENENNEN. Bei Yoda deckt das beste Rig nur 122 von 126
            // DofIds (31 von 50 Clips) - die vier Spuren hiessen "kanalNN" und
            // liefen in Max ins Leere. Fehlende DofIds werden in den uebrigen
            // Rigs gesucht; ein Name gilt nur, wenn ALLE Rigs, die die DofId
            // kennen, denselben Namen dafuer fuehren.
            if (ne.benannt < dofIds.size()) {
                for (size_t k = 0; k < dofIds.size(); ++k) {
                    if (!ne.namen[k].empty()) continue;
                    std::string name;
                    bool einig = true;
                    for (size_t r = 0; r < rigs_.size() && einig; ++r) {
                        if (r == bester || RigDofSet(r).count(dofIds[k]) == 0) continue;
                        const std::vector<long long>& d2 = RigDofs(r);
                        const auto& rn2 = RigNamen(r);
                        for (size_t i = 0; i < d2.size(); ++i) {
                            if (d2[i] != dofIds[k] || i >= rn2.first.size() || rn2.first[i].empty()) continue;
                            if (name.empty()) name = rn2.first[i];
                            else if (name != rn2.first[i]) einig = false;
                            break;
                        }
                    }
                    if (!name.empty() && einig) { ne.namen[k] = name; ++ne.benannt; ++ne.nachbenannt; }
                    else if (!name.empty()) ++ne.uneinig;
                }
            }
        }
    }
    return namenCache_.emplace(schl, std::move(ne)).first->second;
}

bool Quelle::Kanalnamen(const ClipEintrag& c, std::vector<std::string>& namen, std::string& rig, size_t& benannt) {
    // 1.11.0: ueber denselben Zwischenspeicher wie Entpacke. Vorher suchte jede
    // Abfrage neu durch alle Rigs - im Labor waren das 40 Minuten je Fahrzeug.
    namen.clear();
    rig.clear();
    benannt = 0;
    fbgd::Datensatz ds;
    if (Satz(Ort{ c.bank, c.eintrag }, ds) == nullptr) return false;
    const NamenErgebnis& ne = NamenFuer(ds.basis);
    namen = ne.namen;
    rig = ne.rig;
    benannt = ne.benannt;
    return !namen.empty();
}

bool Quelle::Felder(const ClipEintrag& ce, fbgd::Datensatz& aus) {
    return Satz(Ort{ ce.bank, ce.eintrag }, aus) != nullptr;
}

bool Quelle::FindeEintrag(const std::string& key, ClipEintrag& aus) {
    const auto it = nachKey_.find(key);
    if (it == nachKey_.end()) return false;
    fbgd::Datensatz ds;
    const fbgd::Eintrag* e = Satz(it->second, ds);
    if (e == nullptr) return false;
    aus = ClipEintrag();
    aus.bank = it->second.bank;
    aus.eintrag = it->second.eintrag;
    aus.klasse = e->klassenName;
    aus.name = ds.nameDa ? ds.name : std::string();
    aus.key = key;
    if (const fbgd::Wert* w = fbgd::Feld(ds.basis, "CodecType")) aus.codec = fbgd::Codec(w->ganz);
    return true;
}

bool Quelle::Entpacke(const ClipEintrag& c, Clip& aus, std::string& fehler) {
    aus = Clip();
    fbgd::Datensatz ds;
    const fbgd::Eintrag* e = Satz(Ort{ c.bank, c.eintrag }, ds);
    if (e == nullptr) { fehler = "Bank nicht lesbar"; return false; }
    aus.name = c.name;
    aus.bank = bankName_[c.bank];
    aus.klasse = e->klassenName;
    aus.codec = c.codec;
    aus.endFrame = static_cast<int32_t>(Ganz(ds.basis, "EndFrame", -1));
    aus.additiv = Ganz(ds.basis, "Additive") != 0;
    if (const fbgd::Wert* w = fbgd::Feld(ds.basis, "TrimmedDuration")) aus.dauer = w->gleit;

    // Namen: DofIds des Clips -> Rig mit der groessten Deckung -> Slotnamen
    // (0.65.2: je ChannelToDofAsset nur einmal gesucht).
    const NamenErgebnis& ne = NamenFuer(ds.basis);
    aus.dofIds = ne.dofs;
    aus.rig = ne.rig;
    aus.rigTreffer = ne.rigTreffer;
    aus.benannt = ne.benannt;
    aus.nachbenannt = ne.nachbenannt;
    aus.uneinig = ne.uneinig;
    std::vector<std::string> namen = ne.namen;
    for (size_t k = 0; k < namen.size(); ++k) if (namen[k].empty()) namen[k] = "kanal" + std::to_string(k);

    const long long q = Ganz(ds.felder, "QuatCount"), v = Ganz(ds.felder, "Vec3Count"), fl = Ganz(ds.felder, "FloatCount");
    if (aus.klasse == "RawAnimationAsset") {
        const long long cq = Ganz(ds.felder, "ConstQuatCount"), cv = Ganz(ds.felder, "ConstVec3Count"), cf = Ganz(ds.felder, "ConstFloatCount");
        const long long keys = Ganz(ds.felder, "NumKeys");
        const std::vector<float> daten = Floats(ds.felder, "Data");
        const std::vector<float> fest = Floats(ds.felder, "ConstData");
        const std::vector<long long> mapping = Ganze(ds.felder, "MappingIndices");
        const long long jeKey = 4 * q + 4 * v + ((fl + 3) / 4) * 4;
        if (keys && !daten.empty() && static_cast<long long>(daten.size()) != keys * jeKey) {
            fehler = "Data " + std::to_string(daten.size()) + " passt nicht zu " + std::to_string(keys) + " Keys mal " + std::to_string(jeKey);
            return false;
        }
        const std::vector<long long> kz = Ganze(ds.felder, "KeyTimes");
        if (!kz.empty()) for (long long t : kz) aus.zeiten.push_back(static_cast<float>(t));
        else for (long long t = 0; t < keys; ++t) aus.zeiten.push_back(static_cast<float>(t));
        struct Art { char a; bool k; long long i; };
        std::vector<Art> liste;
        for (long long i = 0; i < q; ++i) liste.push_back({ 'q', false, i });
        for (long long i = 0; i < v; ++i) liste.push_back({ 't', false, i });
        for (long long i = 0; i < fl; ++i) liste.push_back({ 'f', false, i });
        for (long long i = 0; i < cq; ++i) liste.push_back({ 'q', true, i });
        for (long long i = 0; i < cv; ++i) liste.push_back({ 't', true, i });
        for (long long i = 0; i < cf; ++i) liste.push_back({ 'f', true, i });
        std::set<std::string> vergeben;
        auto hole = [](const std::vector<float>& quelle, long long von, int n, std::vector<float>& ziel) {
            for (int i = 0; i < n; ++i) {
                const long long p = von + i;
                if (p >= 0 && static_cast<size_t>(p) < quelle.size()) ziel.push_back(quelle[static_cast<size_t>(p)]);
            }
        };
        for (size_t j = 0; j < liste.size(); ++j) {
            const long long m = (j < mapping.size()) ? mapping[j] : static_cast<long long>(j);
            std::string n = (m >= 0 && static_cast<size_t>(m) < namen.size()) ? namen[static_cast<size_t>(m)] : "kanal" + std::to_string(m);
            n = Eindeutig(vergeben, n);
            vergeben.insert(n);
            Kanal k;
            k.name = n;
            k.art = liste[j].a;
            k.komponenten = (k.art == 'q') ? 4 : (k.art == 't' ? 3 : 1);
            k.konstant = liste[j].k;
            const long long i = liste[j].i;
            if (k.konstant) {
                if (k.art == 'q') hole(fest, 4 * i, 4, k.werte);
                else if (k.art == 't') hole(fest, 4 * cq + 4 * i, 3, k.werte);
                else hole(fest, 4 * cq + 4 * cv + i, 1, k.werte);
            } else {
                for (long long key = 0; key < keys; ++key) {
                    const long long b = key * jeKey;
                    if (k.art == 'q') hole(daten, b + 4 * i, 4, k.werte);
                    else if (k.art == 't') hole(daten, b + 4 * q + 4 * i, 3, k.werte);
                    else hole(daten, b + 4 * q + 4 * v + i, 1, k.werte);
                }
            }
            aus.kanaele.push_back(std::move(k));
        }
        return true;
    }
    if (aus.klasse == "FrameAnimationAsset") {
        const std::vector<float> daten = Floats(ds.felder, "Data");
        if (static_cast<long long>(daten.size()) < 4 * q + 4 * v + fl) {
            fehler = "Data " + std::to_string(daten.size()) + " zu kurz fuer die Pose";
            return false;
        }
        std::set<std::string> vergeben;
        long long nr = 0;
        auto ablegen = [&](char art, long long von, int komp) {
            std::string n = (nr < static_cast<long long>(namen.size())) ? namen[static_cast<size_t>(nr)] : "kanal" + std::to_string(nr);
            while (vergeben.count(n)) n += "#2";
            vergeben.insert(n);
            Kanal k;
            k.name = n;
            k.art = art;
            k.komponenten = komp;
            k.konstant = true;
            for (int i = 0; i < komp; ++i) k.werte.push_back(daten[static_cast<size_t>(von + i)]);
            aus.kanaele.push_back(std::move(k));
            ++nr;
        };
        for (long long i = 0; i < q; ++i) ablegen('q', 4 * i, 4);
        for (long long i = 0; i < v; ++i) ablegen('t', 4 * q + 4 * i, 3);
        for (long long i = 0; i < fl; ++i) ablegen('f', 4 * q + 4 * v + i, 1);
        return true;
    }
    if (aus.klasse == "VbrAnimationAsset") {                                                  // 0.65.0
        const bool ok = VbrLesen(ds, namen, aus, fehler);
        // Diagnose (1.43.0): SWBF2_VBR_STAT gesetzt -> je Clip Palette und Ergebnis auf stderr
        if (std::getenv("SWBF2_VBR_STAT") != nullptr)
            std::fprintf(stderr, "VBRSTAT pal %lld konst %lld kanaele %zu %s %s\n", Ganz(ds.felder, "ConstPaletteSize"),
                         Ganz(ds.felder, "ConstQuaternionCount") * 4 + Ganz(ds.felder, "ConstVector3Count") * 3 + Ganz(ds.felder, "ConstFloatCount"),
                         namen.size(), ok ? "OK" : "FEHLER", c.name.c_str());
        return ok;
    }
    if (aus.klasse == "DctAnimationAsset") return DctLesen(ds, namen, aus, fehler);      // 0.65.0
    fehler = aus.klasse + " (" + aus.codec + ") wird noch nicht entpackt";
    return false;
}

namespace {

const char kCacheMagic[8] = { 'F', 'B', 'C', 'L', 'I', 'P', 'S', '1' };
const uint32_t kCacheFassung = 2;

struct Schreiber {
    std::vector<uint8_t> b;
    void U32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void U64(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void F32(float f) { uint32_t v; std::memcpy(&v, &f, 4); U32(v); }
    void Str(const std::string& s) { U32(static_cast<uint32_t>(s.size())); b.insert(b.end(), s.begin(), s.end()); }
};

struct Leser {
    const std::vector<uint8_t>& b;
    size_t p = 0;
    bool ok = true;
    explicit Leser(const std::vector<uint8_t>& d) : b(d) {}
    uint32_t U32() { if (p + 4 > b.size()) { ok = false; return 0; } uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | b[p + static_cast<size_t>(i)]; p += 4; return v; }
    uint64_t U64() { const uint64_t lo = U32(); const uint64_t hi = U32(); return lo | (hi << 32); }
    float F32() { const uint32_t v = U32(); float f; std::memcpy(&f, &v, 4); return f; }
    std::string Str() {
        const uint32_t n = U32();
        if (!ok || p + n > b.size()) { ok = false; return std::string(); }
        std::string s(b.begin() + static_cast<long>(p), b.begin() + static_cast<long>(p + n));
        p += n;
        return s;
    }
};

} // namespace

bool Quelle::Speichere(const std::string& utf8Pfad, int64_t kopfnummer, std::string& fehler) const {
    Schreiber w;
    w.b.insert(w.b.end(), kCacheMagic, kCacheMagic + 8);
    w.U32(kCacheFassung);
    w.U64(static_cast<uint64_t>(kopfnummer));
    w.U32(static_cast<uint32_t>(bankName_.size()));
    for (size_t i = 0; i < bankName_.size(); ++i) { w.Str(bankName_[i]); w.Str(bankSha_[i]); }
    w.U32(static_cast<uint32_t>(nachKey_.size()));
    for (const auto& kv : nachKey_) { w.Str(kv.first); w.U32(static_cast<uint32_t>(kv.second.bank)); w.U32(static_cast<uint32_t>(kv.second.eintrag)); }
    w.U32(static_cast<uint32_t>(rigs_.size()));
    for (const Ort& o : rigs_) { w.U32(static_cast<uint32_t>(o.bank)); w.U32(static_cast<uint32_t>(o.eintrag)); }
    w.U32(static_cast<uint32_t>(clips_.size()));
    for (const ClipEintrag& c : clips_) {
        w.U32(static_cast<uint32_t>(c.bank)); w.U32(static_cast<uint32_t>(c.eintrag));
        w.Str(c.name); w.Str(c.klasse); w.Str(c.codec); w.Str(c.key); w.Str(c.anzeige);
        w.U32(static_cast<uint32_t>(c.endFrame)); w.F32(c.fps); w.U32(static_cast<uint32_t>(c.controller));
    }
    w.b.insert(w.b.end(), kCacheMagic, kCacheMagic + 8);          // Endmarke: Datei vollstaendig
    return fbdatei::SchreibeAlles(utf8Pfad, w.b, fehler);
}

bool Quelle::Lade(fbgame::Spiel& spiel, const std::string& utf8Pfad, int64_t kopfnummer, std::string& grund) {
    std::vector<uint8_t> d;
    if (!fbdatei::LiesAlles(utf8Pfad, d, grund)) return false;
    if (d.size() < 32 || std::memcmp(d.data(), kCacheMagic, 8) != 0 || std::memcmp(d.data() + d.size() - 8, kCacheMagic, 8) != 0) {
        grund = "keine vollstaendige Clipdatei";
        return false;
    }
    Leser r(d);
    r.p = 8;
    if (r.U32() != kCacheFassung) { grund = "andere Fassung der Clipdatei"; return false; }
    if (static_cast<int64_t>(r.U64()) != kopfnummer) { grund = "Spiel wurde aktualisiert (Kopfnummer anders)"; return false; }
    Quelle q;
    const uint32_t nb = r.U32();
    for (uint32_t i = 0; i < nb && r.ok; ++i) { q.bankName_.push_back(r.Str()); q.bankSha_.push_back(r.Str()); }
    const uint32_t nk = r.U32();
    for (uint32_t i = 0; i < nk && r.ok; ++i) { std::string k = r.Str(); Ort o; o.bank = r.U32(); o.eintrag = r.U32(); q.nachKey_.emplace(std::move(k), o); }
    const uint32_t nr = r.U32();
    for (uint32_t i = 0; i < nr && r.ok; ++i) { Ort o; o.bank = r.U32(); o.eintrag = r.U32(); q.rigs_.push_back(o); }
    const uint32_t nc = r.U32();
    q.clips_.reserve(nc);
    for (uint32_t i = 0; i < nc && r.ok; ++i) {
        ClipEintrag c;
        c.bank = r.U32(); c.eintrag = r.U32();
        c.name = r.Str(); c.klasse = r.Str(); c.codec = r.Str(); c.key = r.Str(); c.anzeige = r.Str();
        c.endFrame = static_cast<int32_t>(r.U32()); c.fps = r.F32(); c.controller = r.U32();
        if (c.bank >= q.bankName_.size()) { r.ok = false; break; }
        q.clips_.push_back(std::move(c));
    }
    if (!r.ok || r.p + 8 != d.size()) { grund = "Clipdatei beschaedigt"; return false; }
    for (const Ort& o : q.rigs_) if (o.bank >= q.bankName_.size()) { grund = "Clipdatei beschaedigt"; return false; }
    *this = std::move(q);
    spiel_ = &spiel;
    return true;
}

bool SchreibeFbanim(const std::string& utf8Pfad, const Clip& c, std::string& fehler) {
    std::vector<uint8_t> str(1, 0);
    std::map<std::string, uint32_t> bekannt{ { std::string(), 0u } };
    auto st = [&](const std::string& s) -> uint32_t {
        const auto it = bekannt.find(s);
        if (it != bekannt.end()) return it->second;
        const uint32_t off = static_cast<uint32_t>(str.size());
        str.insert(str.end(), s.begin(), s.end());
        str.push_back(0);
        bekannt[s] = off;
        return off;
    };
    auto u32 = [](std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto f32 = [&](std::vector<uint8_t>& b, float f) { uint32_t v; std::memcpy(&v, &f, 4); u32(b, v); };
    std::vector<uint8_t> kanal;
    for (const Kanal& k : c.kanaele) {
        u32(kanal, st(k.name));
        u32(kanal, static_cast<uint32_t>(static_cast<unsigned char>(k.art)));
        u32(kanal, static_cast<uint32_t>(k.komponenten));
        u32(kanal, k.konstant ? 1u : 0u);
        const size_t saetze = k.konstant ? 1 : c.zeiten.size();
        for (size_t s = 0; s < saetze; ++s) {
            for (int i = 0; i < k.komponenten; ++i) {
                const size_t p = s * static_cast<size_t>(k.komponenten) + static_cast<size_t>(i);
                f32(kanal, p < k.werte.size() ? k.werte[p] : 0.0f);
            }
        }
    }
    const uint32_t nameOff = st(c.name), codecOff = st(c.codec);
    while (str.size() % 4) str.push_back(0);
    std::vector<uint8_t> aus;
    const char magic[8] = { 'F', 'B', 'A', 'N', 'I', 'M', '0', '1' };
    aus.insert(aus.end(), magic, magic + 8);
    u32(aus, 1); u32(aus, 64);
    u32(aus, static_cast<uint32_t>(c.kanaele.size()));
    u32(aus, static_cast<uint32_t>(c.zeiten.size()));
    u32(aus, 0);
    u32(aus, static_cast<uint32_t>(str.size()));
    f32(aus, c.dauer);
    u32(aus, static_cast<uint32_t>(c.endFrame));
    u32(aus, c.additiv ? 1u : 0u);
    u32(aus, nameOff); u32(aus, codecOff);
    f32(aus, 0.0f);
    u32(aus, static_cast<uint32_t>(-1));
    u32(aus, 0);
    aus.insert(aus.end(), str.begin(), str.end());
    for (float z : c.zeiten) f32(aus, z);
    aus.insert(aus.end(), kanal.begin(), kanal.end());
    return fbdatei::SchreibeAlles(utf8Pfad, aus, fehler);
}

bool NormiereLaufrichtung(Clip& c, double& ersetztGrad) {
    ersetztGrad = 0.0;
    if (c.additiv) return false;
    for (Kanal& k : c.kanaele) {
        if (k.name != "AITrajectory.q" || k.art != 'q' || k.komponenten != 4 || k.werte.size() < 4) continue;
        // Konstant? (auch ein animierter Kanal, der sich nicht bewegt)
        const size_t n = k.werte.size() / 4;
        for (size_t i = 1; i < n; ++i) {
            const double d = std::fabs(double(k.werte[0]) * k.werte[i * 4] + double(k.werte[1]) * k.werte[i * 4 + 1] +
                                       double(k.werte[2]) * k.werte[i * 4 + 2] + double(k.werte[3]) * k.werte[i * 4 + 3]);
            if (d < 0.99996) return false;              // > ~1 Grad: echte Drehung (Turn-Clip) - so lassen
        }
        const double y = k.werte[1], w = k.werte[3];
        const double x = k.werte[0], z = k.werte[2];
        if (std::fabs(x) > 0.02 || std::fabs(z) > 0.02) return false;   // keine reine Gierdrehung
        double gier = 2.0 * std::atan2(y, w) * 57.29577951308232;
        while (gier > 180.0) gier -= 360.0;
        while (gier <= -180.0) gier += 360.0;
        if (std::fabs(gier - kGrundrichtungGrad) < 1.0) return false;
        const float s = static_cast<float>(std::sin(kGrundrichtungGrad * 0.5 / 57.29577951308232));
        const float co = static_cast<float>(std::cos(kGrundrichtungGrad * 0.5 / 57.29577951308232));
        for (size_t i = 0; i < n; ++i) { k.werte[i * 4] = 0.0f; k.werte[i * 4 + 1] = s; k.werte[i * 4 + 2] = 0.0f; k.werte[i * 4 + 3] = co; }
        // Die GANZE Lage drehen, nicht nur die Drehung: bei Obi-Wan zeigt die
        // Verschiebung von AITrajectory in jedem Richtungsclip nach +Z (Walk_Bwd
        // +3,71 m, Walk_Left1 +3,60 m) - der Clip ist als Ganzes um die Hochachse
        // gedreht. Nur die Drehung zu tauschen liesse ihn rueckwaerts nach vorn
        // gleiten. Korrektur C = Gier(Grundrichtung - gier): q' = C*q, t' = R(C)*t
        // (lokal * Eltern mit Zeilenvektoren). Probe: Bwd -> -Z 3,71 m wie bei
        // Vader (-3,80), Left1 -> +X 3,60 m wie bei Vader (+4,28).
        const double korrektur = (kGrundrichtungGrad - gier) / 57.29577951308232;
        const double cs = std::cos(korrektur), sn = std::sin(korrektur);
        for (Kanal& t : c.kanaele) {
            if (t.name != "AITrajectory.t" || t.art != 't' || t.komponenten != 3) continue;
            for (size_t i = 0; i + 2 < t.werte.size(); i += 3) {
                const double x = t.werte[i], z = t.werte[i + 2];
                t.werte[i] = static_cast<float>(x * cs + z * sn);
                t.werte[i + 2] = static_cast<float>(-x * sn + z * cs);
            }
        }
        ersetztGrad = gier;
        return true;
    }
    return false;
}

} // namespace fbanim
