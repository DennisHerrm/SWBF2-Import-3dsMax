// ============================================================
//  fbindex.cpp - Index ueber alle Bundles.
// ============================================================
#include "fbindex.h"
#include "fbdatei.h"

#include <cstdio>
#include <cstring>

namespace fbindex {

const std::vector<std::string>& GeteilteBundles() {
    // Aus ProfileCreator.cs, SWBF2-Profil: die Bundles, deren Hash Frosty
    // einen Namen zuordnet. Reihenfolge und Schreibweise unveraendert -
    // der Hash haengt an jedem Zeichen.
    static const std::vector<std::string> liste = {
        "win32/gameplay/bundles/sharedbundles/frontend+mp/abilities/sharedbundleabilities_frontend+mp",
        "win32/gameplay/bundles/sharedbundles/common/animation/sharedbundleanimation_common",
        "win32/gameplay/bundles/sharedbundles/frontend+mp/characters/sharedbundlecharacters_frontend+mp",
        "win32/gameplay/bundles/sharedbundles/common/vehicles/sharedbundlevehiclescockpits",
        "win32/gameplay/bundles/sharedbundles/common/characters/sharedbundlecharacters1p",
        "win32/ui/frontend/webbrowser/webbrowserresourcebundle",
        "win32/systems/frostbitestartupdata",
        "win32/gameplay/wrgameconfiguration",
        "win32/default_settings",
        "win32/s1/gameplay/bundles/sharedbundleseason1",
        "win32/gameplay/bundles/sp/vehicle/sharedbundle_sp_vehicle",
        "win32/gameplay/bundles/sp/sharedbundle_sp",
        "win32/gameplay/bundles/sp/player/sharedbundle_sp_player",
        "win32/gameplay/bundles/sp/droid/sharedbundle_sp_droid",
        "win32/gameplay/bundles/sp/buddy/sharedbundle_sp_buddy",
        "win32/gameplay/bundles/sharedbundles/sp/vehicles/sharedbundlevehicles_sp",
        "win32/gameplay/bundles/sharedbundles/sp/abilities/sharedbundleabilities_sp",
        "win32/a3/gameplay/bundles/sp/vehicle/sharedbundle_sp_vehicle_a3",
        "win32/a3/gameplay/bundles/sp/sharedbundle_sp_a3",
        "win32/a3/gameplay/bundles/sp/player/sharedbundle_sp_player_a3",
        "win32/a3/gameplay/bundles/sp/buddy/sharedbundle_sp_buddy_a3",
        "win32/ui/static",
        "win32/loadingscreens_bundle",
        "win32/gameplay/bundles/sharedbundles/common/weapons/sharedbundleweapons_common",
        "win32/persistence/wsmppersistence"
    };
    return liste;
}

std::string ResTypName(uint32_t typ) {
    // Aus FrostySdk/Managers/Entries/ResAssetEntry.cs - die Typen, die SWBF2
    // benutzt. Alles andere kommt als Hexzahl zurueck, damit ein unbekannter
    // Typ auffaellt statt still durchzurutschen.
    static const std::map<uint32_t, const char*> tabelle = {
        { 0x0DEAFE10, "IesResource" },
        { 0x15E1F32E, "TerrainDecals" },
        { 0x1CA38E06, "VisualTerrain" },
        { 0x22FE8AC8, "TerrainStreamingTree" },
        { 0x30B4A553, "OccluderMesh" },
        { 0x41D57E10, "RenderTexture" },
        { 0x49B156D4, "MeshSet" },
        { 0x4B803D3B, "PathfindingRuntimeResource" },
        { 0x50E8E7EE, "Dx12NvRvmDatabase" },
        { 0x51A3C853, "AssetBank" },
        { 0x6B4B6E85, "Dx12PcRvmDatabase" },
        { 0x6BDE20BA, "Texture" },
        { 0x70C5CB3E, "EnlightenDatabase" },
        { 0x78791C75, "EmitterGraphResource" },
        { 0x7DD4CC89, "SerializedExpressionNodeGraph" },
        { 0x85548684, "CompressedClipData" },
        { 0x85AC783D, "EAClothAssetData" },
        { 0x85EA8656, "EAClothEntityData" },
        { 0x89983F10, "SvgImage" },
        { 0x8DA16895, "Dx11RvmDatabase" },
        { 0x91043F65, "HavokPhysicsData" },
        { 0x957C32B1, "AtlasTexture" },
        { 0x9C4FAA17, "HeightfieldDecal" },
        { 0x9D00966A, "UITtfFontFile" },
        { 0xA23E75DB, "TerrainLayerCombinations" },
        { 0xB15AD3FD, "EnlightenShaderDatabaseResource" },
        { 0xB2C465F6, "NewWaveResource" },
        { 0xC611F34A, "MeshEmitterResource" },
        { 0xC6CD3286, "EnlightenStaticDatabase" },
        { 0xD070EED1, "AnimTrackData" },
        { 0xD8F5DAAF, "ShaderBlockDepot" },
        { 0xEC1B7BF4, "AntResource" },
        { 0xEFC70728, "ZoneStreamerGrid" },
        { 0xF04F0C81, "Dx11ShaderProgramDatabase" },
        { 0xF7CC814D, "Dx11NvRvmDatabase" }
    };
    const auto it = tabelle.find(typ);
    if (it != tabelle.end()) return it->second;
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", typ);
    return b;
}

namespace {

std::string GuidText(const uint8_t g[16]) {
    // Dieselbe Schreibweise wie fbtools: 8-4-4-4-12, die ersten drei
    // Gruppen klein-endig.
    char b[40];
    std::snprintf(b, sizeof b,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
        g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return b;
}

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

} // namespace

bool BaueIndex(fbgame::Spiel& spiel, Index& aus, std::string& fehler, Fortschritt* fs) {
    std::map<uint32_t, std::string> geteilt;
    for (const std::string& n : GeteilteBundles()) geteilt[fbbundle::Fnv1(n)] = n;

    if (fs != nullptr) { fs->gesamt = spiel.Bundles().size(); fs->fertig = 0; }
    for (size_t bi = 0; bi < spiel.Bundles().size(); ++bi) {
        if (fs != nullptr) {
            if (fs->abbrechen.load()) { fehler = "abgebrochen"; return false; }
            fs->fertig = bi;
        }
        const fbgame::ManifestBundle& mb = spiel.Bundles()[bi];
        std::vector<uint8_t> roh;
        if (!spiel.LiesBundleRoh(mb, roh, fehler)) continue;
        fbbundle::Bundle bu;
        if (!fbbundle::LiesBundle(roh, bu, fehler)) continue;

        std::string name, woher;
        auto it = geteilt.find(mb.hash);
        if (it != geteilt.end()) { name = it->second; woher = "shared"; }
        if (name.empty()) {
            for (const fbbundle::EbxRef& e : bu.ebx) {
                const std::string kand = "win32/" + e.name;
                if (fbbundle::Fnv1(kand) == mb.hash) { name = kand; woher = "ebx"; break; }
                const std::string klein = Klein(kand);
                if (fbbundle::Fnv1(klein) == mb.hash) { name = klein; woher = "ebx-lower"; break; }
            }
        }
        if (name.empty()) {
            char b[16];
            std::snprintf(b, sizeof b, "%08x", mb.hash);
            name = b;
            woher = "hash";
        }
        aus.namensherkunft[woher] += 1;

        BundleInfo bin;
        bin.nummer = bi;
        bin.hash = mb.hash;
        bin.name = name;
        bin.woher = woher;
        bin.ebx = bu.ebx.size();
        bin.res = bu.res.size();
        bin.chunks = bu.chunks.size();
        aus.bundles.push_back(bin);

        for (const fbbundle::EbxRef& e : bu.ebx) {
            Eintrag& d = aus.ebx[e.name];
            if (d.name.empty()) {
                d.name = e.name; d.sha1 = e.sha1; d.originalSize = e.originalSize;
            }
            d.bundles.push_back(bi);
        }
        for (const fbbundle::ResRef& r : bu.res) {
            Eintrag& d = aus.res[r.name];
            if (d.name.empty()) {
                d.name = r.name; d.sha1 = r.sha1; d.originalSize = r.originalSize;
                d.resType = r.resType; d.resRid = r.resRid;
                std::memcpy(d.resMeta, r.resMeta, 16);
            }
            d.bundles.push_back(bi);
        }
        for (const fbbundle::ChunkRef& c : bu.chunks) {
            const std::string id = GuidText(c.guid);
            Eintrag& d = aus.chunks[id];
            if (d.name.empty()) {
                d.name = id; d.sha1 = c.sha1; d.originalSize = c.logicalSize;
            }
            d.bundles.push_back(bi);
        }
    }

    // Chunks aus dem Manifest, die in keinem Bundle stehen.
    for (const fbgame::ManifestChunk& mc : spiel.Chunks()) {
        const std::string id = GuidText(mc.guid);
        Eintrag& d = aus.chunks[id];
        if (d.name.empty()) { d.name = id; }
    }

    fehler.clear();
    return true;
}

// ------------------------------------------------------------
namespace {

constexpr char kIdxMagic[8] = { 'F','B','I','D','X','0','0','1' };

void PackU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v)); b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v >> 16)); b.push_back(static_cast<uint8_t>(v >> 24));
}
void PackU64(std::vector<uint8_t>& b, uint64_t v) {
    PackU32(b, static_cast<uint32_t>(v)); PackU32(b, static_cast<uint32_t>(v >> 32));
}
void PackText(std::vector<uint8_t>& b, const std::string& s) {
    PackU32(b, static_cast<uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

struct Aus {
    const uint8_t* d; size_t n; size_t p = 0; bool bad = false;
    bool brauche(size_t k) { if (bad || p > n || k > n - p) { bad = true; return false; } return true; }   // ueberlaufsicher
    uint32_t u32() {
        if (!brauche(4)) return 0;
        const uint32_t v = static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p+1]) << 8) |
                           (static_cast<uint32_t>(d[p+2]) << 16) | (static_cast<uint32_t>(d[p+3]) << 24);
        p += 4; return v;
    }
    uint64_t u64() { const uint32_t a = u32(), b = u32(); return (static_cast<uint64_t>(b) << 32) | a; }
    std::string text() {
        const uint32_t k = u32();
        if (!brauche(k)) return std::string();
        std::string s(reinterpret_cast<const char*>(d + p), k);
        p += k; return s;
    }
    void roh(uint8_t* ziel, size_t k) { if (brauche(k)) { std::memcpy(ziel, d + p, k); p += k; } }
};

void PackEintrag(std::vector<uint8_t>& b, const std::string& schluessel, const Eintrag& e) {
    PackText(b, schluessel);
    PackText(b, e.name);
    PackText(b, e.sha1);
    PackU32(b, e.originalSize);
    PackU32(b, e.resType);
    PackU64(b, e.resRid);
    b.insert(b.end(), e.resMeta, e.resMeta + 16);
    PackU32(b, static_cast<uint32_t>(e.bundles.size()));
    for (size_t bi : e.bundles) PackU32(b, static_cast<uint32_t>(bi));
}

void LiesEintrag(Aus& r, std::map<std::string, Eintrag>& ziel) {
    const std::string schluessel = r.text();
    Eintrag e;
    e.name = r.text();
    e.sha1 = r.text();
    e.originalSize = r.u32();
    e.resType = r.u32();
    e.resRid = r.u64();
    r.roh(e.resMeta, 16);
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && !r.bad; ++i) e.bundles.push_back(r.u32());
    if (!r.bad) ziel[schluessel] = std::move(e);
}

} // namespace

bool SpeichereIndex(const Index& idx, const std::string& pfad, int64_t kopfnummer,
                    std::string& fehler) {
    std::vector<uint8_t> b;
    b.insert(b.end(), kIdxMagic, kIdxMagic + 8);
    PackU64(b, static_cast<uint64_t>(kopfnummer));
    PackU32(b, static_cast<uint32_t>(idx.bundles.size()));
    PackU32(b, static_cast<uint32_t>(idx.ebx.size()));
    PackU32(b, static_cast<uint32_t>(idx.res.size()));
    PackU32(b, static_cast<uint32_t>(idx.chunks.size()));
    PackU32(b, static_cast<uint32_t>(idx.namensherkunft.size()));
    for (const auto& kv : idx.namensherkunft) { PackText(b, kv.first); PackU32(b, static_cast<uint32_t>(kv.second)); }
    for (const BundleInfo& bi : idx.bundles) {
        PackU64(b, bi.nummer); PackU32(b, bi.hash);
        PackText(b, bi.name); PackText(b, bi.woher);
        PackU64(b, bi.ebx); PackU64(b, bi.res); PackU64(b, bi.chunks);
    }
    for (const auto& kv : idx.ebx) PackEintrag(b, kv.first, kv.second);
    for (const auto& kv : idx.res) PackEintrag(b, kv.first, kv.second);
    for (const auto& kv : idx.chunks) PackEintrag(b, kv.first, kv.second);

    // fwrite UND fclose werden geprueft: eine halb geschriebene Ablage (Platte
    // voll) meldet sich jetzt hier, statt beim naechsten Laden aufzufallen.
    return fbdatei::SchreibeAlles(pfad, b, fehler);
}

bool LadeIndex(Index& aus, const std::string& pfad, int64_t kopfnummer, std::string& fehler) {
    if (!fbdatei::Existiert(pfad)) { fehler = "kein abgelegter Index"; return false; }
    std::vector<uint8_t> b;
    std::string grund;
    if (!fbdatei::LiesAlles(pfad, b, grund)) { fehler = "Index unvollstaendig (" + grund + ")"; return false; }
    if (b.size() < 16 || std::memcmp(b.data(), kIdxMagic, 8) != 0) {
        fehler = "abgelegter Index hat die falsche Kennung";
        return false;
    }
    Aus r{ b.data(), b.size(), 8, false };
    const int64_t alt = static_cast<int64_t>(r.u64());
    if (alt != kopfnummer) {
        fehler = "abgelegter Index gehoert zu einem anderen Spielstand";
        return false;
    }
    const uint32_t nb = r.u32(), ne = r.u32(), nr = r.u32(), nc = r.u32(), nh = r.u32();
    for (uint32_t i = 0; i < nh && !r.bad; ++i) {
        const std::string k = r.text();
        aus.namensherkunft[k] = static_cast<int>(r.u32());
    }
    for (uint32_t i = 0; i < nb && !r.bad; ++i) {
        BundleInfo bi;
        bi.nummer = static_cast<size_t>(r.u64());
        bi.hash = r.u32();
        bi.name = r.text();
        bi.woher = r.text();
        bi.ebx = static_cast<size_t>(r.u64());
        bi.res = static_cast<size_t>(r.u64());
        bi.chunks = static_cast<size_t>(r.u64());
        aus.bundles.push_back(bi);
    }
    for (uint32_t i = 0; i < ne && !r.bad; ++i) LiesEintrag(r, aus.ebx);
    for (uint32_t i = 0; i < nr && !r.bad; ++i) LiesEintrag(r, aus.res);
    for (uint32_t i = 0; i < nc && !r.bad; ++i) LiesEintrag(r, aus.chunks);
    if (r.bad) { fehler = "abgelegter Index endet mitten in den Daten"; return false; }
    return true;
}

} // namespace fbindex
