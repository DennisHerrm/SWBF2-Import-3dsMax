// ============================================================
//  fbmeshset.cpp - MeshSet lesen.
// ============================================================
#include "fbmeshset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fbmesh {
namespace {

const std::map<uint8_t, const char*>& UsageTabelle() {
    static const std::map<uint8_t, const char*> t = {
        { 0x00, "Unknown" },
        { 0x01, "Pos" },
        { 0x02, "BoneIndices" },
        { 0x03, "BoneIndices2" },
        { 0x04, "BoneWeights" },
        { 0x05, "BoneWeights2" },
        { 0x06, "Normal" },
        { 0x07, "Tangent" },
        { 0x08, "Binormal" },
        { 0x09, "BinormalSign" },
        { 0x0A, "WorldTrans1" },
        { 0x0B, "WorldTrans2" },
        { 0x0C, "WorldTrans3" },
        { 0x0D, "InstanceId" },
        { 0x17, "Index" },
        { 0x1E, "Color0" },
        { 0x1F, "Color1" },
        { 0x21, "TexCoord0" },
        { 0x22, "TexCoord1" },
        { 0x23, "TexCoord2" },
        { 0x24, "TexCoord3" },
        { 0x25, "TexCoord4" },
        { 0x26, "TexCoord5" },
        { 0x27, "TexCoord6" },
        { 0x28, "TexCoord7" },
        { 0x29, "DisplacementMapTexCoord" },
        { 0x2A, "RadiosityTexCoord" },
        { 0x2B, "VisInfo" },
        { 0x2D, "PackedTexCoord0" },
        { 0x2E, "PackedTexCoord1" },
        { 0x2F, "PackedTexCoord2" },
        { 0x30, "PackedTexCoord3" },
        { 0x33, "SubMaterialIndex" },
        { 0x34, "TangentSpace" },
        { 0x64, "RegionIds" },
        { 0x65, "BlendWeights" }
    };
    return t;
}

struct FormatInfo { const char* name; int groesse; };

const std::map<uint8_t, FormatInfo>& FormatTabelle() {
    static const std::map<uint8_t, FormatInfo> t = {
        { 0x00, { "None", 0 } },
        { 0x01, { "Float", 4 } },
        { 0x02, { "Float2", 8 } },
        { 0x03, { "Float3", 12 } },
        { 0x04, { "Float4", 16 } },
        { 0x05, { "Half", 2 } },
        { 0x06, { "Half2", 4 } },
        { 0x07, { "Half3", 6 } },
        { 0x08, { "Half4", 8 } },
        { 0x0A, { "Byte4", 4 } },
        { 0x0B, { "Byte4N", 4 } },
        { 0x0C, { "UByte4", 4 } },
        { 0x0D, { "UByte4N", 4 } },
        { 0x0E, { "Short", 2 } },
        { 0x0F, { "Short2", 4 } },
        { 0x10, { "Short3", 6 } },
        { 0x11, { "Short4", 8 } },
        { 0x12, { "ShortN", 2 } },
        { 0x13, { "Short2N", 4 } },
        { 0x14, { "Short3N", 6 } },
        { 0x15, { "Short4N", 8 } },
        { 0x16, { "UShort2", 4 } },
        { 0x17, { "UShort4", 8 } },
        { 0x18, { "UShort2N", 4 } },
        { 0x19, { "UShort4N", 8 } },
        { 0x1A, { "Int", 4 } },
        { 0x1B, { "Int2", 8 } },
        { 0x1C, { "Int4", 16 } },
        { 0x1D, { "IntN", 4 } },
        { 0x1E, { "Int2N", 8 } },
        { 0x1F, { "Int4N", 16 } },
        { 0x20, { "UInt", 4 } },
        { 0x21, { "UInt2", 8 } },
        { 0x22, { "UInt4", 16 } },
        { 0x23, { "UIntN", 4 } },
        { 0x24, { "UInt2N", 8 } },
        { 0x25, { "UInt4N", 16 } },
        { 0x26, { "Comp3_10_10_10", 4 } },
        { 0x27, { "Comp3N_10_10_10", 4 } },
        { 0x28, { "UComp3_10_10_10", 4 } },
        { 0x29, { "UComp3N_10_10_10", 4 } },
        { 0x2A, { "Comp3_11_11_10", 4 } },
        { 0x2B, { "Comp3N_11_11_10", 4 } },
        { 0x2C, { "UComp3_11_11_10", 4 } },
        { 0x2D, { "UComp3N_11_11_10", 4 } },
        { 0x2E, { "Comp4_10_10_10_2", 4 } },
        { 0x2F, { "Comp4N_10_10_10_2", 4 } },
        { 0x30, { "UComp4_10_10_10_2", 4 } },
        { 0x31, { "UComp4N_10_10_10_2", 4 } },
        { 0x32, { "UByteN", 1 } },
        { 0x33, { "Int3", 12 } },
        { 0x34, { "UInt3", 12 } }
    };
    return t;
}

std::string UsageName(uint8_t u) {
    const auto& t = UsageTabelle();
    const auto it = t.find(u);
    if (it != t.end()) return it->second;
    char b[8]; std::snprintf(b, sizeof b, "0x%02X", u); return b;
}

FormatInfo FormatVon(uint8_t f, std::string& name) {
    const auto& t = FormatTabelle();
    const auto it = t.find(f);
    if (it != t.end()) { name = it->second.name; return it->second; }
    char b[8]; std::snprintf(b, sizeof b, "0x%02X", f);
    name = b;
    return FormatInfo{ "", 0 };
}

// ---- Leser, der nie ueber das Ende hinauslaeuft -------------
struct R {
    const uint8_t* d; size_t n; size_t p = 0; bool bad = false;
    R(const std::vector<uint8_t>& v, size_t start = 0) : d(v.data()), n(v.size()), p(start) {}
    bool brauche(size_t k) { if (bad || p + k > n) { bad = true; return false; } return true; }
    uint8_t  u8()  { return brauche(1) ? d[p++] : 0; }
    int8_t   i8()  { return static_cast<int8_t>(u8()); }
    uint16_t u16() { if (!brauche(2)) return 0; const uint16_t v = static_cast<uint16_t>(d[p] | (d[p+1] << 8)); p += 2; return v; }
    int16_t  i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() {
        if (!brauche(4)) return 0;
        const uint32_t v = static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p+1]) << 8) |
                           (static_cast<uint32_t>(d[p+2]) << 16) | (static_cast<uint32_t>(d[p+3]) << 24);
        p += 4; return v;
    }
    int32_t  i32() { return static_cast<int32_t>(u32()); }
    uint64_t u64() { const uint32_t a = u32(), b = u32(); return (static_cast<uint64_t>(b) << 32) | a; }
    int64_t  i64() { return static_cast<int64_t>(u64()); }
    float    f32() { const uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }
    void ueberspringe(size_t k) { if (brauche(k)) p += k; }
    void setze(size_t q) { if (q <= n) p = q; else bad = true; }
    void aufRunden(size_t k) { while (p % k) { if (!brauche(1)) return; ++p; } }
    std::string cstrAn(int64_t off) const {
        if (off <= 0 || static_cast<size_t>(off) >= n) return std::string();
        size_t e = static_cast<size_t>(off);
        while (e < n && d[e] != 0) ++e;
        return std::string(reinterpret_cast<const char*>(d + off), e - static_cast<size_t>(off));
    }
    std::string guid() {
        uint8_t g[16] = {};
        if (!brauche(16)) return std::string();
        std::memcpy(g, d + p, 16); p += 16;
        char b[40];
        std::snprintf(b, sizeof b,
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
            g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
        return b;
    }
};

float HalbZuFloat(uint16_t h) {
    // IEEE 754 binary16 -> float, wie HalfUtils.Unpack.
    const uint32_t vorzeichen = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantisse = h & 0x3FF;
    uint32_t bits;
    if (exponent == 0) {
        if (mantisse == 0) { bits = vorzeichen; }
        else {
            // Subnormal: normalisieren.
            exponent = 127 - 15 + 1;
            while ((mantisse & 0x400) == 0) { mantisse <<= 1; --exponent; }
            mantisse &= 0x3FF;
            bits = vorzeichen | (exponent << 23) | (mantisse << 13);
        }
    } else if (exponent == 31) {
        bits = vorzeichen | 0x7F800000u | (mantisse << 13);
    } else {
        bits = vorzeichen | ((exponent - 15 + 127) << 23) | (mantisse << 13);
    }
    float f; std::memcpy(&f, &bits, 4); return f;
}

void LiesDecl(R& r, Decl& d) {
    d.elements.resize(kMaxElements);
    for (int i = 0; i < kMaxElements; ++i) {
        Element& e = d.elements[static_cast<size_t>(i)];
        e.usage = r.u8(); e.format = r.u8(); e.offset = r.u8(); e.streamIndex = r.u8();
        e.usageName = UsageName(e.usage);
        e.size = FormatVon(e.format, e.formatName).groesse;
    }
    d.streams.resize(kMaxStreams);
    for (int i = 0; i < kMaxStreams; ++i) {
        d.streams[static_cast<size_t>(i)].stride = r.u8();
        d.streams[static_cast<size_t>(i)].classification = r.u8();
    }
    d.elementCount = r.u8();
    d.streamCount = r.u8();
    r.ueberspringe(2);
}

void LiesSection(R& r, Section& s, int index) {
    s.index = index;
    r.i64(); r.i64();                       // zwei Zeiger, unbenutzt
    const int64_t stringOffset = r.i64();
    s.materialId = r.i32();
    r.u32();                                // lightMapUvMappingIndex
    s.primitiveCount = r.u32();
    s.startIndex = r.u32();
    s.vertexOffset = r.u32();
    s.vertexCount = r.u32();
    s.vertexStride = r.u8();
    s.primitiveType = r.u8();
    r.u8(); r.u8();
    r.u32(); r.u32(); r.u32();
    s.bonesPerVertex = r.u8();
    r.u8();
    const uint16_t boneCount = r.u16();
    const int64_t boneListOffset = r.i64();
    r.u64();
    for (int i = 0; i < kDeclCount; ++i) LiesDecl(r, s.decls[i]);
    for (int i = 0; i < 6; ++i) r.f32();    // texCoordRatios
    r.ueberspringe(kUnknownSectionBytes);

    const size_t merk = r.p;
    if (boneListOffset > 0) {
        r.setze(static_cast<size_t>(boneListOffset));
        s.boneList.clear();
        for (uint16_t i = 0; i < boneCount; ++i) s.boneList.push_back(r.u16());
    }
    s.materialName = r.cstrAn(stringOffset);
    r.setze(merk);
}

void LiesLod(R& r, Lod& lod, int& sectionZaehler) {
    lod.meshTypeId = r.u32();
    r.u32();                                 // maxInstances
    const uint32_t sectionCount = r.u32();
    const int64_t sectionOffset = r.i64();

    size_t merk = r.p;
    r.setze(static_cast<size_t>(sectionOffset));
    for (uint32_t i = 0; i < sectionCount && !r.bad; ++i) {
        Section s;
        LiesSection(r, s, sectionZaehler++);
        lod.sections.push_back(s);
    }
    r.setze(merk);

    for (int i = 0; i < kMaxCategories; ++i) {
        const int32_t anzahl = r.i32();
        const int64_t off = r.i64();
        merk = r.p;
        if (off > 0) {
            r.setze(static_cast<size_t>(off));
            for (int32_t k = 0; k < anzahl; ++k) r.u8();
        }
        r.setze(merk);
    }
    lod.flags = r.u32();
    lod.indexBufferFormat = r.i32();
    lod.indexBufferSize = r.u32();
    lod.vertexBufferSize = r.u32();
    r.i32();                                 // adjacencyBufferSize
    lod.chunkId = r.guid();
    r.u32();                                 // inlineDataOffset
    r.i64();                                 // adjacencyOffset
    const int64_t so1 = r.i64(), so2 = r.i64(), so3 = r.i64();
    lod.nameHash = r.u32();
    r.i64();
    if (lod.meshTypeId == 1) { r.u32(); r.i64(); }
    else if (lod.meshTypeId == 2) { r.i64(); }
    r.aufRunden(16);

    lod.shaderDebugName = r.cstrAn(so1);
    lod.name = r.cstrAn(so2);
    lod.shortName = r.cstrAn(so3);
}

} // namespace

bool LiesMeshSet(const std::vector<uint8_t>& daten, MeshSet& aus, std::string& fehler,
                 const uint8_t* resMeta) {
    if (daten.size() < 0x100) { fehler = "kein MeshSet: zu klein"; return false; }
    R r(daten);
    for (int i = 0; i < 6; ++i) r.f32();     // boundingBox: zwei Vec3 mit Fuellwort
    r.f32(); r.f32();
    int64_t lodOffsets[kMaxLodCount];
    for (int i = 0; i < kMaxLodCount; ++i) lodOffsets[i] = r.i64();
    r.i64();
    const int64_t fullnameOffset = r.i64();
    const int64_t nameOffset = r.i64();
    aus.nameHash = r.u32();
    aus.meshTypeId = r.u32();
    for (int i = 0; i < kMaxLodCount * 2; ++i) r.u16();
    aus.flags = r.u32();
    r.i8(); r.i8(); r.i16();
    const uint16_t lodCount = r.u16();
    aus.sectionCount = r.u16();

    // Grobpruefung, BEVOR Versaetzen gefolgt wird: die LOD-Versaetze muessen
    // aufsteigend in der Datei liegen und der erste hinter dem Kopf.
    if (lodCount == 0 || lodCount > kMaxLodCount) {
        char b[64]; std::snprintf(b, sizeof b, "kein MeshSet: lodCount %u", lodCount);
        fehler = b; return false;
    }
    int64_t vorher = 0;
    for (int i = 0; i < lodCount; ++i) {
        const int64_t o = lodOffsets[i];
        if (o < 0x80 || static_cast<size_t>(o) >= daten.size() || o <= vorher) {
            char b[96];
            std::snprintf(b, sizeof b, "kein MeshSet: lodOffset[%d]=%lld bei Groesse %zu",
                          i, static_cast<long long>(o), daten.size());
            fehler = b; return false;
        }
        vorher = o;
    }

    if (aus.meshTypeId != 0) {
        uint16_t a = r.u16(), b2 = r.u16();
        const uint16_t teile = (aus.meshTypeId == 1) ? b2 : a;
        if (aus.meshTypeId == 1) aus.kopfBoneCount = a;
        aus.kopfWerte[0] = a; aus.kopfWerte[1] = b2;
        if (a || b2) {
            const int64_t off1 = r.i64(), off2 = r.i64();
            const size_t merk = r.p;
            if (off1 > 0) {
                r.setze(static_cast<size_t>(off1));
                for (uint16_t i = 0; i < teile; ++i) {
                    if (aus.meshTypeId == 1) aus.kopfBoneListe.push_back(r.u16());
                    else {
                        // Composite (typ 2): je Teil eine LinearTransform, vier
                        // Zeilen zu je vier Floats (right, up, forward, trans).
                        std::array<float, 16> m{};
                        for (int k = 0; k < 16; ++k) m[static_cast<size_t>(k)] = r.f32();
                        aus.teilTransformen.push_back(m);
                    }
                }
            }
            if (off2 > 0) {
                r.setze(static_cast<size_t>(off2));
                for (uint16_t i = 0; i < teile; ++i) { for (int k = 0; k < 8; ++k) r.f32(); }
            }
            r.setze(merk);
        }
    }
    r.aufRunden(16);

    int zaehler = 0;
    for (int i = 0; i < lodCount; ++i) {
        if (r.p != static_cast<size_t>(lodOffsets[i])) {
            char b[96];
            std::snprintf(b, sizeof b, "LOD %d: Position %zu, laut Kopf %lld",
                          i, r.p, static_cast<long long>(lodOffsets[i]));
            aus.warnungen.push_back(b);
            r.setze(static_cast<size_t>(lodOffsets[i]));
        }
        Lod lod;
        LiesLod(r, lod, zaehler);
        aus.lods.push_back(lod);
    }
    aus.fullname = r.cstrAn(fullnameOffset);
    aus.name = r.cstrAn(nameOffset);
    if (r.bad) { fehler = "MeshSet endet mitten in den Daten"; return false; }

    if (resMeta != nullptr) {
        const uint32_t off = static_cast<uint32_t>(resMeta[0]) |
                             (static_cast<uint32_t>(resMeta[1]) << 8) |
                             (static_cast<uint32_t>(resMeta[2]) << 16) |
                             (static_cast<uint32_t>(resMeta[3]) << 24);
        const uint32_t laenge = static_cast<uint32_t>(resMeta[4]) |
                                (static_cast<uint32_t>(resMeta[5]) << 8) |
                                (static_cast<uint32_t>(resMeta[6]) << 16) |
                                (static_cast<uint32_t>(resMeta[7]) << 24);
        if (off && laenge && static_cast<size_t>(off) + laenge <= daten.size()) {
            aus.inlineDaten.assign(daten.begin() + off, daten.begin() + off + laenge);
        }
    }
    return true;
}

// ------------------------------------------------------------
size_t LodVersatz(const MeshSet& ms, size_t lodIndex, size_t pufferLaenge) {
    auto aufgerundet = [](size_t v) { return (v + kLodAusrichtung - 1) / kLodAusrichtung * kLodAusrichtung; };
    size_t gesamt = 0;
    for (const Lod& l : ms.lods) gesamt += aufgerundet(l.vertexBufferSize + l.indexBufferSize);
    if (pufferLaenge < gesamt) return 0;      // je LOD ein eigener Chunk
    size_t p = 0;
    for (size_t i = 0; i < lodIndex && i < ms.lods.size(); ++i) {
        p += aufgerundet(ms.lods[i].vertexBufferSize + ms.lods[i].indexBufferSize);
    }
    return p;
}

// ------------------------------------------------------------
namespace {

std::vector<double> Entschluessle(const std::string& fmt, const uint8_t* roh, int groesse) {
    std::vector<double> aus;
    auto f32 = [&](int i) { float f; std::memcpy(&f, roh + i * 4, 4); return static_cast<double>(f); };
    auto u16 = [&](int i) { return static_cast<uint16_t>(roh[i*2] | (roh[i*2+1] << 8)); };
    auto i16 = [&](int i) { return static_cast<int16_t>(u16(i)); };
    auto u32 = [&](int i) {
        return static_cast<uint32_t>(roh[i*4]) | (static_cast<uint32_t>(roh[i*4+1]) << 8) |
               (static_cast<uint32_t>(roh[i*4+2]) << 16) | (static_cast<uint32_t>(roh[i*4+3]) << 24);
    };

    if (fmt == "Float")  { aus.push_back(f32(0)); return aus; }
    if (fmt == "Float2") { for (int i = 0; i < 2; ++i) aus.push_back(f32(i)); return aus; }
    if (fmt == "Float3") { for (int i = 0; i < 3; ++i) aus.push_back(f32(i)); return aus; }
    if (fmt == "Float4") { for (int i = 0; i < 4; ++i) aus.push_back(f32(i)); return aus; }
    if (fmt.rfind("Half", 0) == 0) {
        const int n = (fmt == "Half") ? 1 : (fmt == "Half2") ? 2 : (fmt == "Half3") ? 3 : 4;
        for (int i = 0; i < n; ++i) aus.push_back(static_cast<double>(HalbZuFloat(u16(i))));
        return aus;
    }
    if (fmt == "Byte4" || fmt == "Byte4N") {
        for (int i = 0; i < 4; ++i) {
            const double v = static_cast<double>(static_cast<int8_t>(roh[i]));
            aus.push_back(fmt.back() == 'N' ? v / 127.0 : v);
        }
        return aus;
    }
    if (fmt == "UByte4" || fmt == "UByte4N") {
        for (int i = 0; i < 4; ++i) {
            const double v = static_cast<double>(roh[i]);
            aus.push_back(fmt.back() == 'N' ? v / 255.0 : v);
        }
        return aus;
    }
    if (fmt == "UByteN") { aus.push_back(static_cast<double>(roh[0]) / 255.0); return aus; }
    if (fmt.rfind("Short", 0) == 0) {
        const int n = std::max(1, groesse / 2);
        for (int i = 0; i < n; ++i) {
            const double v = static_cast<double>(i16(i));
            aus.push_back(fmt.back() == 'N' ? v / 32767.0 : v);
        }
        return aus;
    }
    if (fmt.rfind("UShort", 0) == 0) {
        const int n = groesse / 2;
        for (int i = 0; i < n; ++i) {
            const double v = static_cast<double>(u16(i));
            aus.push_back(fmt.back() == 'N' ? v / 65535.0 : v);
        }
        return aus;
    }
    if (fmt.rfind("Int", 0) == 0) {
        const int n = groesse / 4;
        for (int i = 0; i < n; ++i) {
            const double v = static_cast<double>(static_cast<int32_t>(u32(i)));
            aus.push_back(fmt.back() == 'N' ? v / 2147483647.0 : v);
        }
        return aus;
    }
    if (fmt.rfind("UInt", 0) == 0) {
        const int n = groesse / 4;
        for (int i = 0; i < n; ++i) {
            const double v = static_cast<double>(u32(i));
            aus.push_back(fmt.back() == 'N' ? v / 4294967295.0 : v);
        }
        return aus;
    }
    if (fmt.rfind("Comp3", 0) == 0 || fmt.rfind("UComp3", 0) == 0 ||
        fmt.rfind("Comp4", 0) == 0 || fmt.rfind("UComp4", 0) == 0) {
        const uint32_t wert = u32(0);
        const bool vorzeichen = (fmt[0] != 'U');
        const bool normiert = (fmt.find("N_") != std::string::npos) || fmt.back() == 'N';
        int breiten[4]; int anzahl;
        if (fmt.find("11_11_10") != std::string::npos) { breiten[0]=11; breiten[1]=11; breiten[2]=10; anzahl=3; }
        else if (fmt.find("10_10_10_2") != std::string::npos) { breiten[0]=10; breiten[1]=10; breiten[2]=10; breiten[3]=2; anzahl=4; }
        else { breiten[0]=10; breiten[1]=10; breiten[2]=10; anzahl=3; }
        int schub = 0;
        for (int i = 0; i < anzahl; ++i) {
            const int w = breiten[i];
            int64_t x = (wert >> schub) & ((1u << w) - 1u);
            if (vorzeichen && x >= (1LL << (w - 1))) x -= (1LL << w);
            double f = static_cast<double>(x);
            if (normiert) {
                const double teiler = vorzeichen ? static_cast<double>((1LL << (w - 1)) - 1)
                                                 : static_cast<double>((1LL << w) - 1);
                f = static_cast<double>(x) / teiler;
            }
            aus.push_back(f);
            schub += w;
        }
        return aus;
    }
    for (int i = 0; i < groesse; ++i) aus.push_back(static_cast<double>(roh[i]));
    return aus;
}

} // namespace

bool LiesVertices(const Section& s, const std::vector<uint8_t>& vertexPuffer,
                  int decl, std::vector<Vertex>& aus, size_t maxVertices) {
    if (decl < 0 || decl >= kDeclCount) return false;
    const Decl& d = s.decls[decl];
    const size_t n = maxVertices ? std::min<size_t>(s.vertexCount, maxVertices) : s.vertexCount;
    aus.assign(n, Vertex());

    size_t gesamt = 0;
    for (int j = 0; j < kMaxStreams; ++j) {
        const uint8_t stride = d.streams[static_cast<size_t>(j)].stride;
        if (stride == 0) continue;
        const size_t block = s.vertexOffset + gesamt * s.vertexCount;
        for (size_t i = 0; i < n; ++i) {
            const size_t p = block + i * stride;
            for (const Element& e : d.elements) {
                if (e.usage == 0 || e.streamIndex != j) continue;
                // Der Versatz steht IM Element - nicht die Summe der Groessen
                // mitzaehlen, die Elemente folgen nicht immer lueckenlos.
                if (e.size == 0 || static_cast<int>(e.offset) + e.size > stride) continue;
                const size_t q = p + e.offset;
                if (q + static_cast<size_t>(e.size) > vertexPuffer.size()) continue;
                std::string schluessel = e.usageName;
                if (aus[i].find(schluessel) != aus[i].end()) {
                    char b[64];
                    std::snprintf(b, sizeof b, "%s#%d", e.usageName.c_str(), j);
                    schluessel = b;
                }
                aus[i][schluessel] = Entschluessle(e.formatName, vertexPuffer.data() + q, e.size);
            }
        }
        gesamt += stride;
    }
    return true;
}

Bewertung Bewerte(const std::vector<Vertex>& verts, double grenze) {
    Bewertung n;
    n.anzahl = verts.size();
    if (verts.empty()) return n;

    auto endlich = [&](const std::vector<double>& w) {
        for (double x : w) if (!std::isfinite(x) || std::fabs(x) > grenze) return false;
        return true;
    };
    auto uvOk = [](const std::vector<double>& w) {
        for (double x : w) if (!std::isfinite(x) || std::fabs(x) > 32.0) return false;
        return true;
    };

    size_t gutPos = 0, gesamtPos = 0, gutUv = 0, gesamtUv = 0, gutGew = 0, gesamtGew = 0;
    for (const Vertex& v : verts) {
        const auto p = v.find("Pos");
        if (p != v.end()) { ++gesamtPos; if (endlich(p->second)) ++gutPos; }
        const auto u = v.find("TexCoord0");
        if (u != v.end()) { ++gesamtUv; if (uvOk(u->second)) ++gutUv; }
        const auto w = v.find("BoneWeights");
        if (w != v.end()) {
            ++gesamtGew;
            double summe = 0.0;
            for (double x : w->second) summe += x;
            // Die Gewichtssumme geht ueber BEIDE Saetze: Sections mit
            // bonesPerVertex 8 tragen BoneWeights UND BoneWeights2.
            const auto w2 = v.find("BoneWeights2");
            if (w2 != v.end()) for (double x : w2->second) summe += x;
            if (std::fabs(summe - 1.0) <= 0.02) ++gutGew;
        }
    }
    if (gesamtPos) n.pos = static_cast<double>(gutPos) / static_cast<double>(gesamtPos);
    if (gesamtUv)  n.uv = static_cast<double>(gutUv) / static_cast<double>(gesamtUv);
    if (gesamtGew) n.gewichte = static_cast<double>(gutGew) / static_cast<double>(gesamtGew);

    double summe = 0.0; int teile = 0;
    if (n.pos >= 0) { summe += n.pos; ++teile; }
    if (n.gewichte >= 0) { summe += n.gewichte; ++teile; }
    if (n.uv >= 0) { summe += n.uv; ++teile; }
    n.punkte = teile ? summe / teile : 0.0;
    return n;
}

bool LiesVerticesGemessen(const Section& s, const std::vector<uint8_t>& vertexPuffer,
                          std::vector<Vertex>& aus, int& decl, Bewertung& note,
                          size_t maxVertices) {
    bool haben = false;
    for (int d = 0; d < kDeclCount; ++d) {
        std::vector<Vertex> v;
        if (!LiesVertices(s, vertexPuffer, d, v, maxVertices)) continue;
        const Bewertung b = Bewerte(v);
        if (!haben || b.punkte > note.punkte) {
            aus = std::move(v); decl = d; note = b; haben = true;
        }
    }
    if (!haben) { aus.clear(); decl = 0; note = Bewertung(); }
    return haben;
}

// ------------------------------------------------------------
int IndexBreite(const Lod& lod) {
    if (lod.indexBufferFormat == kRenderFormatR16Uint) return 16;
    if (lod.indexBufferFormat == kRenderFormatR32Uint) return 32;
    uint64_t tris = 0;
    for (const Section& s : lod.sections) tris += s.primitiveCount;
    if (tris == 0) return 16;
    for (int bits : { 16, 32 }) {
        const uint64_t noetig = tris * 3 * static_cast<uint64_t>(bits / 8);
        if (lod.indexBufferSize >= noetig && lod.indexBufferSize < noetig + 64) return bits;
    }
    return (lod.indexBufferSize <= tris * 3 * 2 + 64) ? 16 : 32;
}

bool LiesIndices(const Lod& lod, const std::vector<uint8_t>& puffer, const Section& s,
                 std::vector<uint32_t>& aus, std::string& fehler) {
    const int bits = IndexBreite(lod);
    const size_t schritt = static_cast<size_t>(bits / 8);
    const size_t start = lod.vertexBufferSize + static_cast<size_t>(s.startIndex) * schritt;
    const size_t n = static_cast<size_t>(s.primitiveCount) * 3;
    if (start + n * schritt > puffer.size()) {
        char b[96];
        std::snprintf(b, sizeof b, "Indexpuffer zu kurz: brauche %zu, habe %zu",
                      start + n * schritt, puffer.size());
        fehler = b;
        return false;
    }
    aus.clear();
    aus.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t q = start + i * schritt;
        if (bits == 16) {
            aus.push_back(static_cast<uint32_t>(puffer[q] | (puffer[q+1] << 8)));
        } else {
            aus.push_back(static_cast<uint32_t>(puffer[q]) | (static_cast<uint32_t>(puffer[q+1]) << 8) |
                          (static_cast<uint32_t>(puffer[q+2]) << 16) | (static_cast<uint32_t>(puffer[q+3]) << 24));
        }
    }
    return true;
}

bool IstTiefenSection(const Section& s) {
    if (!s.materialName.empty()) return false;
    for (const Element& e : s.decls[0].elements) {
        if (e.usage == 0) continue;
        if (e.usageName == "TexCoord0" || e.usageName == "Normal") return false;
    }
    return true;
}

bool IstSchattengeometrie(const Section& s, const std::vector<Vertex>& verts) {
    if (!s.materialName.empty()) return false;
    for (const Vertex& v : verts) {
        for (const auto& kv : v) {
            if (kv.first.find("Normal") != std::string::npos) return false;
        }
        break;                      // ein Vertex genuegt, alle sind gleich gebaut
    }
    return true;
}

// ------------------------------------------------------------
//  Textfassung fuer die Gegenprobe. Kein huebsches Protokoll -
//  jede Zeile ist so gebaut, dass Python sie Zeichen fuer
//  Zeichen genauso erzeugen kann.
// ------------------------------------------------------------
std::string AlsText(const MeshSet& ms) {
    std::string aus;
    char b[512];
    std::snprintf(b, sizeof b, "MESHSET %s\n", ms.name.c_str()); aus += b;
    std::snprintf(b, sizeof b, "FULLNAME %s\n", ms.fullname.c_str()); aus += b;
    std::snprintf(b, sizeof b, "HASH %08x TYP %u FLAGS %u SECTIONS %u LODS %zu\n",
                  ms.nameHash, ms.meshTypeId, ms.flags, ms.sectionCount, ms.lods.size());
    aus += b;
    for (const std::string& w : ms.warnungen) { aus += "WARNUNG "; aus += w; aus += "\n"; }

    for (size_t li = 0; li < ms.lods.size(); ++li) {
        const Lod& l = ms.lods[li];
        std::snprintf(b, sizeof b,
            "LOD %zu typ=%u flags=%u idxfmt=%d idxsize=%u vtxsize=%u hash=%08x sections=%zu\n",
            li, l.meshTypeId, l.flags, l.indexBufferFormat, l.indexBufferSize,
            l.vertexBufferSize, l.nameHash, l.sections.size());
        aus += b;
        std::snprintf(b, sizeof b, "  NAME %s | %s | %s\n",
                      l.name.c_str(), l.shortName.c_str(), l.shaderDebugName.c_str());
        aus += b;
        std::snprintf(b, sizeof b, "  CHUNK %s BITS %d\n", l.chunkId.c_str(), IndexBreite(l));
        aus += b;
        for (const Section& s : l.sections) {
            std::snprintf(b, sizeof b,
                "  S %d mat=%d name=%s prim=%u start=%u vtxoff=%u vtx=%u stride=%u typ=%u bpv=%u bones=%zu tiefe=%d\n",
                s.index, s.materialId, s.materialName.c_str(), s.primitiveCount, s.startIndex,
                s.vertexOffset, s.vertexCount, s.vertexStride, s.primitiveType,
                s.bonesPerVertex, s.boneList.size(), IstTiefenSection(s) ? 1 : 0);
            aus += b;
            for (int d = 0; d < kDeclCount; ++d) {
                const Decl& dd = s.decls[d];
                std::snprintf(b, sizeof b, "    DECL %d elem=%u stream=%u\n",
                              d, dd.elementCount, dd.streamCount);
                aus += b;
                for (int j = 0; j < kMaxStreams; ++j) {
                    if (dd.streams[static_cast<size_t>(j)].stride == 0) continue;
                    std::snprintf(b, sizeof b, "      STREAM %d stride=%u class=%u\n",
                                  j, dd.streams[static_cast<size_t>(j)].stride,
                                  dd.streams[static_cast<size_t>(j)].classification);
                    aus += b;
                }
                for (const Element& e : dd.elements) {
                    if (e.usage == 0) continue;
                    std::snprintf(b, sizeof b, "      E %s %s off=%u stream=%u size=%d\n",
                                  e.usageName.c_str(), e.formatName.c_str(), e.offset,
                                  e.streamIndex, e.size);
                    aus += b;
                }
            }
        }
    }
    return aus;
}

} // namespace fbmesh
