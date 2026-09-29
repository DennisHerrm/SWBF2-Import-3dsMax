// ============================================================
//  fbbundle.cpp - Bundle lesen.
// ============================================================
#include "fbbundle.h"
#include "fbcas.h"
#include "fbdb.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fbbundle {
namespace {

constexpr uint32_t kMagicStandard  = 0xED1CEDB8u;
constexpr uint32_t kMagicFifa      = 0xC3889333u;
constexpr uint32_t kMagicEncrypted = 0xC3E5D5C3u;
// "pecm" gross-endig gelesen - der Wuerfel fuer Spiele bis 2017.
constexpr uint32_t kSalt = 0x7065636Du;

struct Leser {
    const uint8_t* d; size_t n; size_t p = 0; bool bad = false;
    Leser(const uint8_t* dd, size_t nn) : d(dd), n(nn) {}
    bool brauche(size_t k) { if (bad || p + k > n) { bad = true; return false; } return true; }
    uint32_t u32(bool be) {
        if (!brauche(4)) return 0;
        uint32_t v;
        if (be) v = (static_cast<uint32_t>(d[p]) << 24) | (static_cast<uint32_t>(d[p+1]) << 16) |
                    (static_cast<uint32_t>(d[p+2]) << 8) | d[p+3];
        else    v = static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p+1]) << 8) |
                    (static_cast<uint32_t>(d[p+2]) << 16) | (static_cast<uint32_t>(d[p+3]) << 24);
        p += 4; return v;
    }
    uint64_t u64(bool be) {
        const uint32_t a = u32(be), b = u32(be);
        return be ? ((static_cast<uint64_t>(a) << 32) | b)
                  : ((static_cast<uint64_t>(b) << 32) | a);
    }
    void roh(uint8_t* aus, size_t k) { if (brauche(k)) { std::memcpy(aus, d + p, k); p += k; } }
    std::string cstr() {
        std::string s;
        while (brauche(1)) { const uint8_t c = d[p++]; if (c == 0) break; s.push_back(static_cast<char>(c)); }
        return s;
    }
    void setze(size_t q) { if (q <= n) p = q; else bad = true; }
};

std::string Hex(const uint8_t* b, size_t n) {
    static const char* z = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; ++i) { s[i*2] = z[(b[i] >> 4) & 0xF]; s[i*2+1] = z[b[i] & 0xF]; }
    return s;
}

} // namespace

uint32_t Fnv1(const std::string& s) {
    // ACHTUNG: das ist NICHT der Standard-FNV-1 (2166136261 / 16777619),
    // sondern Frosty.Hash.Fnv1.HashString: h = 5381, dann h = (h * 33) ^ c.
    // Mit dem Standardverfahren trifft kein einziger Bundle-Hash.
    uint32_t h = 5381u;
    for (unsigned char c : s) {
        h = static_cast<uint32_t>(h * 33u) ^ c;
    }
    return h;
}

bool LiesBundle(const std::vector<uint8_t>& daten, Bundle& aus, std::string& fehler) {
    Leser r(daten.data(), daten.size());
    const uint32_t groesse = r.u32(true);
    bool be = true;
    uint32_t magic = r.u32(true) ^ kSalt;
    if (magic != kMagicStandard && magic != kMagicFifa && magic != kMagicEncrypted) {
        // Noch einmal klein-endig versuchen, bevor aufgegeben wird.
        be = false;
        r.setze(4);
        magic = r.u32(false) ^ kSalt;
        if (magic != kMagicStandard && magic != kMagicFifa && magic != kMagicEncrypted) {
            char b[96];
            std::snprintf(b, sizeof b, "Bundle-Kennung passt nicht (0x%08X)", magic);
            fehler = b;
            return false;
        }
    }
    if (magic == kMagicEncrypted) {
        fehler = "verschluesseltes Bundle (Key2) - bei SWBF2 nicht erwartet";
        return false;
    }
    const bool mitSha1 = (magic != kMagicFifa);

    const uint32_t gesamt = r.u32(be);
    const uint32_t nEbx = r.u32(be);
    const uint32_t nRes = r.u32(be);
    const uint32_t nChunk = r.u32(be);
    // Die Versaetze zaehlen ab dem Kopfende, deshalb 0x20 abziehen.
    const uint32_t stringsAb = r.u32(be) - 0x20;
    const uint32_t metaAb = r.u32(be) - 0x20;
    const uint32_t metaGroesse = r.u32(be);
    if (r.bad || groesse < 0x20 || groesse > daten.size()) {
        fehler = "Bundle: Kopf unvollstaendig oder Groesse falsch";
        return false;
    }

    // Der Rumpf beginnt bei 0x24, NICHT bei 0x20: vier Byte Groesse plus
    // ein Kopf von 0x20. Die Versaetze im Kopf zaehlen ab dem Kopfanfang,
    // also ab 4 - deshalb oben "- 0x20" und hier "+ 0x24". Wer bei 0x20
    // anfaengt, liest alles um vier Byte verschoben und landet mitten in
    // den Eintraegen.
    if (groesse + 4 > daten.size() + 0x20) { fehler = "Bundle: Blob zu kurz"; return false; }
    Leser b(daten.data() + 0x24, daten.size() - 0x24);

    std::vector<std::string> sha1s;
    sha1s.reserve(gesamt);
    for (uint32_t i = 0; i < gesamt && !b.bad; ++i) {
        if (mitSha1) {
            uint8_t h[20] = {};
            b.roh(h, 20);
            sha1s.push_back(Hex(h, 20));
        } else {
            sha1s.push_back(std::string(40, '0'));
        }
    }

    auto namenAn = [&](uint32_t versatz) -> std::string {
        const size_t merk = b.p;
        b.setze(stringsAb + versatz);
        const std::string s = b.cstr();
        b.setze(merk);
        return s;
    };

    aus.ebx.clear();
    for (uint32_t i = 0; i < nEbx && !b.bad; ++i) {
        const uint32_t nameAb = b.u32(be);
        const uint32_t osize = b.u32(be);
        EbxRef e;
        e.name = namenAn(nameAb);
        e.sha1 = (i < sha1s.size()) ? sha1s[i] : std::string(40, '0');
        e.originalSize = osize;
        aus.ebx.push_back(e);
    }

    aus.res.clear();
    aus.res.resize(nRes);
    for (uint32_t i = 0; i < nRes && !b.bad; ++i) {
        const uint32_t nameAb = b.u32(be);
        aus.res[i].originalSize = b.u32(be);
        aus.res[i].name = namenAn(nameAb);
        const size_t k = nEbx + i;
        aus.res[i].sha1 = (k < sha1s.size()) ? sha1s[k] : std::string(40, '0');
    }
    // Typ, Meta und Rid stehen als DREI eigene Listen dahinter, nicht
    // eingestreut - das ist die Falle an dieser Stelle.
    for (uint32_t i = 0; i < nRes && !b.bad; ++i) aus.res[i].resType = b.u32(be);
    for (uint32_t i = 0; i < nRes && !b.bad; ++i) b.roh(aus.res[i].resMeta, 16);
    for (uint32_t i = 0; i < nRes && !b.bad; ++i) aus.res[i].resRid = b.u64(be);

    aus.chunks.clear();
    for (uint32_t i = 0; i < nChunk && !b.bad; ++i) {
        ChunkRef c;
        b.roh(c.guid, 16);
        if (be) {
            // NativeReader.ReadGuid(Endian.Big): Data1 (4 Byte), Data2 und
            // Data3 (je 2 Byte) werden gedreht, der Rest bleibt.
            std::swap(c.guid[0], c.guid[3]);
            std::swap(c.guid[1], c.guid[2]);
            std::swap(c.guid[4], c.guid[5]);
            std::swap(c.guid[6], c.guid[7]);
        }
        c.logicalOffset = b.u32(be);
        c.logicalSize = b.u32(be);
        const size_t k = nEbx + nRes + i;
        c.sha1 = (k < sha1s.size()) ? sha1s[k] : std::string(40, '0');
        aus.chunks.push_back(c);
    }

    if (b.bad) { fehler = "Bundle endet mitten in den Eintraegen"; return false; }
    (void)metaAb; (void)metaGroesse;      // Chunk-Meta wird noch nicht gebraucht
    aus.datenVersatz = groesse;
    aus.grossEndig = be;
    return true;
}

} // namespace fbbundle
