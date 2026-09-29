// ============================================================
//  fbcas.cpp - Entschleierung, cas.cat und die Blockentpackung.
//
//  Vorlage ist fb_container.py. Wo dort eine Zahl steht, steht
//  sie hier auch - und daneben, woher sie kommt.
// ============================================================
#include "fbcas.h"
#include "fbdatei.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include "lz4.h"
#include "miniz.h"
}
#include "zstd.h"

namespace fbcas {
namespace {

// ---- Leser, der nie ueber das Ende hinauslaeuft -------------
struct Leser {
    const uint8_t* d;
    size_t n;
    size_t p = 0;
    bool bad = false;

    Leser(const std::vector<uint8_t>& v, size_t start = 0)
        : d(v.data()), n(v.size()), p(start) {}

    bool brauche(size_t k) {
        if (bad || p + k > n) { bad = true; return false; }
        return true;
    }
    uint8_t u8() { return brauche(1) ? d[p++] : 0; }
    uint16_t u16le() {
        if (!brauche(2)) return 0;
        const uint16_t v = static_cast<uint16_t>(d[p] | (d[p + 1] << 8));
        p += 2; return v;
    }
    uint16_t u16be() {
        if (!brauche(2)) return 0;
        const uint16_t v = static_cast<uint16_t>((d[p] << 8) | d[p + 1]);
        p += 2; return v;
    }
    uint32_t u32le() {
        if (!brauche(4)) return 0;
        const uint32_t v = static_cast<uint32_t>(d[p]) |
                           (static_cast<uint32_t>(d[p + 1]) << 8) |
                           (static_cast<uint32_t>(d[p + 2]) << 16) |
                           (static_cast<uint32_t>(d[p + 3]) << 24);
        p += 4; return v;
    }
    uint32_t u32be() {
        if (!brauche(4)) return 0;
        const uint32_t v = (static_cast<uint32_t>(d[p]) << 24) |
                           (static_cast<uint32_t>(d[p + 1]) << 16) |
                           (static_cast<uint32_t>(d[p + 2]) << 8) |
                           static_cast<uint32_t>(d[p + 3]);
        p += 4; return v;
    }
    int32_t i32le() { return static_cast<int32_t>(u32le()); }
    void sha1(uint8_t aus[20]) {
        if (!brauche(20)) return;
        std::memcpy(aus, d + p, 20);
        p += 20;
    }
    void ueberspringe(size_t k) { if (brauche(k)) p += k; }
    bool ende() const { return p >= n; }
};

const char kCatKennung[16] = { 'N','y','a','n','N','y','a','n',
                               'N','y','a','n','N','y','a','n' };

} // namespace

// ------------------------------------------------------------
size_t DeobfStart(const std::vector<uint8_t>& daten) {
    if (daten.size() >= 4) {
        const uint32_t magic = static_cast<uint32_t>(daten[0]) |
                               (static_cast<uint32_t>(daten[1]) << 8) |
                               (static_cast<uint32_t>(daten[2]) << 16) |
                               (static_cast<uint32_t>(daten[3]) << 24);
        if (magic == kObfMagicA || magic == kObfMagicB) return kObfHeader;
    }
    return 0;
}

long FindeCatKennung(const std::vector<uint8_t>& daten) {
    // Regulaer steht der Katalog bei 0 oder 0x22C. Weicht eine
    // Fassung ab, wird die Kennung in den ersten 4 KB gesucht -
    // damit der Fehler benannt wird statt nur gemeldet.
    const size_t kandidaten[3] = { DeobfStart(daten), 0, kObfHeader };
    for (size_t s : kandidaten) {
        if (s + 16 <= daten.size() &&
            std::memcmp(daten.data() + s, kCatKennung, 16) == 0) {
            return static_cast<long>(s);
        }
    }
    const size_t grenze = daten.size() < 0x1000 ? daten.size() : 0x1000;
    for (size_t s = 0; s + 16 <= grenze; ++s) {
        if (std::memcmp(daten.data() + s, kCatKennung, 16) == 0) {
            return static_cast<long>(s);
        }
    }
    return -1;
}

bool LiesKatalog(const std::vector<uint8_t>& daten, Katalog& aus, std::string& fehler) {
    const long start = FindeCatKennung(daten);
    if (start < 0) {
        fehler = "kein cas.cat: die Kennung NyanNyanNyanNyan steht nicht in den ersten 4 KB";
        return false;
    }
    Leser r(daten, static_cast<size_t>(start));
    r.ueberspringe(16);
    const uint32_t anzahl = r.u32le();
    const uint32_t patches = r.u32le();
    r.u32le();                 // encryptedCount - bei SWBF2 immer 0
    r.ueberspringe(12);
    if (r.bad) { fehler = "cas.cat: Kopf unvollstaendig"; return false; }

    aus.eintraege.clear();
    aus.eintraege.reserve(anzahl);
    for (uint32_t i = 0; i < anzahl && !r.bad; ++i) {
        CatEintrag e;
        r.sha1(e.sha1);
        e.versatz = r.u32le();
        e.laenge = r.u32le();
        e.logischerVersatz = r.u32le();
        // Nur das unterste Byte ist die Archivnummer.
        e.archiv = r.i32le() & 0xFF;
        aus.eintraege.push_back(e);
    }
    aus.patches.clear();
    aus.patches.reserve(patches);
    for (uint32_t i = 0; i < patches && !r.bad; ++i) {
        CatPatch p;
        r.sha1(p.sha1);
        r.sha1(p.basisSha1);
        r.sha1(p.deltaSha1);
        aus.patches.push_back(p);
    }
    if (r.bad) { fehler = "cas.cat endet mitten in den Eintraegen"; return false; }
    return true;
}

// ------------------------------------------------------------
void BlockLeser::SetzeWoerterbuch(std::vector<uint8_t> woerterbuch) {
    woerterbuch_ = std::move(woerterbuch);
}

void BlockLeser::Zaehle(int art) {
    for (auto& z : zaehler_) {
        if (z.first == art) { ++z.second; return; }
    }
    zaehler_.emplace_back(art, 1);
}

bool BlockLeser::EinBlock(const std::vector<uint8_t>& daten, size_t& p,
                          std::vector<uint8_t>& aus, std::string& fehler) {
    Leser r(daten, p);
    uint32_t entpackt = r.u32be();
    const uint16_t artRoh = r.u16le();
    uint32_t puffer = r.u16be();
    if (r.bad) { fehler = "Block: Kopf unvollstaendig"; return false; }

    // Die oberen acht Bit des Verfahrensworts tragen Zusatzangaben:
    // die unteren vier davon erweitern die Pufferlaenge um 16 Bit.
    const uint32_t flags = (artRoh & 0xFF00u) >> 8;
    if (flags & 0x0Fu) puffer = ((flags & 0x0Fu) << 16) + puffer;

    // Das oberste Byte der entpackten Groesse sagt: mit Woerterbuch.
    const bool mitWoerterbuch = (entpackt & 0xFF000000u) != 0;
    entpackt &= 0x00FFFFFFu;
    const int art = artRoh & 0x7F;

    if (!r.brauche(puffer)) { fehler = "Block: Nutzdaten reichen nicht"; return false; }
    const uint8_t* nutz = r.d + r.p;
    r.p += puffer;
    p = r.p;
    Zaehle(art);

    const size_t vorher = aus.size();
    switch (art) {
    case 0x00:                                   // roh
        aus.insert(aus.end(), nutz, nutz + puffer);
        return true;

    case 0x02: {                                 // zlib
        aus.resize(vorher + entpackt);
        mz_ulong ziel = entpackt;
        const int rc = mz_uncompress(aus.data() + vorher, &ziel, nutz, puffer);
        if (rc != MZ_OK || ziel != entpackt) {
            char b[128];
            std::snprintf(b, sizeof b, "zlib: Fehler %d, %lu statt %u Byte",
                          rc, static_cast<unsigned long>(ziel), entpackt);
            fehler = b;
            return false;
        }
        return true;
    }

    case 0x09: {                                 // lz4
        aus.resize(vorher + entpackt);
        const int n = LZ4_decompress_safe(reinterpret_cast<const char*>(nutz),
                                          reinterpret_cast<char*>(aus.data() + vorher),
                                          static_cast<int>(puffer),
                                          static_cast<int>(entpackt));
        if (n < 0 || static_cast<uint32_t>(n) != entpackt) {
            char b[128];
            std::snprintf(b, sizeof b, "lz4: %d statt %u Byte", n, entpackt);
            fehler = b;
            return false;
        }
        return true;
    }

    case 0x0F: {                                 // zstd, mit oder ohne Woerterbuch
        if (mitWoerterbuch && woerterbuch_.empty()) {
            fehler = "Block braucht das ebx.dict aus initfs_win32, es fehlt";
            return false;
        }
        aus.resize(vorher + entpackt);
        size_t n = 0;
        if (mitWoerterbuch) {
            ZSTD_DCtx* ctx = ZSTD_createDCtx();
            if (ctx == nullptr) { fehler = "zstd: kein Kontext"; return false; }
            n = ZSTD_decompress_usingDict(ctx, aus.data() + vorher, entpackt,
                                          nutz, puffer,
                                          woerterbuch_.data(), woerterbuch_.size());
            ZSTD_freeDCtx(ctx);
        } else {
            n = ZSTD_decompress(aus.data() + vorher, entpackt, nutz, puffer);
        }
        if (ZSTD_isError(n) || n != entpackt) {
            char b[192];
            std::snprintf(b, sizeof b, "zstd%s: %s (%zu statt %u Byte)",
                          mitWoerterbuch ? " mit Woerterbuch" : "",
                          ZSTD_isError(n) ? ZSTD_getErrorName(n) : "Laenge falsch",
                          n, entpackt);
            fehler = b;
            return false;
        }
        return true;
    }

    case 0x11: case 0x15: case 0x19: {
        char b[96];
        std::snprintf(b, sizeof b, "Oodle-Block (Typ 0x%02X) - kommt bei SWBF2 nicht vor", art);
        fehler = b;
        return false;
    }

    default: {
        char b[96];
        std::snprintf(b, sizeof b, "unbekanntes Verfahren 0x%02X", art);
        fehler = b;
        return false;
    }
    }
}

bool BlockLeser::EntpackeAlles(const std::vector<uint8_t>& daten,
                               std::vector<uint8_t>& aus, std::string& fehler) {
    size_t p = 0;
    while (p < daten.size()) {
        if (!EinBlock(daten, p, aus, fehler)) return false;
    }
    return true;
}

// ------------------------------------------------------------
//  Deltapfad (CasReader.ReadPatched)
//
//  Der Deltastrom besteht aus Anweisungen. Die obersten vier Bit
//  eines u32 sagen, was zu tun ist:
//    0  so viele Basisbloecke unveraendert uebernehmen
//    1  einen Basisblock mit Delta mischen (16-Bit-Versaetze)
//    2  einen Basisblock mit Delta mischen (32-Bit-Versaetze)
//    3  so viele Deltabloecke einfuegen
//    4  so viele Basisbloecke ueberspringen
// ------------------------------------------------------------
bool BlockLeser::EntpackeGepatcht(const std::vector<uint8_t>& basis,
                                  const std::vector<uint8_t>& delta,
                                  std::vector<uint8_t>& aus, std::string& fehler) {
    size_t pb = 0;
    Leser d(delta);
    while (!d.ende()) {
        const uint32_t befehl = d.u32be();
        if (d.bad) break;
        const uint32_t art = (befehl & 0xF0000000u) >> 28;
        const uint32_t n = befehl & 0x0FFFFFFFu;

        if (art == 0) {
            for (uint32_t i = 0; i < n; ++i) {
                if (!EinBlock(basis, pb, aus, fehler)) return false;
            }
        } else if (art == 3) {
            for (uint32_t i = 0; i < n; ++i) {
                if (!EinBlock(delta, d.p, aus, fehler)) return false;
            }
        } else if (art == 4) {
            std::vector<uint8_t> weg;
            for (uint32_t i = 0; i < n; ++i) {
                if (!EinBlock(basis, pb, weg, fehler)) return false;
            }
        } else if (art == 1 || art == 2) {
            // Einen Basisblock entpacken und stueckweise ersetzen.
            std::vector<uint8_t> b;
            if (!EinBlock(basis, pb, b, fehler)) return false;
            std::vector<uint8_t> dd;
            if (!EinBlock(delta, d.p, dd, fehler)) return false;

            Leser dr(dd);
            size_t gelesen = 0;
            while (!dr.ende()) {
                const uint32_t versatz = (art == 1) ? dr.u16be() : dr.u32be();
                const uint32_t weglassen = (art == 1) ? dr.u16be() : dr.u32be();
                if (dr.bad) break;
                if (versatz > b.size()) { fehler = "Delta: Versatz hinter dem Basisblock"; return false; }
                aus.insert(aus.end(), b.begin() + static_cast<long>(gelesen),
                           b.begin() + static_cast<long>(versatz));
                gelesen = versatz + weglassen;
                if (gelesen > b.size()) { fehler = "Delta: uebersprungenes Stueck zu lang"; return false; }
            }
            aus.insert(aus.end(), b.begin() + static_cast<long>(gelesen), b.end());
        } else {
            char t[96];
            std::snprintf(t, sizeof t, "Delta: unbekannte Anweisung %u", art);
            fehler = t;
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------
bool LiesDatei(const std::string& pfad, std::vector<uint8_t>& aus, std::string& fehler) {
    // 64-Bit-Laenge und Unicode-Pfad: siehe fbdatei.h.
    return fbdatei::LiesAlles(pfad, aus, fehler);
}

std::string Sha1Text(const uint8_t sha1[20]) {
    static const char* ziffern = "0123456789abcdef";
    std::string s(40, '0');
    for (int i = 0; i < 20; ++i) {
        s[i * 2] = ziffern[(sha1[i] >> 4) & 0xF];
        s[i * 2 + 1] = ziffern[sha1[i] & 0xF];
    }
    return s;
}

} // namespace fbcas
