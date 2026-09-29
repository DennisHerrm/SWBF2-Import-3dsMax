// ============================================================
//  fbgd.cpp - GD-Baenke lesen.
// ============================================================
#include "fbgd.h"

#include <cstdio>
#include <cstring>

namespace fbgd {
namespace {

constexpr int kKlassenkopf = 32;
constexpr int kFeldeintrag = 32;

uint32_t U32(const std::vector<uint8_t>& d, size_t p, bool be) {
    if (p + 4 > d.size()) return 0;
    if (be) return (static_cast<uint32_t>(d[p]) << 24) | (static_cast<uint32_t>(d[p+1]) << 16) |
                  (static_cast<uint32_t>(d[p+2]) << 8) | d[p+3];
    return static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p+1]) << 8) |
           (static_cast<uint32_t>(d[p+2]) << 16) | (static_cast<uint32_t>(d[p+3]) << 24);
}
uint16_t U16(const std::vector<uint8_t>& d, size_t p, bool be) {
    if (p + 2 > d.size()) return 0;
    return be ? static_cast<uint16_t>((d[p] << 8) | d[p+1])
              : static_cast<uint16_t>(d[p] | (d[p+1] << 8));
}
int16_t I16(const std::vector<uint8_t>& d, size_t p, bool be) { return static_cast<int16_t>(U16(d, p, be)); }
int64_t I64(const std::vector<uint8_t>& d, size_t p, bool be) {
    if (p + 8 > d.size()) return 0;
    uint64_t v = 0;
    if (be) { for (int i = 0; i < 8; ++i) v = (v << 8) | d[p + i]; }
    else    { for (int i = 7; i >= 0; --i) v = (v << 8) | d[p + i]; }
    return static_cast<int64_t>(v);
}
uint64_t U64(const std::vector<uint8_t>& d, size_t p, bool be) { return static_cast<uint64_t>(I64(d, p, be)); }

std::string CStr(const std::vector<uint8_t>& d, size_t p) {
    if (p >= d.size()) return std::string();
    size_t e = p;
    while (e < d.size() && d[e] != 0) ++e;
    return std::string(reinterpret_cast<const char*>(d.data() + p), e - p);
}

std::vector<Block> LiesBloecke(const std::vector<uint8_t>& d, size_t start) {
    std::vector<Block> aus;
    size_t p = start;
    while (p + 12 <= d.size()) {
        if (std::memcmp(d.data() + p, "GD.", 3) != 0) break;
        Block b;
        b.marke = std::string(reinterpret_cast<const char*>(d.data() + p), 7);
        b.pos = p;
        // Das achte Byte sagt die Bytereihenfolge: 'b' heisst gross-endig.
        b.kleinEndig = (d[p + 7] != 'b');
        b.groesse = U32(d, p + 8, !b.kleinEndig);
        b.wort0 = U32(d, p + 12, !b.kleinEndig);
        b.wort1 = U32(d, p + 16, !b.kleinEndig);
        aus.push_back(b);
        if (b.marke == "GD.STRM") { p += 16; continue; }   // umspannt den Rest
        if (b.groesse <= 16) break;
        p += b.groesse;
    }
    return aus;
}

struct RohKlasse {
    size_t pos = 0, ende = 0;
    std::string name;
    uint32_t hash = 0, groesse = 0;
    uint8_t nativ = 0;
    std::vector<GdFeld> felder;
};

bool LiesKlasse(const std::vector<uint8_t>& d, size_t pos, bool be, RohKlasse& k) {
    if (pos + kKlassenkopf > d.size()) return false;
    k.pos = pos;
    k.groesse = U32(d, pos + 12, be);
    const uint32_t strOff = U32(d, pos + 16, be);
    const uint32_t strLen = U32(d, pos + 20, be);
    k.nativ = d[pos + 25];
    k.hash = U32(d, pos + 28, be);
    if (strOff < kKlassenkopf) return false;
    const int n = static_cast<int>((strOff - kKlassenkopf) / kFeldeintrag);
    const size_t st = pos + strOff;
    if (st + strLen > d.size()) return false;

    // Die Zeichenkettentabelle beginnt mit einem Nullbyte; der Klassenname ist
    // die erste nichtleere Zeichenkette darin.
    size_t q = st;
    while (q < st + strLen && k.name.empty()) {
        k.name = CStr(d, q);
        q += k.name.size() + 1;
    }

    for (int i = 0; i < n; ++i) {
        const size_t f = pos + kKlassenkopf + static_cast<size_t>(i) * kFeldeintrag;
        if (f + kFeldeintrag > d.size()) return false;
        GdFeld g;
        g.typHash = U32(d, f + 0, be);
        g.elementGroesse = U32(d, f + 4, be);
        g.versatz = U32(d, f + 8, be);
        const uint32_t nameVersatz = U32(d, f + 12, be);
        g.anzahl = U16(d, f + 16, be);
        g.flags = U16(d, f + 18, be);
        g.istArray = (g.flags == 1);
        g.elementAusrichtung = U16(d, f + 20, be);
        g.rle = I16(d, f + 22, be);
        g.layout = Versatz(I64(d, f + 24, be));
        g.name = CStr(d, st + nameVersatz);
        k.felder.push_back(g);
    }
    size_t ende = pos + strOff + strLen;
    ende = (ende + 7) / 8 * 8;
    k.ende = ende;
    return true;
}

} // namespace

bool LiesBank(const std::vector<uint8_t>& daten, Bank& aus, std::string& fehler) {
    if (daten.size() < 32) { fehler = "keine GD-Bank: zu klein"; return false; }
    // Der Kopf ist GROSS-ENDIG.
    aus.paketTyp = U32(daten, 0, true);
    aus.datenVersatz = U32(daten, 4, true);
    aus.reflTyp = U32(daten, 8, true);
    aus.unterZahl = U32(daten, 12, true);
    aus.unterKapazitaet = U32(daten, 16, true);
    const size_t kopfGroesse = 36;

    if (aus.datenVersatz + 7 > daten.size() ||
        std::memcmp(daten.data() + aus.datenVersatz + 4, "GD.", 3) != 0) {
        char b[80];
        std::snprintf(b, sizeof b, "keine GD-Marke bei %u", aus.datenVersatz + 4);
        fehler = b;
        return false;
    }

    for (uint32_t i = 0; i < aus.unterZahl; ++i) {
        const size_t p = kopfGroesse + static_cast<size_t>(i) * 16;
        if (p + 8 > daten.size()) break;
        char b[24];
        std::snprintf(b, sizeof b, "%016llx",
                      static_cast<unsigned long long>(U64(daten, p, true)));
        aus.ids.push_back(b);
    }

    aus.bloecke = LiesBloecke(daten, aus.datenVersatz + 4);

    // ---- REF2: die Typbeschreibung -------------------------------
    const Block* ref = nullptr;
    for (const Block& b : aus.bloecke) {
        if (b.marke == "GD.REF2" || b.marke == "GD.REFL") { ref = &b; break; }
    }
    if (ref != nullptr) {
        // Die Typnamen stehen im Klartext im Block - die reicht der Leser
        // schon zuverlaessig durch, auch ohne die Feldtabelle.
        const size_t ende = ref->pos + ref->groesse;
        size_t i = ref->pos;
        while (i < ende && i < daten.size()) {
            size_t j = i;
            while (j < ende && j < daten.size() && daten[j] != 0) ++j;
            const size_t laenge = j - i;
            if (laenge >= 2 && laenge <= 40) {
                bool druckbar = true;
                for (size_t k = i; k < j; ++k) if (daten[k] < 32 || daten[k] >= 127) { druckbar = false; break; }
                if (druckbar) aus.typNamen.push_back(std::string(reinterpret_cast<const char*>(daten.data() + i), laenge));
            }
            i = j + 1;
        }

        const bool be = !ref->kleinEndig;
        size_t p = ref->pos + 12;               // nach Marke (8) und Gesamtgroesse (4)
        if (ref->marke == "GD.REFL") p += 4;
        const int64_t anzahl = Versatz(I64(daten, p, be));
        p += 8;
        if (anzahl >= 0 && anzahl < 4096) {
            p += 8 * static_cast<size_t>(anzahl);   // Offsetliste ueberspringen
            std::vector<RohKlasse> klassen;
            for (int64_t k = 0; k < anzahl; ++k) {
                RohKlasse rk;
                if (!LiesKlasse(daten, p, be, rk)) {
                    char b[80];
                    std::snprintf(b, sizeof b, "REF2 endet vor Klasse %zu", klassen.size());
                    aus.warnungen.push_back(b);
                    break;
                }
                klassen.push_back(rk);
                p = rk.ende;
            }
            std::map<uint32_t, std::string> nachHash;
            for (const RohKlasse& k : klassen) nachHash[k.hash] = k.name;
            for (const RohKlasse& k : klassen) {
                GdKlasse g;
                g.name = k.name; g.hash = k.hash; g.groesse = k.groesse; g.nativ = k.nativ;
                g.felder = k.felder;
                for (GdFeld& f : g.felder) {
                    const auto it = nachHash.find(f.typHash);
                    if (it != nachHash.end()) f.typName = it->second;
                }
                aus.klassen[k.hash] = g;
            }
            if (static_cast<int64_t>(klassen.size()) != anzahl) {
                char b[80];
                std::snprintf(b, sizeof b, "REF2: %zu von %lld Klassen gelesen",
                              klassen.size(), static_cast<long long>(anzahl));
                aus.warnungen.push_back(b);
            }
        } else {
            char b[80];
            std::snprintf(b, sizeof b, "REF2: unglaubwuerdige Klassenzahl %lld",
                          static_cast<long long>(anzahl));
            aus.warnungen.push_back(b);
        }
    }

    // ---- DAT2: je Eintrag ein Datenblock -------------------------
    constexpr size_t kDatensatzVersatz = 44;
    for (const Block& b : aus.bloecke) {
        if (b.marke != "GD.DAT2") continue;
        const bool be = !b.kleinEndig;
        Eintrag e;
        e.pos = b.pos;
        e.groesse = b.groesse;
        e.typHash = b.wort0;
        e.klassenHash = U32(daten, b.pos + 28, be);
        const auto it = aus.klassen.find(e.klassenHash);
        if (it != aus.klassen.end()) e.klassenName = it->second.name;
        e.datensatz = b.pos + kDatensatzVersatz;
        e.kleinEndig = b.kleinEndig;
        aus.eintraege.push_back(e);
    }
    if (!aus.eintraege.empty() && aus.eintraege.size() != aus.unterZahl) {
        char b[96];
        std::snprintf(b, sizeof b, "subDataCount %u, aber %zu DAT2-Bloecke",
                      aus.unterZahl, aus.eintraege.size());
        aus.warnungen.push_back(b);
    }
    return true;
}

std::string AlsText(const Bank& b) {
    std::string aus;
    char t[512];
    std::snprintf(t, sizeof t, "BANK typ=%u dataOffset=%u refl=%u unter=%u/%u\n",
                  b.paketTyp, b.datenVersatz, b.reflTyp, b.unterZahl, b.unterKapazitaet);
    aus += t;
    std::snprintf(t, sizeof t, "IDS %zu\n", b.ids.size()); aus += t;
    for (const std::string& i : b.ids) { aus += "ID "; aus += i; aus += "\n"; }
    std::snprintf(t, sizeof t, "BLOECKE %zu\n", b.bloecke.size()); aus += t;
    for (const Block& k : b.bloecke) {
        std::snprintf(t, sizeof t, "BL %s pos=%zu size=%u le=%d w=%u,%u\n",
                      k.marke.c_str(), k.pos, k.groesse, k.kleinEndig ? 1 : 0, k.wort0, k.wort1);
        aus += t;
    }
    std::snprintf(t, sizeof t, "KLASSEN %zu\n", b.klassen.size()); aus += t;
    for (const auto& kv : b.klassen) {
        const GdKlasse& k = kv.second;
        std::snprintf(t, sizeof t, "K %08x %s size=%u nativ=%u felder=%zu\n",
                      k.hash, k.name.c_str(), k.groesse, k.nativ, k.felder.size());
        aus += t;
        for (const GdFeld& f : k.felder) {
            std::snprintf(t, sizeof t,
                          "  F %s typ=%08x(%s) off=%u es=%u n=%u flags=%u arr=%d ea=%u rle=%d lay=%lld\n",
                          f.name.c_str(), f.typHash, f.typName.c_str(), f.versatz,
                          f.elementGroesse, f.anzahl, f.flags, f.istArray ? 1 : 0,
                          f.elementAusrichtung, f.rle, static_cast<long long>(f.layout));
            aus += t;
        }
    }
    std::snprintf(t, sizeof t, "EINTRAEGE %zu\n", b.eintraege.size()); aus += t;
    for (const Eintrag& e : b.eintraege) {
        std::snprintf(t, sizeof t, "E pos=%zu size=%u typ=%08x klasse=%08x(%s) satz=%zu\n",
                      e.pos, e.groesse, e.typHash, e.klassenHash, e.klassenName.c_str(),
                      e.datensatz);
        aus += t;
    }
    for (const std::string& w : b.warnungen) { aus += "WARNUNG "; aus += w; aus += "\n"; }
    return aus;
}

} // namespace fbgd
