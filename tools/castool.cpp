// ============================================================
//  castool - die Containerschicht von aussen sichtbar machen.
//
//    castool <cas.cat>                     Eintraege auflisten
//    castool <cas.cat> --entpacke <n> <ziel> [woerterbuch]
//
//  Die Ausgabe ist genau das Format, gegen das die
//  Python-Referenz gehalten wird.
// ============================================================
#include "fbcas.h"
#include "fbdb.h"
#include "fbgame.h"
#include "fbbundle.h"
#include "fbauswahl.h"
#include "fbanim.h"
#include "fbfahrzeug.h"
#include "fbgesicht.h"
#include "fbgd.h"
#include "fbgdwerte.h"
#include <filesystem>
#include <cctype>
#include <cmath>
#include <set>
#include <functional>
#include "fbbeipack.h"
#include "fbmaterial.h"
#include <memory>
#include <map>
#include "fbmeshset.h"
#include "fbebx.h"
#include "fbdatei.h"
#include "fbindex.h"
#include "fbfigur.h"
#include <chrono>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

static std::string OrdnerVon(const std::string& p) {
    const size_t i = p.find_last_of("/\\");
    return (i == std::string::npos) ? std::string(".") : p.substr(0, i);
}


// ============================================================
//  --material: Materialien und Texturen einer Figur MESSEN.
//
//  Vorarbeit fuer Stufe 4. Gedruckt wird, was fuer die Zuordnung
//  Section -> Material -> Textur gebraucht wird, nach Frostys eigener
//  Kette (FrostyMeshSetEditor.cs):
//      MeshAsset.Materials[section.MaterialId] -> Shader.TextureParameters
//      -> ParameterName + Value (Verweis auf die TextureAsset-EBX);
//      sind die TextureParameters leer, kommen sie aus der
//      MeshVariationDatabase (MaterialCollection der Variation).
//  Dazu je Textur-RES der Kopf nach Frostys Texture.cs (SWBF2-Zweig)
//  mit Gegenprobe: Summe der Mip-Groessen gegen ChunkSize und gegen
//  den Chunk im Index, Mip 0 gegen Breite x Hoehe je Format.
// ============================================================
namespace {

std::string MKlein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string MHex(const uint8_t* p, size_t n) {
    static const char* z = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        if (i) s += ' ';
        s += z[p[i] >> 4];
        s += z[p[i] & 15];
    }
    return s;
}

// Dieselbe Schreibweise wie der Index (fbindex.cpp): 8-4-4-4-12, die
// ersten drei Gruppen klein-endig.
std::string MGuid(const uint8_t g[16]) {
    char b[40];
    std::snprintf(b, sizeof b,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
        g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return b;
}

void MZeige(const fbebx::Datei& d, const fbebx::Wert& w, const std::string& name, int tiefe,
            const std::map<std::string, std::string>& guidName, std::string& aus, int& zeilen) {
    using fbebx::Art;
    if (zeilen > 6000) return;
    ++zeilen;
    aus.append(static_cast<size_t>(tiefe) * 2, ' ');
    char b[256];
    switch (w.art) {
    case Art::Objekt:
        aus += "OBJ " + name + " " + w.typ + "\n";
        for (const fbebx::Feld& f : w.felder) if (f.wert) MZeige(d, *f.wert, f.name, tiefe + 1, guidName, aus, zeilen);
        break;
    case Art::Liste:
        std::snprintf(b, sizeof b, "LISTE %s [%zu]\n", name.c_str(), w.liste.size());
        aus += b;
        for (size_t i = 0; i < w.liste.size() && i < 64; ++i) {
            if (w.liste[i]) MZeige(d, *w.liste[i], "[" + std::to_string(i) + "]", tiefe + 1, guidName, aus, zeilen);
        }
        break;
    case Art::Zeiger: {
        const auto& o = d.Objekte();
        if (w.verweis >= 0 && static_cast<size_t>(w.verweis) < o.size() && o[static_cast<size_t>(w.verweis)] && tiefe < 14) {
            const fbebx::Wert& z = *o[static_cast<size_t>(w.verweis)];
            aus += "REF " + name + " -> " + z.typ + "\n";
            for (const fbebx::Feld& f : z.felder) if (f.wert) MZeige(d, *f.wert, f.name, tiefe + 1, guidName, aus, zeilen);
        } else {
            aus += "REF " + name + " " + std::to_string(w.verweis) + "\n";
        }
        break;
    }
    case Art::Import: {
        const auto it = guidName.find(MKlein(w.text));
        aus += "IMPORT " + name + " " + w.text + " " + w.typ +
               (it != guidName.end() ? "  -> " + it->second : std::string("  -> (nicht im Bundle)")) + "\n";
        break;
    }
    case Art::Bool:  aus += "BOOL " + name + (w.wahr ? " 1\n" : " 0\n"); break;
    case Art::Ganz:  aus += "GANZ " + name + " " + std::to_string(w.zahl) + "\n"; break;
    case Art::Gleit: std::snprintf(b, sizeof b, "GLEIT %s %.6f\n", name.c_str(), w.gleit); aus += b; break;
    case Art::Text:  aus += "TEXT " + name + " " + w.text + "\n"; break;
    case Art::Guid:  aus += "GUID " + name + " " + w.text + "\n"; break;
    case Art::Sha1:  aus += "SHA1 " + name + " " + w.text + "\n"; break;
    case Art::Boxed: aus += "BOXED " + name + " " + std::to_string(w.zahl) + "\n"; break;
    default:         aus += "NICHTS " + name + "\n"; break;
    }
}

const char* MFormat(int32_t f) {
    switch (f) {
    case 54: return "BC1_UNORM";
    case 63: return "BC5_UNORM";
    case 66: return "BC7_UNORM";
    case 67: return "BC7_UNORM_SRGB";
    default: return "?";
    }
}

uint32_t MBlockBytes(int32_t f) {
    if (f == 54) return 8;
    if (f == 63 || f == 66 || f == 67) return 16;
    return 0;
}

int Materialmessung(int argc, char** argv) {
    const std::string spielordner = argv[2];
    const std::string figur = argv[3];
    std::string cache;
    for (int i = 4; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];

    fbgame::Spiel spiel;
    std::string fehler;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    std::string grund;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }

    // Bundle: genauer Name zuerst, dann Teilstring (wie fbfigur).
    const fbindex::BundleInfo* bundle = nullptr;
    for (const auto& b : idx.bundles) if (b.name == figur) { bundle = &b; break; }
    if (bundle == nullptr) for (const auto& b : idx.bundles) if (b.name.find(figur) != std::string::npos) { bundle = &b; break; }
    if (bundle == nullptr) { std::fprintf(stderr, "Bundle nicht gefunden: %s\n", figur.c_str()); return 1; }
    std::printf("MATERIALMESSUNG %s\n", bundle->name.c_str());

    auto imBundle = [&](const fbindex::Eintrag& e) {
        for (size_t n : e.bundles) if (n == bundle->nummer) return true;
        return false;
    };

    // 1. Alle EBX des Bundles lesen, GUID -> Name merken.
    std::map<std::string, std::unique_ptr<fbebx::Datei>> ebx;
    std::map<std::string, std::string> guidName;
    for (const auto& kv : idx.ebx) {
        if (!imBundle(kv.second)) continue;
        std::vector<uint8_t> roh;
        std::string f;
        auto d = std::make_unique<fbebx::Datei>();
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f) || !d->Lies(roh, f)) {
            std::printf("EBX FEHLER %s: %s\n", kv.first.c_str(), f.c_str());
            continue;
        }
        const std::string typ = d->Objekte().empty() || !d->Objekte()[0] ? std::string("?") : d->Objekte()[0]->typ;
        std::printf("EBX %-28s %s  guid=%s\n", typ.c_str(), kv.first.c_str(), d->DateiGuid().c_str());
        guidName[MKlein(d->DateiGuid())] = kv.first;
        ebx[kv.first] = std::move(d);
    }

    // 2. MeshSets: Section -> MaterialId (LOD 0).
    for (const auto& kv : idx.res) {
        if (!imBundle(kv.second) || kv.second.resType != 0x49B156D4u) continue;
        std::vector<uint8_t> roh;
        std::string f;
        fbmesh::MeshSet ms;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f) || !fbmesh::LiesMeshSet(roh, ms, f)) {
            std::printf("MESHSET FEHLER %s: %s\n", kv.first.c_str(), f.c_str());
            continue;
        }
        std::printf("MESHSET %s  lods=%zu\n", kv.first.c_str(), ms.lods.size());
        if (!ms.lods.empty()) {
            for (const auto& sec : ms.lods[0].sections) {
                std::printf("  SECTION materialId=%d  material=%s  vertices=%u  primitive=%u\n",
                            sec.materialId, sec.materialName.c_str(), sec.vertexCount, sec.primitiveCount);
            }
        }
    }

    // 3. MeshAssets: die Materials-Liste samt Shader.TextureParameters.
    std::vector<std::string> meshNamen;
    for (const auto& kv : ebx) {
        for (const auto& o : kv.second->Objekte()) {
            if (!o) continue;
            const fbebx::Wert* m = o->Feldwert("Materials");
            if (m == nullptr || m->art != fbebx::Art::Liste) continue;
            meshNamen.push_back(kv.first);
            std::printf("MESHASSET %s (%s)\n", kv.first.c_str(), o->typ.c_str());
            std::string aus;
            int zeilen = 0;
            MZeige(*kv.second, *m, "Materials", 1, guidName, aus, zeilen);
            std::fwrite(aus.data(), 1, aus.size(), stdout);
            break;
        }
    }

    // 4. MeshVariationDatabase: nur Eintraege zu den Meshes dieses Bundles.
    bool mvdb = false;
    for (const auto& kv : ebx) {
        for (const auto& o : kv.second->Objekte()) {
            if (!o || o->typ.find("MeshVariationDatabase") == std::string::npos) continue;
            mvdb = true;
            const fbebx::Wert* eintr = o->Feldwert("Entries");
            std::printf("MVDB %s (%s) Eintraege=%zu\n", kv.first.c_str(), o->typ.c_str(),
                        eintr != nullptr ? eintr->liste.size() : size_t(0));
            if (eintr == nullptr) break;
            size_t gezeigt = 0;
            for (const auto& e : eintr->liste) {
                if (!e) continue;
                const fbebx::Wert* mesh = e->Feldwert("Mesh");
                std::string ziel;
                if (mesh != nullptr && mesh->art == fbebx::Art::Import) {
                    const auto it = guidName.find(MKlein(mesh->text));
                    if (it != guidName.end()) ziel = it->second;
                }
                bool unseres = false;
                for (const auto& n : meshNamen) if (n == ziel) unseres = true;
                if (!unseres || gezeigt >= 24) continue;
                ++gezeigt;
                std::string aus;
                int zeilen = 0;
                MZeige(*kv.second, *e, "Eintrag -> " + ziel, 1, guidName, aus, zeilen);
                std::fwrite(aus.data(), 1, aus.size(), stdout);
            }
            std::printf("MVDB gezeigt: %zu Eintraege zu Meshes dieses Bundles\n", gezeigt);
            break;
        }
    }
    if (!mvdb) std::printf("MVDB keine MeshVariationDatabase im Bundle\n");

    // 5. Texturen: Kopf nach Frostys Texture.cs (SWBF2-Zweig, 132 Byte).
    size_t texturen = 0, passend = 0;
    for (const auto& kv : idx.res) {
        if (!imBundle(kv.second) || kv.second.resType != 0x6BDE20BAu) continue;
        ++texturen;
        std::vector<uint8_t> d;
        std::string f;
        if (!spiel.HoleNachSha1(kv.second.sha1, d, f)) { std::printf("TEXTUR FEHLER %s: %s\n", kv.first.c_str(), f.c_str()); continue; }
        std::printf("TEXTUR %s  res=%zu Byte  rid=%016llx\n", kv.first.c_str(), d.size(),
                    static_cast<unsigned long long>(kv.second.resRid));
        std::printf("  resMeta %s\n", MHex(kv.second.resMeta, 16).c_str());
        if (d.size() < 132) { std::printf("  KOPF zu kurz (%zu Byte)\n", d.size()); continue; }
        size_t p = 0;
        auto u32 = [&]() { const uint32_t v = static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p + 1]) << 8) |
                                              (static_cast<uint32_t>(d[p + 2]) << 16) | (static_cast<uint32_t>(d[p + 3]) << 24);
                           p += 4; return v; };
        auto u16 = [&]() { const uint16_t v = static_cast<uint16_t>(d[p] | (d[p + 1] << 8)); p += 2; return v; };
        const uint32_t mipOff0 = u32(), mipOff1 = u32(), typ = u32();
        const int32_t format = static_cast<int32_t>(u32());
        const uint32_t unb1 = u32();
        const uint16_t flags = u16(), breite = u16(), hoehe = u16(), tiefe = u16(), scheiben = u16();
        const uint8_t mips = d[p], ersterMip = d[p + 1];
        p += 2;
        uint8_t chunk[16];
        std::memcpy(chunk, &d[p], 16);
        p += 16;
        uint32_t mipGroesse[15];
        for (int i = 0; i < 15; ++i) mipGroesse[i] = u32();
        const uint32_t chunkGroesse = u32(), namensHash = u32();
        std::string gruppe(reinterpret_cast<const char*>(&d[p]), 16);
        gruppe = gruppe.substr(0, gruppe.find('\0'));
        std::printf("  kopf typ=%u format=%d (%s) %ux%u tiefe=%u scheiben=%u mips=%u ersterMip=%u flags=0x%04x unb1=0x%08x mipOff=%u/%u gruppe=%s hash=0x%08x\n",
                    typ, format, MFormat(format), breite, hoehe, tiefe, scheiben, mips, ersterMip, flags, unb1,
                    mipOff0, mipOff1, gruppe.c_str(), namensHash);
        uint64_t summe = 0;
        for (int i = 0; i < mips && i < 15; ++i) summe += mipGroesse[i];
        const std::string gid = MGuid(chunk);
        const auto it = idx.chunks.find(gid);
        std::string imIndex = (it == idx.chunks.end()) ? std::string("FEHLT im Index")
                                                       : std::to_string(it->second.originalSize) + " Byte im Index";
        const uint32_t bb = MBlockBytes(format);
        const uint64_t erwartet = bb ? static_cast<uint64_t>((breite + 3u) / 4u) * ((hoehe + 3u) / 4u) * bb * (scheiben ? scheiben : 1u) : 0;
        const bool ok = (summe == chunkGroesse) && (bb == 0 || erwartet == mipGroesse[0]);
        if (ok) ++passend;
        // Was kommt beim Holen WIRKLICH heraus? Im Index steht bei allen 21
        // Anakin-Texturen nur die Summe der Mips AB ersterMip (0.34.0) - ob
        // der Chunk die grossen Ebenen trotzdem enthaelt, sagt nur das Holen.
        uint64_t abErstem = 0;
        for (int i = ersterMip; i < mips && i < 15; ++i) abErstem += mipGroesse[i];
        std::string geholt = "nicht geholt";
        if (it != idx.chunks.end()) {
            std::vector<uint8_t> cdaten;
            std::string cf;
            if (spiel.HoleNachSha1(it->second.sha1, cdaten, cf)) {
                geholt = std::to_string(cdaten.size()) + " Byte entpackt (" +
                         (cdaten.size() == chunkGroesse ? std::string("ALLE Mips") :
                          cdaten.size() == abErstem ? "nur ab ersterMip" : std::string("weder noch")) + ")";
            } else {
                geholt = "Holen FEHLGESCHLAGEN: " + cf;
            }
        }
        std::printf("  chunkdaten %s; Summe ab ersterMip %llu\n", geholt.c_str(), static_cast<unsigned long long>(abErstem));
        std::printf("  chunk %s: %s; ChunkSize %u; Summe der %u Mips %llu (%s); Mip0 %u, erwartet %llu (%s)\n",
                    gid.c_str(), imIndex.c_str(), chunkGroesse, mips, static_cast<unsigned long long>(summe),
                    summe == chunkGroesse ? "passt" : "WEICHT AB", mipGroesse[0], static_cast<unsigned long long>(erwartet),
                    bb == 0 ? "Format unbekannt" : (erwartet == mipGroesse[0] ? "passt" : "WEICHT AB"));
    }
    std::printf("TEXTUREN %zu, Kopf stimmig bei %zu\n", texturen, passend);
    return 0;
}

// --texturen: die Texturen einer Figur dekodieren und als PNG ablegen,
// samt Beipackzettel - derselbe Weg wie im Figurenfenster.
int Texturausgabe(int argc, char** argv) {
    const std::string spielordner = argv[2];
    const std::string figur = argv[3];
    const std::string ziel = argv[4];
    std::string cache;
    for (int i = 5; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }
    fbfigur::Modell m;
    if (!fbfigur::BaueFigur(spiel, idx, figur, "Characters/Rigs/Humanoids/Walrus_HumanMale", 0, false, m, fehler)) {
        std::fprintf(stderr, "%s\n", fehler.c_str());
        return 1;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(ziel), ec);
    std::vector<fbbeipack::MeshMaterial> mm;
    std::vector<std::string> prot;
    const auto t0 = std::chrono::steady_clock::now();
    if (!fbmaterial::Loese(spiel, idx, m, mm, prot, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbmaterial::SchreibeTexturen(spiel, idx, mm, ziel, prot, fehler);
    const std::string blatt = m.quelle.substr(m.quelle.find_last_of('/') + 1);
    const std::string zettel = ziel + "/" + blatt + ".fbmodel.material.txt";
    if (!fbbeipack::Schreibe(zettel, mm, fehler)) prot.push_back("BEIPACK nicht geschrieben: " + fehler);
    for (const std::string& z : prot) std::printf("%s\n", z.c_str());
    std::printf("BEIPACK %s (%.1f s)\n", zettel.c_str(),
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return 0;
}

// --clips: alle Animationsclips in allen AssetBanks zaehlen und auflisten
// (Stufe 5a). Gegenprobe gegen fbtools: 646 Baenke mit 1.469 Clips, davon
// RAW 931, Vbr 368, Frame 152, DCT 18 (fbtools animstat).
int Clipliste(int argc, char** argv) {
    const std::string spielordner = argv[2];
    std::string cache, liste, figur;
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
        if (std::strcmp(argv[i], "--liste") == 0) liste = argv[i + 1];
        if (std::strcmp(argv[i], "--figur") == 0) figur = argv[i + 1];
    }
    for (char& c : figur) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }
    const auto t0 = std::chrono::steady_clock::now();
    size_t banken = 0, ohneGd = 0, mitClips = 0, clips = 0;
    std::map<std::string, size_t> nachKlasse, nachCodec;
    std::string alle;
    // Wo stehen LESBARE Namen? Je Klasse zaehlen, wie viele __name nicht wie
    // eine Kennzahl aussehen (16 Hexzeichen + Endung), und ClipController
    // mit ihrem Verweis "Anim" auf einen Clip sammeln.
    std::map<std::string, std::pair<size_t, size_t>> namenJeKlasse;   // Klasse -> (alle, lesbar)
    std::map<std::string, std::string> beispielJeKlasse;
    std::map<std::string, std::string> clipCodecNachKey;
    std::vector<std::pair<std::string, std::string>> controller;        // Name, Anim-Key
    auto lesbar = [](const std::string& n) {
        if (n.empty()) return false;
        size_t hex = 0;
        while (hex < n.size() && std::isxdigit(static_cast<unsigned char>(n[hex]))) ++hex;
        return !(hex >= 16 && (hex == n.size() || n[hex] == '.'));
    };
    for (const auto& kv : idx.res) {
        if (kv.second.resType != 0x51A3C853u) continue;
        ++banken;
        std::vector<uint8_t> roh;
        std::string f;
        fbgd::Bank b;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f) || !fbgd::LiesBank(roh, b, f)) { ++ohneGd; continue; }
        size_t hier = 0;
        for (const fbgd::Eintrag& e : b.eintraege) {
            const std::string& k = e.klassenName;
            if (k.size() < 14 || k.compare(k.size() - 14, 14, "AnimationAsset") != 0) {
                fbgd::Datensatz s2;
                fbgd::LiesDatensatz(roh, b, e, s2, 1);
                auto& z = namenJeKlasse[k.empty() ? std::string("?") : k];
                ++z.first;
                if (s2.nameDa && lesbar(s2.name)) {
                    ++z.second;
                    if (!beispielJeKlasse.count(k)) beispielJeKlasse[k] = s2.name;
                }
                if (k == "ClipControllerAsset") {
                    const fbgd::Wert* anim = fbgd::Feld(s2.felder, "Anim");
                    controller.emplace_back(s2.nameDa ? s2.name : std::string(), anim ? anim->text : std::string());
                }
                continue;
            }
            fbgd::Datensatz s;
            fbgd::LiesDatensatz(roh, b, e, s, 1);
            if (const fbgd::Wert* kk = fbgd::Feld(s.felder, "__key")) clipCodecNachKey[kk->text] = k;
            std::string codec = "-", ende = "-", keys = "-";
            if (const fbgd::Wert* w = fbgd::Feld(s.basis, "CodecType")) codec = fbgd::Codec(w->ganz);
            if (const fbgd::Wert* w = fbgd::Feld(s.basis, "EndFrame")) ende = std::to_string(w->ganz);
            if (const fbgd::Wert* w = fbgd::Feld(s.felder, "NumKeys")) keys = std::to_string(w->ganz);
            ++clips;
            ++hier;
            ++nachKlasse[k];
            ++nachCodec[codec];
            alle += kv.first + "\t" + (s.nameDa ? s.name : std::string("-")) + "\t" + k + "\t" + codec +
                    "\tEndFrame " + ende + "\tNumKeys " + keys + "\n";
        }
        if (hier) ++mitClips;
    }
    std::printf("CLIPS %zu in %zu Baenken mit Clips (%zu AssetBanks, %zu ohne GD-Kopf) in %.1f s\n", clips, mitClips, banken, ohneGd,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    for (const auto& kv : nachKlasse) std::printf("KLASSE %-22s %zu\n", kv.first.c_str(), kv.second);
    for (const auto& kv : nachCodec) std::printf("CODEC %-8s %zu\n", kv.first.c_str(), kv.second);
    // Klassen mit den meisten lesbaren Namen
    std::vector<std::pair<size_t, std::string>> rang;
    for (const auto& kv : namenJeKlasse) rang.emplace_back(kv.second.second, kv.first);
    std::sort(rang.rbegin(), rang.rend());
    for (size_t i = 0; i < rang.size() && i < 12; ++i) {
        const auto& z = namenJeKlasse[rang[i].second];
        std::printf("NAMEN %-26s %6zu von %6zu lesbar, z.B. %s\n", rang[i].second.c_str(), z.second, z.first,
                    beispielJeKlasse.count(rang[i].second) ? beispielJeKlasse[rang[i].second].c_str() : "-");
    }
    size_t aufClip = 0, lesbarAufClip = 0, gezeigt = 0;
    for (const auto& cc : controller) {
        const auto it = clipCodecNachKey.find(cc.second);
        if (it == clipCodecNachKey.end()) continue;
        ++aufClip;
        if (lesbar(cc.first)) {
            ++lesbarAufClip;
            if (gezeigt++ < 15) std::printf("CC %s -> %s (%s)\n", cc.first.c_str(), cc.second.c_str(), it->second.c_str());
        }
    }
    std::printf("CLIPCONTROLLER %zu, davon verweisen %zu auf einen Clip, %zu davon mit lesbarem Namen\n", controller.size(), aufClip,
                lesbarAufClip);
    // Mehrere Figuren mit Komma (0.44.2): anakin,darthvader,vader - misst, wie
    // viele Clips unter dem Ordnernamen und unter der Kurzform stehen.
    std::vector<std::string> figurenListe;
    for (size_t a0 = 0; !figur.empty() && a0 <= figur.size();) {
        size_t e0 = figur.find(',', a0);
        if (e0 == std::string::npos) e0 = figur.size();
        if (e0 > a0) figurenListe.push_back(figur.substr(a0, e0 - a0));
        a0 = e0 + 1;
    }
    for (const std::string& figurName : figurenListe) {
        // Wie viele Clips traegt die Figur im Controllernamen? Grundlage fuer
        // den Filter "nur Figur" im Animationsfenster (0.38.1).
        std::map<std::string, size_t> jeKlasse;
        std::set<std::string> clipsDerFigur;
        std::vector<std::string> beispiele;
        for (const auto& cc : controller) {
            std::string n = cc.first;
            for (char& c : n) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (n.find(figurName) == std::string::npos) continue;
            const auto it = clipCodecNachKey.find(cc.second);
            if (it == clipCodecNachKey.end() || !clipsDerFigur.insert(cc.second).second) continue;
            ++jeKlasse[it->second];
            if (beispiele.size() < 25) beispiele.push_back(cc.first + " (" + it->second + ")");
        }
        std::string aufteilung;
        for (const auto& kv : jeKlasse) aufteilung += " " + kv.first + " " + std::to_string(kv.second);
        std::printf("FIGUR %s: %zu Clips mit dem Namen im Controller:%s\n", figurName.c_str(), clipsDerFigur.size(), aufteilung.c_str());
        for (size_t b3 = 0; b3 < beispiele.size() && b3 < 8; ++b3) std::printf("  %s\n", beispiele[b3].c_str());
    }
    if (!liste.empty()) {
        std::string fe;
        fbdatei::SchreibeAlles(liste, std::vector<uint8_t>(alle.begin(), alle.end()), fe);
        std::printf("LISTE %s\n", liste.c_str());
    }
    return 0;
}

// --clipinfo: fuer einzelne Clips die ganze Namenskette MESSEN (Vorarbeit
// Stufe 5b), nach dem Weg, den fbtools gefunden hat:
//   Clip.__base.ChannelToDofAsset -> DofIds des Clips
//   RigAsset, dessen DofIds >= 90 % davon enthalten -> Platz je DofId
//   RigAsset.RigDofSets + DofSetIdIndices -> je DofSet die Slotnamen (ueber
//   LayoutAsset.Slots[].Name, rekursiv ueber LayoutAssets und Children),
//   an ihre STARTPOSITION gelegt, nicht aneinandergehaengt
//   RAW: Kanal j heisst namen[MappingIndices[j]].
// Selbstpruefung: Drehkanaele muessen auf ".q" enden, Vektorkanaele auf ".t"
// oder ".s" - ohne dass das irgendwo vorausgesetzt wurde.
int Clipinfo(int argc, char** argv) {
    const std::string spielordner = argv[2];
    std::string muster = argv[3];
    for (char& c : muster) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    std::string cache;
    size_t hoechstens = 3;
    for (int i = 4; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
        if (std::strcmp(argv[i], "--max") == 0) hoechstens = static_cast<size_t>(std::atoi(argv[i + 1]));
    }
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }
    struct Ort { size_t bank = 0, eintrag = 0; };
    std::vector<std::string> bankName, bankSha;
    std::map<std::string, Ort> nachKey;
    std::vector<Ort> rigs, treffer;
    const auto t0 = std::chrono::steady_clock::now();
    auto klein = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return x; };
    for (const auto& kv : idx.res) {
        if (kv.second.resType != 0x51A3C853u) continue;
        std::vector<uint8_t> roh;
        std::string f;
        fbgd::Bank b;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f) || !fbgd::LiesBank(roh, b, f)) continue;
        const size_t bi = bankName.size();
        bankName.push_back(kv.first);
        bankSha.push_back(kv.second.sha1);
        const bool bankPasst = klein(kv.first).find(muster) != std::string::npos;
        for (size_t ei = 0; ei < b.eintraege.size(); ++ei) {
            const fbgd::Eintrag& e = b.eintraege[ei];
            fbgd::Datensatz ds;
            fbgd::LiesDatensatz(roh, b, e, ds, 1);
            if (const fbgd::Wert* k = fbgd::Feld(ds.felder, "__key")) nachKey[k->text] = Ort{ bi, ei };
            if (e.klassenName == "RigAsset") rigs.push_back(Ort{ bi, ei });
            const std::string& kl = e.klassenName;
            if (treffer.size() < hoechstens && kl.size() >= 14 && kl.compare(kl.size() - 14, 14, "AnimationAsset") == 0 &&
                (bankPasst || (ds.nameDa && klein(ds.name).find(muster) != std::string::npos))) {
                treffer.push_back(Ort{ bi, ei });
            }
        }
    }
    std::printf("CLIPINFO Verzeichnis: %zu Keys, %zu RigAssets, %zu Treffer fuer \"%s\" (%.1f s)\n", nachKey.size(), rigs.size(),
                treffer.size(), muster.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());

    // Baenke bei Bedarf neu holen (kleiner Zwischenspeicher).
    std::map<size_t, std::pair<std::vector<uint8_t>, fbgd::Bank>> bankCache;
    auto satzVon = [&](const Ort& o, fbgd::Datensatz& ds) -> const fbgd::Eintrag* {
        auto it = bankCache.find(o.bank);
        if (it == bankCache.end()) {
            if (bankCache.size() > 24) bankCache.clear();
            std::pair<std::vector<uint8_t>, fbgd::Bank> neu;
            std::string f;
            if (!spiel.HoleNachSha1(bankSha[o.bank], neu.first, f) || !fbgd::LiesBank(neu.first, neu.second, f)) return nullptr;
            it = bankCache.emplace(o.bank, std::move(neu)).first;
        }
        if (o.eintrag >= it->second.second.eintraege.size()) return nullptr;
        const fbgd::Eintrag& e = it->second.second.eintraege[o.eintrag];
        fbgd::LiesDatensatz(it->second.first, it->second.second, e, ds, 0);
        return &e;
    };
    std::function<void(const std::string&, int, std::set<std::string>&, std::vector<std::string>&)> slotsVon =
        [&](const std::string& key, int tiefe, std::set<std::string>& gesehen, std::vector<std::string>& namen) {
            if (tiefe > 6 || gesehen.count(key)) return;
            gesehen.insert(key);
            const auto it = nachKey.find(key);
            if (it == nachKey.end()) return;
            fbgd::Datensatz ds;
            const fbgd::Eintrag* e = satzVon(it->second, ds);
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
                    for (const fbgd::Wert& k : w->werte) slotsVon(k.text, tiefe + 1, gesehen, namen);
                }
            }
        };

    for (const Ort& o : treffer) {
        fbgd::Datensatz ds;
        const fbgd::Eintrag* e = satzVon(o, ds);
        if (e == nullptr) continue;
        auto ganz = [&](const fbgd::Felder& f, const char* n) -> long long { const fbgd::Wert* w = fbgd::Feld(f, n); return w ? w->ganz : -1; };
        std::string codec = "-";
        if (const fbgd::Wert* w = fbgd::Feld(ds.basis, "CodecType")) codec = fbgd::Codec(w->ganz);
        const long long q = ganz(ds.felder, "QuatCount"), v = ganz(ds.felder, "Vec3Count"), fl = ganz(ds.felder, "FloatCount");
        const long long cq = ganz(ds.felder, "ConstQuatCount"), cv = ganz(ds.felder, "ConstVec3Count"), cf = ganz(ds.felder, "ConstFloatCount");
        // DofIds des Clips ueber seine ChannelToDofAsset
        std::vector<long long> dofIds;
        std::string ctd = "-";
        if (const fbgd::Wert* w = fbgd::Feld(ds.basis, "ChannelToDofAsset")) {
            ctd = w->text;
            const auto it = nachKey.find(w->text);
            if (it != nachKey.end()) {
                fbgd::Datensatz c;
                if (satzVon(it->second, c) != nullptr) {
                    if (const fbgd::Wert* d = fbgd::Feld(c.felder, "DofIds")) for (const fbgd::Wert& x : d->werte) dofIds.push_back(x.ganz);
                }
            }
        }
        std::printf("CLIP %s | %s | %s %s | NumKeys %lld EndFrame %lld | q/t/f %lld/%lld/%lld const %lld/%lld/%lld | DofIds %zu (ChannelToDof %s)\n",
                    ds.nameDa ? ds.name.c_str() : "-", bankName[o.bank].c_str(), e->klassenName.c_str(), codec.c_str(),
                    ganz(ds.felder, "NumKeys"), ganz(ds.basis, "EndFrame"), q, v, fl, cq, cv, cf, dofIds.size(), ctd.c_str());
        if (dofIds.empty()) continue;
        // RigAsset mit >= 90 % der DofIds
        const std::set<long long> gesucht(dofIds.begin(), dofIds.end());
        std::vector<std::string> flach;
        std::map<long long, size_t> stelle;
        for (const Ort& r : rigs) {
            fbgd::Datensatz rs;
            if (satzVon(r, rs) == nullptr) continue;
            const fbgd::Wert* rd = fbgd::Feld(rs.felder, "DofIds");
            if (rd == nullptr || rd->werte.empty()) continue;
            size_t drin = 0;
            std::set<long long> vorrat;
            for (const fbgd::Wert& x : rd->werte) vorrat.insert(x.ganz);
            for (long long d : gesucht) if (vorrat.count(d)) ++drin;
            if (drin < std::max<size_t>(1, gesucht.size() * 9 / 10)) continue;
            for (size_t i = 0; i < rd->werte.size(); ++i) stelle[rd->werte[i].ganz] = i;
            flach.assign(rd->werte.size(), std::string());
            const fbgd::Wert* st = fbgd::Feld(rs.felder, "DofSetIdIndices");
            const fbgd::Wert* se = fbgd::Feld(rs.felder, "RigDofSets");
            size_t luecken = 0;
            if (st != nullptr && se != nullptr) {
                for (size_t k = 0; k < se->werte.size(); ++k) {
                    if (k >= st->werte.size()) continue;
                    std::vector<std::string> namen;
                    std::set<std::string> gesehen;
                    slotsVon(se->werte[k].text, 0, gesehen, namen);
                    const long long beginn = st->werte[k].ganz;
                    const long long ende = (k + 1 < st->werte.size()) ? st->werte[k + 1].ganz : static_cast<long long>(flach.size());
                    if (static_cast<long long>(namen.size()) != ende - beginn) ++luecken;
                    for (size_t i = 0; i < namen.size(); ++i) {
                        const long long pl = beginn + static_cast<long long>(i);
                        if (pl >= 0 && static_cast<size_t>(pl) < flach.size()) flach[static_cast<size_t>(pl)] = namen[i];
                    }
                }
            }
            size_t benannt = 0;
            for (const auto& n : flach) if (!n.empty()) ++benannt;
            std::string rigKey = "-";
            if (const fbgd::Wert* k = fbgd::Feld(rs.felder, "__key")) rigKey = k->text;
            std::printf("  RIG %s in %s: %zu von %zu DofIds, benannt %zu von %zu, DofSets mit Luecke %zu\n", rigKey.c_str(),
                        bankName[r.bank].c_str(), drin, gesucht.size(), benannt, flach.size(), luecken);
            break;
        }
        if (flach.empty()) { std::printf("  RIG keines mit >= 90 %% der DofIds\n"); continue; }
        std::vector<std::string> namen9;
        size_t benannt9 = 0;
        for (size_t k = 0; k < dofIds.size(); ++k) {
            const auto it = stelle.find(dofIds[k]);
            std::string n = (it != stelle.end()) ? flach[it->second] : std::string();
            if (!n.empty()) ++benannt9; else n = "kanal" + std::to_string(k);
            namen9.push_back(n);
        }
        std::printf("  NAMEN %zu von %zu DofIds benannt\n", benannt9, dofIds.size());
        if (e->klassenName != "RawAnimationAsset") continue;
        // RAW: Kanalreihenfolge bewegt q,t,f dann konstant q,t,f; Name ueber MappingIndices.
        const fbgd::Wert* mi = fbgd::Feld(ds.felder, "MappingIndices");
        std::vector<char> art;
        for (long long i = 0; i < q; ++i) art.push_back('q');
        for (long long i = 0; i < v; ++i) art.push_back('t');
        for (long long i = 0; i < fl; ++i) art.push_back('f');
        for (long long i = 0; i < cq; ++i) art.push_back('q');
        for (long long i = 0; i < cv; ++i) art.push_back('t');
        for (long long i = 0; i < cf; ++i) art.push_back('f');
        size_t passt[3] = { 0, 0, 0 }, zahl[3] = { 0, 0, 0 };
        std::string beispiele;
        for (size_t j = 0; j < art.size(); ++j) {
            const long long m = (mi != nullptr && j < mi->werte.size()) ? mi->werte[j].ganz : static_cast<long long>(j);
            const std::string n = (m >= 0 && static_cast<size_t>(m) < namen9.size()) ? namen9[static_cast<size_t>(m)] : "kanal" + std::to_string(m);
            const std::string kl = klein(n);
            auto endet = [&](const char* x) { const size_t l = std::strlen(x); return kl.size() >= l && kl.compare(kl.size() - l, l, x) == 0; };
            const int t = art[j] == 'q' ? 0 : (art[j] == 't' ? 1 : 2);
            ++zahl[t];
            if (t == 0 && (endet(".q") || kl == "deltaq")) ++passt[0];
            else if (t == 1 && (endet(".t") || endet(".s") || kl == "deltat")) ++passt[1];
            else if (t == 2 && !endet(".q") && !endet(".t") && !endet(".s")) ++passt[2];
            if (j < 6) beispiele += std::string(" ") + art[j] + ":" + n;
        }
        std::printf("  KANAELE %zu; Anhang passt: Drehung %zu/%zu, Vektor %zu/%zu, Float %zu/%zu;%s\n", art.size(), passt[0], zahl[0],
                    passt[1], zahl[1], passt[2], zahl[2], beispiele.c_str());
    }
    return 0;
}

// --anim: Clips entpacken (RAW, FRAME) und als .fbanim schreiben - fuer die
// Gegenprobe gegen fbtools (tools/VERGLEICHE_ANIM.py vergleicht die Kurven).
int Animausgabe(int argc, char** argv) {
    const std::string spielordner = argv[2];
    const std::string ziel = argv[3];
    std::string cache, liste, muster;
    size_t hoechstens = 20;
    for (int i = 4; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
        if (std::strcmp(argv[i], "--liste") == 0) liste = argv[i + 1];
        if (std::strcmp(argv[i], "--muster") == 0) muster = argv[i + 1];
        if (std::strcmp(argv[i], "--max") == 0) hoechstens = static_cast<size_t>(std::atoi(argv[i + 1]));
    }
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }
    const auto t0 = std::chrono::steady_clock::now();
    fbanim::Quelle q;
    if (!q.Baue(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    std::printf("ANIM Verzeichnis: %zu Clips, %zu Keys, %zu RigAssets (%.1f s)\n", q.Clips().size(), q.KeyZahl(), q.RigZahl(),
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    std::vector<size_t> wahl;
    if (!liste.empty()) {
        std::vector<uint8_t> roh;
        std::string f;
        if (!fbdatei::LiesAlles(liste, roh, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
        std::map<std::string, size_t> nachName;
        for (size_t i = 0; i < q.Clips().size(); ++i) nachName.emplace(q.Clips()[i].name, i);
        std::string zeile;
        for (size_t i = 0; i <= roh.size(); ++i) {
            const char c = i < roh.size() ? static_cast<char>(roh[i]) : '\n';
            if (c == '\n' || c == '\r') {
                if (!zeile.empty()) {
                    const auto it = nachName.find(zeile);
                    if (it != nachName.end()) wahl.push_back(it->second);
                    else std::printf("ANIM %s nicht gefunden\n", zeile.c_str());
                }
                zeile.clear();
            } else {
                zeile += c;
            }
        }
    } else {
        std::string m = muster;
        for (char& c : m) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        for (size_t i = 0; i < q.Clips().size() && wahl.size() < hoechstens; ++i) {
            std::string b = q.BankName(q.Clips()[i].bank) + "|" + q.Clips()[i].name;
            for (char& c : b) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (m.empty() || b.find(m) != std::string::npos) wahl.push_back(i);
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(ziel), ec);
    size_t geschrieben = 0, offen = 0;
    for (size_t i : wahl) {
        const fbanim::ClipEintrag& ce = q.Clips()[i];
        fbanim::Clip c;
        std::string f;
        if (!q.Entpacke(ce, c, f)) {
            ++offen;
            std::printf("ANIM %s %s: %s\n", ce.name.c_str(), ce.klasse.c_str(), f.c_str());
            continue;
        }
        std::string blatt = ce.name;
        if (blatt.size() > 5 && blatt.compare(blatt.size() - 5, 5, ".chan") == 0) blatt.resize(blatt.size() - 5);
        const std::string pfad = ziel + "/animation_" + blatt + ".fbanim";
        if (!fbanim::SchreibeFbanim(pfad, c, f)) { std::printf("ANIM %s: %s\n", ce.name.c_str(), f.c_str()); continue; }
        ++geschrieben;
        std::printf("ANIM %s %s %s kanaele=%zu keys=%zu benannt=%zu/%zu rig=%s (%zu Treffer)\n", ce.name.c_str(), c.klasse.c_str(),
                    c.codec.c_str(), c.kanaele.size(), c.zeiten.size(), c.benannt, c.dofIds, c.rig.empty() ? "-" : c.rig.c_str(), c.rigTreffer);
    }
    std::printf("ANIM geschrieben %zu, noch nicht entpackbar %zu\n", geschrieben, offen);
    return 0;
}

// --pose: die Konvention der Clipdaten MESSEN, bevor Keys in Max entstehen.
// Je Bone mit Kanal "Name.q" wird die Drehung des ersten Keys mit der
// Ruhelage des Skeletts (LocalPose, Zeilen right/up/forward) verglichen - fuer
// beide denkbaren Lesarten der Quaternion (Zeilen = Spalten von R(q) oder =
// Zeilen von R(q)). Die richtige liegt nahe an der Ruhelage (Median klein).
// Ebenso "Name.t" gegen LocalPose.trans: gleicher Raum, gleiche Einheit?
int Posenvergleich(int argc, char** argv) {
    const std::string spielordner = argv[2];
    std::string muster = argv[3];
    std::string cache, skelettName = "characters/rigs/humanoids/walrus_humanmale";
    size_t hoechstens = 4;
    for (int i = 4; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
        if (std::strcmp(argv[i], "--max") == 0) hoechstens = static_cast<size_t>(std::atoi(argv[i + 1]));
    }
    for (char& c : muster) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }
    fbebx::Skelett sk;
    for (const auto& kv : idx.ebx) {
        std::string k = kv.first;
        for (char& c : k) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (k.find(skelettName) == std::string::npos) continue;
        std::vector<uint8_t> roh;
        fbebx::Datei e;
        if (spiel.HoleNachSha1(kv.second.sha1, roh, fehler) && e.Lies(roh, fehler)) { sk = fbebx::LiesSkelett(e); if (sk.gefunden) break; }
    }
    if (!sk.gefunden) { std::fprintf(stderr, "Skelett nicht gefunden\n"); return 1; }
    std::map<std::string, size_t> boneIndex;
    for (size_t i = 0; i < sk.namen.size(); ++i) boneIndex[sk.namen[i]] = i;
    fbanim::Quelle q;
    if (!q.Baue(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    size_t gezeigt = 0;
    for (const fbanim::ClipEintrag& ce : q.Clips()) {
        if (gezeigt >= hoechstens) break;
        if (ce.klasse != "RawAnimationAsset") continue;
        std::string b = q.BankName(ce.bank);
        for (char& c : b) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (b.find(muster) == std::string::npos) continue;
        fbanim::Clip c;
        if (!q.Entpacke(ce, c, fehler)) continue;
        ++gezeigt;
        std::vector<double> winkelA, winkelB, winkelC, winkelD, abstand, laenge;
        for (const fbanim::Kanal& k : c.kanaele) {
            if (k.name.size() < 3) continue;
            const std::string bone = k.name.substr(0, k.name.size() - 2);
            const auto it = boneIndex.find(bone);
            if (it == boneIndex.end() || it->second >= sk.lokal.size()) continue;
            const fbebx::Lage& l = sk.lokal[it->second];
            if (k.art == 'q' && k.name.compare(k.name.size() - 2, 2, ".q") == 0 && k.werte.size() >= 4) {
                const double* M[3] = { l.right, l.up, l.forward };
                auto winkel = [](double spur) { const double c2 = std::max(-1.0, std::min(1.0, (spur - 1.0) / 2.0)); return std::acos(c2) * 180.0 / 3.14159265358979; };
                // Zwei Reihenfolgen der Komponenten (x,y,z,w / w,x,y,z), je zwei Lesarten.
                for (int folge = 0; folge < 2; ++folge) {
                    const double x = folge ? k.werte[1] : k.werte[0], y = folge ? k.werte[2] : k.werte[1];
                    const double z = folge ? k.werte[3] : k.werte[2], w = folge ? k.werte[0] : k.werte[3];
                    const double R[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                                             { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                                             { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
                    // A: Zeilen der Ruhelage gegen die SPALTEN von R, B: gegen die ZEILEN.
                    double spurA = 0, spurB = 0;
                    for (int r = 0; r < 3; ++r) for (int s2 = 0; s2 < 3; ++s2) { spurA += M[r][s2] * R[s2][r]; spurB += M[r][s2] * R[r][s2]; }
                    (folge ? winkelC : winkelA).push_back(winkel(spurA));
                    (folge ? winkelD : winkelB).push_back(winkel(spurB));
                }
            } else if (k.art == 't' && k.name.compare(k.name.size() - 2, 2, ".t") == 0 && k.werte.size() >= 3) {
                const double dx = k.werte[0] - l.trans[0], dy = k.werte[1] - l.trans[1], dz = k.werte[2] - l.trans[2];
                abstand.push_back(std::sqrt(dx * dx + dy * dy + dz * dz));
                laenge.push_back(std::sqrt(l.trans[0] * l.trans[0] + l.trans[1] * l.trans[1] + l.trans[2] * l.trans[2]));
            }
        }
        auto median = [](std::vector<double> v) { if (v.empty()) return -1.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        double fps = 0.0;
        if (c.dauer > 0.0f && c.endFrame > 0) fps = c.endFrame / static_cast<double>(c.dauer);
        std::printf("POSE %s | %zu Keys, EndFrame %d, Dauer %.4f s -> %.3f Bilder/s\n", ce.name.c_str(), c.zeiten.size(), c.endFrame, c.dauer, fps);
        std::printf("  Drehung (erster Key gegen Ruhelage, %zu Bones), Median in Grad: xyzw Spalten %.2f, xyzw Zeilen %.2f, wxyz Spalten %.2f, wxyz Zeilen %.2f\n",
                    winkelA.size(), median(winkelA), median(winkelB), median(winkelC), median(winkelD));
        std::printf("  Verschiebung (%zu Bones): Median Abstand zur Ruhelage %.4f m bei Median-Bonelaenge %.4f m\n", abstand.size(),
                    median(abstand), median(laenge));
    }
    if (gezeigt == 0) std::printf("POSE keine RAW-Clips fuer \"%s\"\n", muster.c_str());
    return 0;
}

// --waffe: die Waffen einer Figur MESSEN, bevor sie importiert werden
// (0.40.0). Gefunden (Index offline): Helden-Ausruestung liegt unter
// gameplay/equipment/heroes/<waffe>/, Lichtschwerter tragen den Helden im
// Namen (lightsaberanakin), Blaster nicht immer (dl44, ee3). Gemessen wird je
// Kandidat: Meshtyp (starr oder mit Skin), LODs, je Section Material,
// Vertices, Bones je Vertex und die Bone-Liste - daran entscheidet sich, ob
// die Waffe an einen Bone gehaengt oder geskinnt wird.
int Waffenmessung(int argc, char** argv) {
    const std::string spielordner = argv[2];
    std::string schluessel = argv[3];
    for (char& c : schluessel) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    std::string cache;
    for (int i = 4; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }
    size_t kandidaten = 0;
    for (const auto& kv : idx.res) {
        if (kv.second.resType != 0x49B156D4u) continue;
        std::string n = kv.first;
        for (char& c : n) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (n.compare(0, 19, "gameplay/equipment/") != 0 || n.find(schluessel) == std::string::npos) continue;
        ++kandidaten;
        std::vector<uint8_t> daten;
        std::string f;
        fbmesh::MeshSet ms;
        if (!spiel.HoleNachSha1(kv.second.sha1, daten, f) || !fbmesh::LiesMeshSet(daten, ms, f, kv.second.resMeta)) {
            std::printf("WAFFE %s: %s\n", kv.first.c_str(), f.c_str());
            continue;
        }
        std::printf("WAFFE %s  (%zu Bundles) Meshtyp %u Flags 0x%08X LODs %zu Sections %u%s\n", kv.first.c_str(), kv.second.bundles.size(),
                    ms.meshTypeId, ms.flags, ms.lods.size(), static_cast<unsigned>(ms.sectionCount),
                    ms.inlineDaten.empty() ? "" : " (Daten in der .res)");
        if (!ms.lods.empty()) {
            const fbmesh::Lod& l = ms.lods[0];
            for (const fbmesh::Section& sec : l.sections) {
                std::string liste;
                for (size_t i = 0; i < sec.boneList.size() && i < 12; ++i) liste += (i ? "," : "") + std::to_string(sec.boneList[i]);
                if (sec.boneList.size() > 12) liste += ",...";
                std::printf("  LOD0 Section %d %-24s %6u Vertices %6u Dreiecke  Bones je Vertex %u  Bone-Liste %zu [%s]\n", sec.index,
                            sec.materialName.c_str(), sec.vertexCount, sec.primitiveCount, static_cast<unsigned>(sec.bonesPerVertex),
                            sec.boneList.size(), liste.c_str());
            }
        }
        for (const std::string& w : ms.warnungen) std::printf("  Hinweis: %s\n", w.c_str());
        // Texturen im selben Ordner
        const std::string ordner = n.substr(0, n.find_last_of('/') + 1);
        for (const auto& t : idx.res) {
            if (t.second.resType != 0x6BDE20BAu) continue;
            std::string tn = t.first;
            for (char& c : tn) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (tn.compare(0, ordner.size(), ordner) == 0) std::printf("  Textur %s\n", t.first.c_str());
        }
    }
    std::printf("WAFFEN fuer \"%s\": %zu Meshes unter gameplay/equipment/\n", schluessel.c_str(), kandidaten);
    return 0;
}

// --clipablage: das Clipverzeichnis bauen, in die Ablage schreiben, wieder
// laden und JEDES Feld vergleichen (0.41.0). Die Ablage darf nichts
// ungenauer machen - hier wird es gemessen, dazu die Zeiten.
int Clipablage(int argc, char** argv) {
    const std::string spielordner = argv[2];
    const std::string datei = argv[3];
    std::string cache;
    for (int i = 4; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }
    auto sek = [](std::chrono::steady_clock::time_point t) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count(); };
    auto t0 = std::chrono::steady_clock::now();
    fbanim::Quelle a;
    if (!a.Baue(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    const double bauen = sek(t0);
    t0 = std::chrono::steady_clock::now();
    if (!a.Speichere(datei, spiel.Kopfnummer(), fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    const double schreiben = sek(t0);
    t0 = std::chrono::steady_clock::now();
    fbanim::Quelle b;
    if (!b.Lade(spiel, datei, spiel.Kopfnummer(), grund)) { std::printf("CLIPABLAGE nicht ladbar: %s\n", grund.c_str()); return 1; }
    const double laden = sek(t0);
    size_t abweichend = 0;
    if (a.Clips().size() != b.Clips().size() || a.KeyZahl() != b.KeyZahl() || a.RigZahl() != b.RigZahl()) abweichend = 1;
    for (size_t i = 0; abweichend == 0 && i < a.Clips().size(); ++i) {
        const fbanim::ClipEintrag& x = a.Clips()[i];
        const fbanim::ClipEintrag& y = b.Clips()[i];
        if (x.bank != y.bank || x.eintrag != y.eintrag || x.name != y.name || x.klasse != y.klasse || x.codec != y.codec || x.key != y.key ||
            x.anzeige != y.anzeige || x.endFrame != y.endFrame || x.fps != y.fps || x.controller != y.controller ||
            a.BankName(x.bank) != b.BankName(y.bank)) ++abweichend;
    }
    // Gegenprobe mit einem echten Clip: aus beiden Verzeichnissen entpacken.
    size_t probe = 0, probeGleich = 0;
    for (size_t i = 0; i < a.Clips().size() && probe < 3; ++i) {
        if (a.Clips()[i].klasse != "RawAnimationAsset") continue;
        fbanim::Clip ca, cb;
        std::string f1, f2;
        if (!a.Entpacke(a.Clips()[i], ca, f1) || !b.Entpacke(b.Clips()[i], cb, f2)) continue;
        ++probe;
        bool gleich = ca.kanaele.size() == cb.kanaele.size() && ca.zeiten == cb.zeiten && ca.rig == cb.rig;
        for (size_t k = 0; gleich && k < ca.kanaele.size(); ++k)
            gleich = ca.kanaele[k].name == cb.kanaele[k].name && ca.kanaele[k].werte == cb.kanaele[k].werte;
        if (gleich) ++probeGleich;
    }
    std::vector<uint8_t> roh;
    std::string f3;
    fbdatei::LiesAlles(datei, roh, f3);
    std::printf("CLIPABLAGE %s: %zu Clips, %zu Keys, %zu Rigs; %zu Eintraege abweichend; Entpack-Probe %zu von %zu gleich; "
                "Bauen %.1f s, Schreiben %.2f s, Laden %.2f s, Datei %.1f MB\n",
                abweichend == 0 && probeGleich == probe ? "gleich" : "ABWEICHUNG", b.Clips().size(), b.KeyZahl(), b.RigZahl(), abweichend,
                probeGleich, probe, bauen, schreiben, laden, roh.size() / 1048576.0);
    return abweichend == 0 ? 0 : 1;
}

// --gesicht: die "verzerrten Koepfe" (0.42.0). Frostbite-Koepfe brauchen eine
// Gesichtspose, sonst sieht das Gesicht in der Bindepose kaputt aus. Laut
// id-daemon (ZenHAX, Werkzeug fuer SWBF2 und weitere Frostbite-Spiele)
// verweist die VisualUnlock-Datei der Figur auf ein Asset in den
// Animationsbaenken, und diese Pose repariert den Kopf - manchmal haengt der
// Verweis an Zaehnen, Haaren oder Bart statt am Kopf. Hier wird gemessen:
// alle Verweise aus den EBX des Figuren-Bundles gegen die Keys aller Baenke,
// und was dort liegt (Klasse, Name, Kanaele, Abstand der FACIAL-Bones zur
// Ruhelage).
int Gesichtsmessung(int argc, char** argv) {
    const std::string spielordner = argv[2];
    std::string muster = argv[3];
    for (char& c : muster) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    std::string cache;
    for (int i = 4; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }
    auto klein = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return x; };
    const fbindex::BundleInfo* bundle = nullptr;
    for (const auto& b : idx.bundles) if (klein(b.name) == muster) { bundle = &b; break; }
    if (bundle == nullptr) for (const auto& b : idx.bundles) if (klein(b.name).find(muster) != std::string::npos) { bundle = &b; break; }
    if (bundle == nullptr) { std::printf("GESICHT kein Bundle mit %s\n", muster.c_str()); return 1; }
    std::printf("GESICHT Bundle %s\n", bundle->name.c_str());
    // Alle Werte aus den EBX, die ein Verweis sein koennten: 16 Hexzeichen
    // (Bank-Key) oder GUIDs (dann erste/letzte 8 Byte, auch umgedreht).
    std::map<std::string, std::string> kandidaten;       // Hex -> woher
    size_t ebxZahl = 0;
    for (const auto& kv : idx.ebx) {
        bool drin = false;
        for (size_t bi : kv.second.bundles) if (bi == bundle->nummer) { drin = true; break; }
        if (!drin) continue;
        std::vector<uint8_t> roh;
        fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, fehler) || !e.Lies(roh, fehler)) continue;
        ++ebxZahl;
        const std::string text = fbebx::AlsText(e, 0);
        size_t zeilen = 0;
        size_t a = 0;
        while (a < text.size()) {
            size_t b = text.find('\n', a);
            if (b == std::string::npos) b = text.size();
            const std::string z = text.substr(a, b - a);
            a = b + 1;
            const std::string zk = klein(z);
            const bool ant = zk.find("ant") != std::string::npos || zk.find("pose") != std::string::npos ||
                             zk.find("face") != std::string::npos || zk.find("facial") != std::string::npos;
            // Hexfolgen sammeln
            std::string hex;
            for (size_t i = 0; i <= zk.size(); ++i) {
                const char c = i < zk.size() ? zk[i] : ' ';
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) { hex += c; continue; }
                if (c == '-' && !hex.empty()) continue;           // GUID-Striche ueberspringen
                if (hex.size() == 16) kandidaten.emplace(hex, kv.first);
                if (hex.size() == 32) {
                    kandidaten.emplace(hex.substr(0, 16), kv.first + " (GUID vorn)");
                    kandidaten.emplace(hex.substr(16), kv.first + " (GUID hinten)");
                    std::string r1, r2;
                    for (int k = 7; k >= 0; --k) { r1 += hex.substr(static_cast<size_t>(2 * k), 2); r2 += hex.substr(static_cast<size_t>(16 + 2 * k), 2); }
                    kandidaten.emplace(r1, kv.first + " (GUID vorn, umgedreht)");
                    kandidaten.emplace(r2, kv.first + " (GUID hinten, umgedreht)");
                    // Frosty (AntAsset.SafeGuid): ID = GUID aus den acht Bytes des
                    // u64-Keys (BitConverter, little-endian). In der Textform stehen
                    // die ersten drei Gruppen byteweise gedreht: data1 (4 Byte),
                    // data2 (2), data3 (2). Rohbytes = jede Gruppe gedreht.
                    auto dreh = [](const std::string& x) { std::string r; for (size_t q = x.size(); q >= 2; q -= 2) r += x.substr(q - 2, 2); return r; };
                    const std::string roh8 = dreh(hex.substr(0, 8)) + dreh(hex.substr(8, 4)) + dreh(hex.substr(12, 4));
                    kandidaten.emplace(roh8, kv.first + " (GUID, Rohbytes 0-7)");
                    kandidaten.emplace(dreh(roh8), kv.first + " (GUID, Rohbytes 0-7 umgedreht)");
                }
                hex.clear();
            }
            if (ant && zeilen < 30) {
                std::printf("  EBX %s: %s\n", kv.first.c_str(), z.substr(0, 180).c_str());
                ++zeilen;
            }
            // Die Felder eines AntRef (AssetGuid, ProjectId) mit ausgeben.
            if (zk.find("antref {") != std::string::npos) {
                size_t c2 = a;
                for (int k = 0; k < 2 && c2 < text.size(); ++k) {
                    size_t e2 = text.find('\n', c2);
                    if (e2 == std::string::npos) e2 = text.size();
                    std::printf("  EBX %s:   %s\n", kv.first.c_str(), text.substr(c2, e2 - c2).c_str());
                    c2 = e2 + 1;
                }
            }
        }
    }
    std::printf("GESICHT %zu EBX im Bundle gelesen, %zu Verweis-Kandidaten\n", ebxZahl, kandidaten.size());
    fbanim::Quelle q;
    if (!q.Baue(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    // Ruhelage fuer den Abstand der FACIAL-Bones
    fbebx::Skelett sk;
    for (const auto& kv : idx.ebx) {
        if (klein(kv.first).find("characters/rigs/humanoids/walrus_humanmale") == std::string::npos) continue;
        std::vector<uint8_t> roh;
        fbebx::Datei e;
        if (spiel.HoleNachSha1(kv.second.sha1, roh, fehler) && e.Lies(roh, fehler)) { sk = fbebx::LiesSkelett(e); if (sk.gefunden) break; }
    }
    std::map<std::string, size_t> boneIndex;
    for (size_t i = 0; i < sk.namen.size(); ++i) boneIndex[sk.namen[i]] = i;
    size_t treffer = 0;
    for (const auto& kv : kandidaten) {
        fbanim::ClipEintrag ce;
        if (!q.FindeEintrag(kv.first, ce)) continue;
        ++treffer;
        std::printf("TREFFER %s aus %s -> %s \"%s\" %s in %s\n", kv.first.c_str(), kv.second.c_str(), ce.klasse.c_str(), ce.name.c_str(),
                    ce.codec.c_str(), q.BankName(ce.bank).c_str());
        {
            fbgd::Datensatz ds;
            if (q.Felder(ce, ds)) {
                size_t zeig = 0;
                for (const auto& fp : ds.felder) {
                    if (zeig++ >= 24) break;
                    const fbgd::Wert& w = fp.second;
                    if (w.art == fbgd::Wert::Art::Feld) std::printf("  FELD %s: Array %s n=%u\n", fp.first.c_str(), w.typ.c_str(), w.anzahl);
                    else if (w.art == fbgd::Wert::Art::Gleit) std::printf("  FELD %s: %g\n", fp.first.c_str(), static_cast<double>(w.gleit));
                    else if (w.art == fbgd::Wert::Art::Hex || w.art == fbgd::Wert::Art::Text) std::printf("  FELD %s: %s\n", fp.first.c_str(), w.text.c_str());
                    else std::printf("  FELD %s: %lld\n", fp.first.c_str(), static_cast<long long>(w.ganz));
                }
            }
        }
        if (ce.klasse != "RawAnimationAsset" && ce.klasse != "FrameAnimationAsset") continue;
        fbanim::Clip c;
        std::string f;
        if (!q.Entpacke(ce, c, f)) { std::printf("  nicht entpackbar: %s\n", f.c_str()); continue; }
        size_t facial = 0, weit = 0;
        double maxT = 0.0;
        for (const fbanim::Kanal& k : c.kanaele) {
            if (k.name.compare(0, 7, "FACIAL_") != 0) continue;
            ++facial;
            if (k.art != 't' || k.werte.size() < 3) continue;
            const std::string bone = k.name.substr(0, k.name.size() - 2);
            const auto it = boneIndex.find(bone);
            if (it == boneIndex.end()) continue;
            const fbebx::Lage& l = sk.lokal[it->second];
            const double dx = k.werte[0] - l.trans[0], dy = k.werte[1] - l.trans[1], dz = k.werte[2] - l.trans[2];
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > 0.0005) ++weit;
            maxT = std::max(maxT, d);
        }
        std::printf("  %zu Kanaele, %zu davon FACIAL, %zu FACIAL-Verschiebungen weiter als 0,5 mm von der Ruhelage, groesste %.2f mm; %zu Keys\n",
                    c.kanaele.size(), facial, weit, maxT * 1000.0, c.zeiten.size());
    }
    std::printf("GESICHT %zu Verweise treffen einen Bank-Eintrag\n", treffer);
    return 0;
}

// --ebxdump: eine EBX-Datei als Text (0.43.0) - fuer den Aufbau von
// BasePoseTransforms und den Lichtschwert-Emitter (Klingenlaenge, Farbe).
int EbxDump(int argc, char** argv) {
    const std::string spielordner = argv[2];
    std::string name = argv[3];
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    std::string cache;
    size_t grenze = 200;
    for (int i = 4; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
        if (std::strcmp(argv[i], "--max") == 0) grenze = static_cast<size_t>(std::atoi(argv[i + 1]));
    }
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }
    for (const auto& kv : idx.ebx) {
        std::string k = kv.first;
        for (char& c : k) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (k != name) continue;
        std::vector<uint8_t> roh;
        fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, fehler) || !e.Lies(roh, fehler)) { std::printf("EBXDUMP %s: %s\n", kv.first.c_str(), fehler.c_str()); return 1; }
        const std::string t = fbebx::AlsText(e, 0);
        std::printf("EBXDUMP %s (%zu Byte)\n", kv.first.c_str(), roh.size());
        size_t a = 0, z = 0;
        while (a < t.size() && z < grenze) {
            size_t b = t.find('\n', a);
            if (b == std::string::npos) b = t.size();
            std::printf("  %s\n", t.substr(a, b - a).c_str());
            a = b + 1;
            ++z;
        }
        if (a < t.size()) std::printf("  ... (gekuerzt nach %zu Zeilen)\n", grenze);
        return 0;
    }
    std::printf("EBXDUMP %s nicht im Index\n", argv[3]);
    return 1;
}

// --fahrzeuge (0.49.0): was es an Fahrzeugen gibt - je Fahrzeugordner
// (gameplay/vehicles/<art>/<name>/) die MeshSets, ein Skelett im Ordner und
// die Clips, deren Name den Fahrzeugnamen traegt (Bindestriche/Unterstriche
// weggelassen: at-st -> atst). Grundlage fuer den Fahrzeugimport.
int Fahrzeugmessung(int argc, char** argv) {
    const std::string spielordner = argv[2];
    std::string cache;
    for (int i = 3; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund)) {
        idx = fbindex::Index();
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    }
    auto klein = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return x; };
    auto dicht = [](const std::string& x) { std::string r; for (char c : x) if (c != '-' && c != '_' && c != ' ') r += c; return r; };
    const std::string wurzel = "gameplay/vehicles/";
    struct Fz { size_t meshsets = 0; std::string skelett; std::vector<std::string> beispiele; size_t clips = 0; };
    std::map<std::string, Fz> fz;
    for (const auto& kv : idx.res) {
        if (kv.second.resType != 0x49B156D4u) continue;
        const std::string n = klein(kv.first);
        if (n.compare(0, wurzel.size(), wurzel) != 0) continue;
        const size_t a1 = n.find('/', wurzel.size());
        if (a1 == std::string::npos) continue;
        const size_t a2 = n.find('/', a1 + 1);
        if (a2 == std::string::npos) continue;
        ++fz[n.substr(wurzel.size(), a2 - wurzel.size())].meshsets;
    }
    for (auto& kv : fz) {
        const std::string o = wurzel + kv.first + "/";
        for (const auto& e : idx.ebx) {
            const std::string n = klein(e.first);
            if (n.compare(0, o.size(), o) != 0 || n.find("destruction") != std::string::npos || n.find("/old/") != std::string::npos) continue;
            if (n.find("_ske") == std::string::npos && n.find("skeleton") == std::string::npos) continue;
            if (kv.second.skelett.empty() || n.size() < klein(kv.second.skelett).size()) kv.second.skelett = e.first;
        }
    }
    fbanim::Quelle q;
    if (!q.Baue(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    std::set<std::string> gezaehlt;
    for (const fbanim::ClipEintrag& c : q.Clips()) {
        const std::string a = dicht(klein(c.anzeige.empty() ? c.name : c.anzeige));
        for (auto& kv : fz) {
            const std::string name = dicht(kv.first.substr(kv.first.find('/') + 1));
            if (name.size() < 3 || a.find(name) == std::string::npos) continue;
            if (!gezaehlt.insert(kv.first + "|" + c.key).second) continue;
            ++kv.second.clips;
            if (kv.second.beispiele.size() < 3) kv.second.beispiele.push_back((c.anzeige.empty() ? c.name : c.anzeige) + " (" + c.codec + ")");
        }
    }
    size_t mitSkelett = 0, mitClips = 0;
    for (const auto& kv : fz) {
        if (!kv.second.skelett.empty()) ++mitSkelett;
        if (kv.second.clips) ++mitClips;
        std::string b;
        for (const std::string& x : kv.second.beispiele) b += "  " + x;
        std::printf("FAHRZEUG %-40s MeshSets %4zu  Skelett %-50s Clips %4zu%s\n", kv.first.c_str(), kv.second.meshsets,
                    kv.second.skelett.empty() ? "-" : kv.second.skelett.c_str(), kv.second.clips, b.c_str());
    }
    std::printf("FAHRZEUGE %zu Ordner, %zu mit Skelett, %zu mit Clips nach Namen\n", fz.size(), mitSkelett, mitClips);
    return 0;
}

} // namespace

// tools/codeclab.cpp (0.50.0)
int CodecLabor(int argc, char** argv);

// tools/bericht.cpp (1.14.0): eine Zeile je Figur und je Fahrzeug - wie viele
// Clips sie bekommen und wodurch. Damit laesst sich pruefen, dass wirklich
// jeder etwas bekommt.
int Bericht(int argc, char** argv);
int MeshProbe(int argc, char** argv);   // 1.41.0, tools/meshprobe.cpp
int EbxFeld(int argc, char** argv);     // 1.41.0, tools/meshprobe.cpp
int TeilBones(int argc, char** argv);   // 1.41.0, tools/teilbones.cpp
int BindProbe(int argc, char** argv);   // 1.41.0, tools/teilbones.cpp
int Wurzel(int argc, char** argv);      // 1.41.0, tools/teilbones.cpp
int EbxGuid(int argc, char** argv);     // 1.41.0, tools/meshprobe.cpp
int ClipCheck(int argc, char** argv);   // 1.42.0, tools/clipcheck.cpp
int SucheWert(int argc, char** argv);   // 1.42.0, tools/clipcheck.cpp
int SkelettTest(int argc, char** argv); // 1.42.0, tools/clipcheck.cpp

// Fahrzeugliste (0.82.0): Ordner, Art, MeshSets, Skelett, Animationssaetze -
// die Grundlage fuer den Fahrzeugimport (Fahrzeuge haengen nicht am
// Animationsautomaten der Figuren, siehe Labor Z6/Z10).
int Fahrzeugliste(int argc, char** argv) {
    const std::string spielordner = argv[2];
    std::string cache;
    for (int i = 3; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund))
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    std::vector<fbfahrzeug::Fahrzeug> fz = fbfahrzeug::Fahrzeuge(idx);
    // 0.91.0: fuer die Fahrzeuge mit Bodenbezug das echte Skelett pruefen
    // (Namenstreffer wie "atst_masterskeleton" tragen nicht immer Bones).
    for (fbfahrzeug::Fahrzeug& f : fz) {
        if (f.art != "ground") continue;
        const std::string echt = fbfahrzeug::SucheSkelett(spiel, idx, f.name);
        if (!echt.empty()) { f.skelett = echt; f.skelettFremd = fbfahrzeug::FahrzeugSchluessel(echt).find(f.name) == std::string::npos; }
    }
    // 0.84.0 Stufe 2: --extrahiere <name> <datei> baut das Modell eines
    // Fahrzeugs (alle Bundles seines Ordners, Skelett = sein Hauptskelett).
    std::string ziel, datei;
    for (int i = 3; i + 2 < argc; ++i)
        if (std::strcmp(argv[i], "--extrahiere") == 0) { ziel = argv[i + 1]; datei = argv[i + 2]; }
    // 0.92.0: --skelettsuche <name> zeigt ALLE EBX, deren Pfad den Namen
    // enthaelt, mit Objekttypen und Feldern - damit ist zu sehen, wie das
    // Skelett eines Fahrzeugs wirklich heisst und aussieht (beim AT-ST liefert
    // "atst_masterskeleton" kein SkeletonAsset).
    std::string suchName;
    for (int i = 3; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--skelettsuche") == 0) suchName = argv[i + 1];
    if (!suchName.empty()) {
        auto klein2 = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return x; };
        auto dicht2 = [](const std::string& x) { std::string r; for (char c : x) if (c != '-' && c != '_' && c != ' ') r += c; return r; };
        const std::string n2 = dicht2(klein2(suchName));
        size_t gezeigt = 0;
        for (const auto& kv : idx.ebx) {
            const std::string p2 = klein2(kv.first);
            if (dicht2(p2).find(n2) == std::string::npos) continue;
            const bool interessant = p2.find("skel") != std::string::npos || p2.find("_ske") != std::string::npos ||
                                     p2.find("rig") != std::string::npos || p2.find("bone") != std::string::npos;
            if (!interessant) continue;
            std::vector<uint8_t> roh;
            std::string f2;
            fbebx::Datei e;
            if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) { std::printf("SKELETTSUCHE %-70s nicht lesbar\n", kv.first.c_str()); continue; }
            std::string typen;
            size_t mitBoneNames = 0, bones = 0;
            for (const auto& o : e.Objekte()) {
                if (!o) continue;
                if (typen.size() < 200) typen += o->typ + " ";
                if (const fbebx::Wert* bn = o->Feldwert("BoneNames")) { ++mitBoneNames; bones = std::max(bones, bn->liste.size()); }
            }
            const fbebx::Skelett sk = fbebx::LiesSkelett(e);
            std::printf("SKELETTSUCHE %-70s Objekte %2zu  BoneNames-Objekte %zu (max %zu Bones)  LiesSkelett %s (%zu)  Typen: %s\n",
                        kv.first.c_str(), e.Objekte().size(), mitBoneNames, bones, sk.gefunden ? "ja" : "nein", sk.namen.size(), typen.c_str());
            if (++gezeigt >= 40) break;
        }
        if (gezeigt == 0) std::printf("SKELETTSUCHE %s: kein Eintrag mit Skelett-/Rig-Bezug gefunden\n", suchName.c_str());
        return 0;
    }
    if (!ziel.empty()) {
        const fbfahrzeug::Fahrzeug* f = nullptr;
        for (const fbfahrzeug::Fahrzeug& x : fz) if (x.name == ziel) f = &x;
        if (f == nullptr) { std::fprintf(stderr, "Fahrzeug %s nicht gefunden\n", ziel.c_str()); return 1; }
        // 0.91.0: Das Skelett aus der Liste ist nur ein Namenstreffer - hier
        // wird geprueft, ob es wirklich Bones liefert, sonst das beste andere.
        std::string skelett = f->skelett;
        {
            const std::string echt = fbfahrzeug::SucheSkelett(spiel, idx, f->name);
            if (!echt.empty() && echt != skelett) { std::printf("FAHRZEUG %s: Skelett %s liefert keine Bones, nehme %s\n", ziel.c_str(),
                                                                skelett.empty() ? "-" : skelett.c_str(), echt.c_str()); skelett = echt; }
        }
        if (skelett.empty()) { std::fprintf(stderr, "Fahrzeug %s hat kein Skelett\n", ziel.c_str()); return 1; }
        fbfigur::Modell m;
        size_t gebaut = 0;
        // 0.95.0: Die "static_donotuse"-Fassungen kommen nur mit, wenn das
        // Fahrzeug KEIN eigenes Hauptmesh hat. AT-RT: "atrt_mesh" ist da, die
        // statische Fassung waere eine Dublette. AT-ST: kein Hauptmesh - dann
        // ist die statische Fassung die einzige vollstaendige.
        // 0.96.0: Erst nur die Teile aus dem eigenen Ordner. Gibt es keine,
        // gelten alle eigenen Teile (Fahrzeuge ohne eigenen Ordner).
        bool nurKern = false;
        for (const std::string& x : f->meshsetNamen) if (fbfahrzeug::IstKernTeil(x, f->name)) { nurKern = true; break; }
        const bool mitStatisch = !fbfahrzeug::HatHauptmesh(f->meshsetNamen, f->name);
        std::printf("FAHRZEUG %s: %s\n", ziel.c_str(), mitStatisch ? "kein eigenes Hauptmesh - die statischen Fassungen kommen mit"
                                                                   : "eigenes Hauptmesh vorhanden - statische Fassungen bleiben draussen");
        {
            const int versuch = 0;
        // 0.86.0: Die Bundles der Fahrzeug-MeshSets sind LEVEL-Bundles - sie
        // enthalten alles, was auf der Karte steht (AT-ST: 34 Bundles, 204
        // Meshes ohne Bones; AT-RT: 6978 Meshes). Deshalb je MeshSet bauen und
        // nur dessen Meshes nehmen.
        std::printf("FAHRZEUG %s: Skelett %s, %zu MeshSets in %zu Bundles\n", ziel.c_str(), skelett.c_str(), f->meshsetNamen.size(),
                    f->bundles.size());
        for (const std::string& meshset : f->meshsetNamen) {
            if (!fbfahrzeug::IstEigenesTeil(meshset, f->name)) {          // 0.94.0: Requisiten, Kulissen, Effekte
                if (versuch == 0) std::printf("FAHRZEUGTEIL %-64s nicht vom Fahrzeug\n", meshset.c_str());
                continue;
            }
            if (nurKern && !fbfahrzeug::IstKernTeil(meshset, f->name)) {  // 0.96.0: andere Fassungen desselben Typs
                if (versuch == 0) std::printf("FAHRZEUGTEIL %-64s andere Fassung\n", meshset.c_str());
                continue;
            }
            if (!fbfahrzeug::IstFahrzeugteil(meshset, mitStatisch)) {     // 0.87.0: Wrack, Zerstoerung, Geschosse
                if (versuch == 0) std::printf("FAHRZEUGTEIL %-64s uebersprungen\n", meshset.c_str());   // 0.88.0: Namen zeigen
                continue;
            }
            fbfigur::Modell teil;
            std::string f2;
            if (!fbfigur::BaueMeshSet(spiel, idx, meshset, skelett, 0, false, teil, f2)) {
                std::printf("FAHRZEUGTEIL %-64s FEHLER %s\n", meshset.c_str(), f2.c_str());
                continue;
            }
            ++gebaut;
            std::string hinw;
            for (const std::string& h : teil.hinweise) if (hinw.size() < 120) hinw += "  " + h;
            std::printf("FAHRZEUGTEIL %-64s Bones %3zu  Meshes %2zu%s\n", meshset.c_str(), teil.bones.size(), teil.meshes.size(), hinw.c_str());
            for (const std::string& h : teil.hinweise) if (h.compare(0, 6, "TEILE ") == 0) std::printf("  %s\n", h.c_str());   // 1.41.0
            if (m.bones.empty()) m = std::move(teil);
            else {
                const size_t vorher = m.materialien.size();
                for (fbfigur::Mesh& me : teil.meshes) { if (me.materialId >= 0) me.materialId += static_cast<int32_t>(vorher); m.meshes.push_back(std::move(me)); }
                for (fbfigur::Material& ma : teil.materialien) m.materialien.push_back(std::move(ma));
            }
        }
        }
        if (gebaut == 0) { std::fprintf(stderr, "kein Teil von %s konnte gebaut werden\n", ziel.c_str()); return 1; }
        std::string f3;
        if (!fbfigur::SchreibeFbmodel(datei, m, f3)) { std::fprintf(stderr, "%s\n", f3.c_str()); return 1; }
        std::printf("FAHRZEUG %s: %zu von %zu MeshSets, %zu Bones, %zu Meshes -> %s\n", ziel.c_str(), gebaut, f->meshsetNamen.size(), m.bones.size(),
                    m.meshes.size(), datei.c_str());
        return 0;
    }
    std::map<std::string, size_t> jeArt;
    size_t mitSkelett = 0, mitAnim = 0, fremd = 0;
    for (const fbfahrzeug::Fahrzeug& f : fz) {
        ++jeArt[f.art];
        if (!f.skelett.empty()) { ++mitSkelett; if (f.skelettFremd) ++fremd; }
        if (!f.animsets.empty()) ++mitAnim;
        std::string a2;
        for (size_t i = 0; i < f.animsets.size() && i < 3; ++i) a2 += "  " + f.animsets[i];
        if (f.animsets.size() > 3) a2 += "  (+" + std::to_string(f.animsets.size() - 3) + ")";
        std::printf("FAHRZEUG %-34s Art %-12s MeshSets %4zu  Bundles %3zu  Skelett %-58s%s Animsets %2zu%s\n", f.name.c_str(), f.art.c_str(),
                    f.meshsets, f.bundles.size(), f.skelett.empty() ? "-" : f.skelett.c_str(), f.skelettFremd ? " (fremd)" : "        ",
                    f.animsets.size(), a2.c_str());
    }
    std::printf("FAHRZEUGE %zu, mit Skelett %zu (davon ausserhalb des Ordners %zu), mit Animationssatz %zu; je Art:", fz.size(), mitSkelett, fremd, mitAnim);
    for (const auto& kv : jeArt) std::printf(" %s %zu,", kv.first.c_str(), kv.second);
    std::printf("\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "castool <cas.cat>\n"
            "castool <cas.cat> --entpacke <nummer> <zieldatei> [woerterbuch]\n"
            "castool <cas.cat> --gepatcht <basis-sha1> <delta-sha1> <ziel> [woerterbuch]\n"
            "castool --db <datei>   (layout.toc, initfs_win32, ...)\n"
            "castool --fs <spielordner> [--sha1 <hex> <ziel>]\n"
            "castool --bundles <spielordner>\n"
            "castool --index <spielordner>\n"
            "castool --ablage <index.fbidx>   (Index ohne Spiel laden, Figurenliste)\n"
            "castool --material <spielordner> <bundle> [--cache <datei>]   (Materialien und Texturen messen)\n"
            "castool --texturen <spielordner> <bundle> <zielordner> [--cache <datei>]   (Texturen als PNG)\n"
            "castool --clips <spielordner> [--cache <datei>] [--liste <datei>]   (alle Animationsclips)\n"
            "castool --clipinfo <spielordner> <muster> [--cache <datei>] [--max n]   (Namenskette je Clip messen)\n"
            "castool --anim <spielordner> <zielordner> [--liste <datei>] [--muster <text>] [--max n] [--cache <datei>]\n"
            "castool --pose <spielordner> <bank-muster> [--max n] [--cache <datei>]   (Quaternion-Lesart messen)\n"
            "castool --waffe <spielordner> <figur> [--cache <datei>]   (Waffen der Figur messen)\n"
            "castool --clipablage <spielordner> <datei> [--cache <datei>]   (Clipverzeichnis: Ablage gegen Neubau)\n"
            "castool --gesicht <spielordner> <bundle> [--cache <datei>]   (Gesichtspose der Figur suchen)\n"
            "castool --ebxdump <spielordner> <ebx-name> [--max zeilen] [--cache <datei>]   (EBX als Text)\n"
            "castool --fahrzeuge <spielordner> [--cache <datei>]   (Fahrzeuge: MeshSets, Skelett, Clips je Ordner)\n"
            "castool --bericht <spielordner> [--cache <index>] [--clipcache <datei>] [--log <datei>]   (je Figur und Fahrzeug: wie viele Clips)\n"
            "castool --fahrzeugliste <spielordner> [--cache <datei>]   (Fahrzeuge: Ordner, Art, MeshSets, Skelett, Animationssaetze)\n"
            "castool --codeclab <spielordner> [--cache <index>] [--clipcache <datei>] [--log <datei>] [--hex <datei>] [--probe n]   (Codec-Labor VBR/DCT/CURV)\n"
            "castool --figuren <spielordner> [--extrahiere <name> <ziel.fbmodel>]\n"
            "                          [--cache <index.fbidx>]\n");
        return 2;
    }
    // --- Das ganze Spiel: Kataloge, Manifest, initfs ---------------------
    if (std::strcmp(argv[1], "--fs") == 0 && argc >= 3) {
        fbgame::Spiel spiel;
        std::string fehler;
        if (!spiel.Oeffne(argv[2], fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        std::printf("PFADE %zu\n", spiel.Pfade().size());
        std::printf("BASE %lld\n", static_cast<long long>(spiel.Basisnummer()));
        std::printf("HEAD %lld\n", static_cast<long long>(spiel.Kopfnummer()));
        std::printf("SUPERBUNDLES %zu\n", spiel.Superbundles().size());
        for (const std::string& sb : spiel.Superbundles()) std::printf("SB %s\n", sb.c_str());
        std::printf("KATALOGE %zu\n", spiel.Kataloge().size());
        for (const fbgame::Katalogangabe& k : spiel.Kataloge()) {
            std::printf("KAT %s %d\n", k.name.c_str(), k.immerInstalliert ? 1 : 0);
        }
        std::printf("MANIFEST %zu %zu %zu\n", spiel.Dateien().size(),
                    spiel.Bundles().size(), spiel.Chunks().size());
        std::printf("CATEINTRAEGE %zu\n", spiel.KatalogEintraege());
        std::printf("CATPATCHES %zu\n", spiel.KatalogPatches());
        const std::vector<uint8_t>* wb = spiel.SpeicherDatei("ebx.dict");
        std::printf("WOERTERBUCH %zu\n", wb ? wb->size() : 0);

        // Optional: eine SHA-1 entpacken und wegschreiben.
        if (argc >= 5 && std::strcmp(argv[3], "--sha1") == 0) {
            std::vector<uint8_t> aus;
            if (!spiel.HoleNachSha1(argv[4], aus, fehler)) {
                std::fprintf(stderr, "%s\n", fehler.c_str());
                return 1;
            }
            if (argc >= 6) {
                std::FILE* f = std::fopen(argv[5], "wb");
                if (f == nullptr) { std::fprintf(stderr, "kann %s nicht schreiben\n", argv[5]); return 1; }
                if (!aus.empty()) std::fwrite(aus.data(), 1, aus.size(), f);
                std::fclose(f);
            }
            std::fprintf(stderr, "%zu Byte entpackt\n", aus.size());
        }
        return 0;
    }

    // --- Materialien und Texturen einer Figur messen (Vorarbeit Stufe 4) --
    if (std::strcmp(argv[1], "--material") == 0 && argc >= 4) return Materialmessung(argc, argv);
    if (std::strcmp(argv[1], "--texturen") == 0 && argc >= 5) return Texturausgabe(argc, argv);
    if (std::strcmp(argv[1], "--clips") == 0 && argc >= 3) return Clipliste(argc, argv);
    if (std::strcmp(argv[1], "--clipinfo") == 0 && argc >= 4) return Clipinfo(argc, argv);
    if (std::strcmp(argv[1], "--anim") == 0 && argc >= 4) return Animausgabe(argc, argv);
    if (std::strcmp(argv[1], "--pose") == 0 && argc >= 4) return Posenvergleich(argc, argv);
    if (std::strcmp(argv[1], "--waffe") == 0 && argc >= 4) return Waffenmessung(argc, argv);
    if (std::strcmp(argv[1], "--clipablage") == 0 && argc >= 4) return Clipablage(argc, argv);
    if (std::strcmp(argv[1], "--gesicht") == 0 && argc >= 4) return Gesichtsmessung(argc, argv);
    if (std::strcmp(argv[1], "--ebxdump") == 0 && argc >= 4) return EbxDump(argc, argv);
    if (std::strcmp(argv[1], "--fahrzeuge") == 0 && argc >= 3) return Fahrzeugmessung(argc, argv);
    if (std::strcmp(argv[1], "--codeclab") == 0 && argc >= 3) return CodecLabor(argc, argv);
    if (std::strcmp(argv[1], "--fahrzeugliste") == 0 && argc >= 3) return Fahrzeugliste(argc, argv);   // 0.82.0
    if (std::strcmp(argv[1], "--bericht") == 0 && argc >= 3) return Bericht(argc, argv);                // 1.14.0
    if (std::strcmp(argv[1], "--meshprobe") == 0 && argc >= 5) return MeshProbe(argc, argv);            // 1.41.0
    if (std::strcmp(argv[1], "--ebxfeld") == 0 && argc >= 5) return EbxFeld(argc, argv);                // 1.41.0
    if (std::strcmp(argv[1], "--teilbones") == 0 && argc >= 5) return TeilBones(argc, argv);            // 1.41.0
    if (std::strcmp(argv[1], "--bindprobe") == 0 && argc >= 3) return BindProbe(argc, argv);            // 1.41.0
    if (std::strcmp(argv[1], "--wurzel") == 0 && argc >= 5) return Wurzel(argc, argv);                  // 1.41.0
    if (std::strcmp(argv[1], "--ebxguid") == 0 && argc >= 4) return EbxGuid(argc, argv);                // 1.41.0
    if (std::strcmp(argv[1], "--clipcheck") == 0 && argc >= 4) return ClipCheck(argc, argv);            // 1.42.0
    if (std::strcmp(argv[1], "--suchewert") == 0 && argc >= 4) return SucheWert(argc, argv);            // 1.42.0
    if (std::strcmp(argv[1], "--skeletttest") == 0 && argc >= 4) return SkelettTest(argc, argv);        // 1.42.0

    // --- Index aus der Ablage, OHNE Spiel -------------------------------
    // Zum Nachsehen und fuer die Gegenprobe ohne Spieldateien: die
    // Kopfnummer kommt aus der Datei selbst. Gibt dieselbe Figurenliste aus
    // wie --figuren - so laesst sich fbauswahl gegen eine alte figuren.txt
    // halten.
    if (std::strcmp(argv[1], "--ablage") == 0 && argc >= 3) {
        std::vector<uint8_t> kopf;
        std::string fehler;
        if (!fbdatei::LiesBereich(argv[2], 0, 16, kopf, fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        uint64_t nummer = 0;
        for (int i = 0; i < 8; ++i) nummer |= static_cast<uint64_t>(kopf[static_cast<size_t>(8 + i)]) << (8 * i);
        fbindex::Index idx;
        const auto t0 = std::chrono::steady_clock::now();
        if (!fbindex::LadeIndex(idx, argv[2], static_cast<int64_t>(nummer), fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        const double dauer = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("INDEX aus der Ablage (Kopfnummer %llu)\n", static_cast<unsigned long long>(nummer));
        std::printf("INDEX bundles=%zu ebx=%zu res=%zu chunks=%zu (%.1f s)\n",
                    idx.bundles.size(), idx.ebx.size(), idx.res.size(), idx.chunks.size(), dauer);
        const std::vector<fbauswahl::Figur> figuren = fbauswahl::Figuren(idx);
        std::printf("FIGUREN %zu davon HELDEN %zu\n", figuren.size(), fbauswahl::ZaehleHelden(figuren));
        for (const fbauswahl::Figur& fi : figuren) std::printf("%s\n", fbauswahl::Zeile(fi).c_str());
        return 0;
    }

    // --- Figuren: Bundles mit Charaktergeometrie -------------------------
    if (std::strcmp(argv[1], "--figuren") == 0 && argc >= 3) {
        fbgame::Spiel spiel;
        std::string fehler;
        if (!spiel.Oeffne(argv[2], fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        // Wenn ein Ablageort genannt ist: erst versuchen zu laden. Den Index
        // ueber 4.777 Bundles zu bauen dauert - fuer ein Plugin, das bei jedem
        // Import laeuft, zu lange.
        std::string cache;
        for (int i = 3; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], "--cache") == 0) { cache = argv[i + 1]; break; }
        }
        fbindex::Index idx;
        bool geladen = false;
        if (!cache.empty()) {
            std::string grund;
            geladen = fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund);
            if (!geladen) {
                idx = fbindex::Index();
                std::printf("INDEX neu gebaut (%s)\n", grund.c_str());
            } else {
                std::printf("INDEX aus der Ablage\n");
            }
        }
        const auto t0 = std::chrono::steady_clock::now();
        if (!geladen) {
            if (!fbindex::BaueIndex(spiel, idx, fehler)) {
                std::fprintf(stderr, "%s\n", fehler.c_str());
                return 1;
            }
            if (!cache.empty()) {
                std::string grund;
                if (!fbindex::SpeichereIndex(idx, cache, spiel.Kopfnummer(), grund)) {
                    std::printf("INDEX Ablage fehlgeschlagen: %s\n", grund.c_str());
                }
            }
        }
        const double dauer = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
        std::printf("INDEX bundles=%zu ebx=%zu res=%zu chunks=%zu (%.1f s)\n",
                    idx.bundles.size(), idx.ebx.size(), idx.res.size(), idx.chunks.size(), dauer);

        // Die Regel steht in fbauswahl - dieselbe Funktion benutzt das
        // Plugin fuer sein Fenster. So zeigt es garantiert dieselbe Liste,
        // die hier gegen fbtools geprueft wird.
        const std::vector<fbauswahl::Figur> figuren = fbauswahl::Figuren(idx);
        std::printf("FIGUREN %zu davon HELDEN %zu\n", figuren.size(), fbauswahl::ZaehleHelden(figuren));

        // Auf Wunsch gleich EINE Figur herausziehen. Der Index ist ohnehin
        // schon gebaut - ein zweiter Lauf waere reine Wartezeit.
        if (argc >= 6 && std::strcmp(argv[3], "--extrahiere") == 0) {
            fbfigur::Modell m;
            if (!fbfigur::BaueFigur(spiel, idx, argv[4],
                                    "Characters/Rigs/Humanoids/Walrus_HumanMale",
                                    0, false, m, fehler)) {
                std::fprintf(stderr, "%s\n", fehler.c_str());
                return 1;
            }
            size_t verts = 0, tris = 0;
            for (const fbfigur::Mesh& me : m.meshes) { verts += me.vertexCount; tris += me.dreiecke; }
            std::printf("FIGUR %s meshes=%zu vertices=%zu dreiecke=%zu bones=%zu materialien=%zu\n",
                        m.quelle.c_str(), m.meshes.size(), verts, tris,
                        m.bones.size(), m.materialien.size());
            if (m.schatten) std::printf("FIGUR schatten=%d\n", m.schatten);
            if (m.anders) std::printf("FIGUR zweiteDeklaration=%d\n", m.anders);
            for (const std::string& h : m.hinweise) std::printf("FIGUR hinweis %s\n", h.c_str());
            if (!fbfigur::SchreibeFbmodel(argv[5], m, fehler)) {
                std::fprintf(stderr, "%s\n", fehler.c_str());
                return 1;
            }
            std::printf("FIGUR geschrieben %s\n", argv[5]);
            {
                // 0.44.0: Gesichtspose aus dem BindPoseAsset, dazu die Gegenprobe
                // gegen den MainFace-Clip der Figur (bei DH repariert er das Gesicht).
                fbfigur::Basispose bp;
                std::string pf;
                fbanim::Quelle q;
                if (!q.Baue(spiel, idx, pf)) std::printf("FIGUR Clipverzeichnis: %s\n", pf.c_str());
                bool ok = fbgesicht::LiesGesichtspose(spiel, idx, q, m, bp);
                for (const std::string& z : bp.protokoll) std::printf("FIGUR %s\n", z.c_str());
                if (!ok) {
                    // 1.43.0: derselbe Rueckfall wie im Figurenfenster (BasePoseTransforms)
                    fbfigur::Basispose bp2;
                    ok = fbfigur::LiesBasispose(spiel, idx, m, bp2);
                    for (const std::string& z : bp2.protokoll) std::printf("FIGUR %s\n", z.c_str());
                    bp = bp2;
                }
                if (ok) {
                    fbfigur::SchreibeBasispose(std::string(argv[5]) + ".pose.txt", bp, pf);
                    for (int i = 6; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--waffen") == 0) fbfigur::SchreibeBasispose(std::string(argv[i + 1]) + ".pose.txt", bp, pf);
                    const std::string schluessel = fbfigur::FigurSchluessel(m.quelle);
                    for (const fbanim::ClipEintrag& ce : q.Clips()) {
                        std::string a2 = ce.anzeige;
                        for (char& c : a2) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                        if (a2.find("mainface") == std::string::npos || a2.find(schluessel) == std::string::npos) continue;
                        fbanim::Clip c;
                        std::string f2;
                        if (!q.Entpacke(ce, c, f2)) continue;
                        std::vector<double> dw, dt;
                        for (const fbfigur::PoseBone& pb2 : bp.bones) {
                            for (const fbanim::Kanal& k : c.kanaele) {
                                if (k.name == pb2.name + ".q" && k.werte.size() >= 4) {
                                    const double x = k.werte[0], y = k.werte[1], z = k.werte[2], w = k.werte[3];
                                    const double R[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                                                             { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                                                             { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
                                    double spur = 0;
                                    for (int r = 0; r < 3; ++r) for (int cc = 0; cc < 3; ++cc) spur += pb2.lokal[r * 3 + cc] * R[cc][r];
                                    dw.push_back(std::acos(std::max(-1.0, std::min(1.0, (spur - 1.0) / 2.0))) * 180.0 / 3.14159265358979);
                                } else if (k.name == pb2.name + ".t" && k.werte.size() >= 3) {
                                    const double dx = k.werte[0] - pb2.lokal[9], dy = k.werte[1] - pb2.lokal[10], dz = k.werte[2] - pb2.lokal[11];
                                    dt.push_back(std::sqrt(dx * dx + dy * dy + dz * dz));
                                }
                            }
                        }
                        auto med = [](std::vector<double> v) { if (v.empty()) return -1.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
                        std::printf("FIGUR GEGENPROBE %s (erster Key): Drehung %zu Bones, Median %.3f Grad; Verschiebung %zu Bones, Median %.3f mm\n",
                                    ce.anzeige.c_str(), dw.size(), med(dw), dt.size(), med(dt) * 1000.0);
                        break;
                    }
                }
            }
            // Dieselbe Figur MIT Waffen (0.42.0) - so, wie das Figurenfenster sie
            // ablegt; die Gegenprobe gegen fbtools laeuft weiter ohne Waffen.
            for (int i = 6; i + 1 < argc; ++i) {
                if (std::strcmp(argv[i], "--waffen") != 0) continue;
                fbfigur::Modell mw = m;
                std::vector<std::string> wp;
                fbfigur::FuegeWaffenHinzu(spiel, idx, mw, wp);
                for (const std::string& z : wp) std::printf("FIGUR %s\n", z.c_str());
                if (!fbfigur::SchreibeFbmodel(argv[i + 1], mw, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
                std::printf("FIGUR mit Waffen geschrieben %s (%zu Meshes)\n", argv[i + 1], mw.meshes.size());
            }
        }
        for (const fbauswahl::Figur& fi : figuren) {
            std::printf("%s\n", fbauswahl::Zeile(fi).c_str());
        }
        return 0;
    }

    // --- Index ueber alle Bundles ---------------------------------------
    if (std::strcmp(argv[1], "--index") == 0 && argc >= 3) {
        fbgame::Spiel spiel;
        std::string fehler;
        if (!spiel.Oeffne(argv[2], fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        fbindex::Index idx;
        if (!fbindex::BaueIndex(spiel, idx, fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        std::printf("INDEX bundles=%zu ebx=%zu res=%zu chunks=%zu\n",
                    idx.bundles.size(), idx.ebx.size(), idx.res.size(), idx.chunks.size());
        for (const auto& kv : idx.namensherkunft) {
            std::printf("NAMEN %s %d\n", kv.first.c_str(), kv.second);
        }
        for (const fbindex::BundleInfo& b : idx.bundles) {
            std::printf("B %zu %08x %s ebx=%zu res=%zu chunks=%zu\n",
                        b.nummer, b.hash, b.name.c_str(), b.ebx, b.res, b.chunks);
        }
        for (const auto& kv : idx.ebx) {
            std::printf("EBX %s %s %u\n", kv.first.c_str(), kv.second.sha1.c_str(),
                        kv.second.originalSize);
        }
        for (const auto& kv : idx.res) {
            std::printf("RES %s %s %u %08X %016llx\n", kv.first.c_str(),
                        kv.second.sha1.c_str(), kv.second.originalSize, kv.second.resType,
                        static_cast<unsigned long long>(kv.second.resRid));
        }
        for (const auto& kv : idx.chunks) {
            std::printf("CHUNK %s %s\n", kv.first.c_str(),
                        kv.second.sha1.empty() ? "-" : kv.second.sha1.c_str());
        }
        return 0;
    }

    // --- Alle Bundles auflisten -----------------------------------------
    if (std::strcmp(argv[1], "--bundles") == 0 && argc >= 3) {
        fbgame::Spiel spiel;
        std::string fehler;
        if (!spiel.Oeffne(argv[2], fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        std::printf("BUNDLES %zu\n", spiel.Bundles().size());
        for (size_t i = 0; i < spiel.Bundles().size(); ++i) {
            const fbgame::ManifestBundle& mb = spiel.Bundles()[i];
            if (mb.dateien.empty()) { std::printf("B %zu %08x LEER\n", i, mb.hash); continue; }
            std::vector<uint8_t> roh;
            if (!spiel.LiesBundleRoh(mb, roh, fehler)) {
                std::printf("B %zu %08x FEHLER %s\n", i, mb.hash, fehler.c_str());
                continue;
            }
            fbbundle::Bundle bu;
            if (!fbbundle::LiesBundle(roh, bu, fehler)) {
                std::printf("B %zu %08x FEHLER %s\n", i, mb.hash, fehler.c_str());
                continue;
            }
            std::printf("B %zu %08x ebx=%zu res=%zu chunks=%zu\n",
                        i, mb.hash, bu.ebx.size(), bu.res.size(), bu.chunks.size());
            for (const fbbundle::EbxRef& e : bu.ebx) {
                std::printf("  EBX %s %s %u\n", e.name.c_str(), e.sha1.c_str(), e.originalSize);
            }
            for (const fbbundle::ResRef& e : bu.res) {
                std::printf("  RES %s %s %u %08x %u\n", e.name.c_str(), e.sha1.c_str(),
                            e.originalSize, e.resType,
                            static_cast<unsigned>(e.resRid & 0xFFFFFFFFu));
            }
            for (const fbbundle::ChunkRef& c : bu.chunks) {
                std::printf("  CHUNK %s %u %u\n", c.sha1.c_str(), c.logicalOffset, c.logicalSize);
            }
        }
        return 0;
    }

    // --- DbObject-Baum ausgeben -----------------------------------------
    if (std::strcmp(argv[1], "--db") == 0 && argc >= 3) {
        std::vector<uint8_t> roh;
        std::string fehler;
        if (!fbcas::LiesDatei(argv[2], roh, fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        fbdb::WertPtr baum;
        if (!fbdb::LiesDbObjekt(roh, baum, fehler) || !baum) {
            std::fprintf(stderr, "%s\n", fehler.empty() ? "leerer Baum" : fehler.c_str());
            return 1;
        }
        const std::string t = fbdb::AlsText(*baum);
        std::fwrite(t.data(), 1, t.size(), stdout);
        return 0;
    }

    const std::string katPfad = argv[1];
    std::vector<uint8_t> roh;
    std::string fehler;
    if (!fbcas::LiesDatei(katPfad, roh, fehler)) {
        std::fprintf(stderr, "%s\n", fehler.c_str());
        return 1;
    }

    fbcas::Katalog kat;
    if (!fbcas::LiesKatalog(roh, kat, fehler)) {
        std::fprintf(stderr, "%s\n", fehler.c_str());
        return 1;
    }

    if (argc >= 6 && std::strcmp(argv[2], "--gepatcht") == 0) {
        // Basis und Delta ueber ihre SHA-1 im Katalog suchen und den
        // Deltapfad gehen - der fuenfte und letzte Entpackweg.
        const std::string basisSha = argv[3];
        const std::string deltaSha = argv[4];
        std::vector<uint8_t> basis, delta;
        for (const fbcas::CatEintrag& e : kat.eintraege) {
            const std::string s = fbcas::Sha1Text(e.sha1);
            if (s != basisSha && s != deltaSha) continue;
            char casName[64];
            std::snprintf(casName, sizeof casName, "/cas_%02d.cas", e.archiv);
            std::vector<uint8_t> cas;
            if (!fbcas::LiesDatei(OrdnerVon(katPfad) + casName, cas, fehler)) {
                std::fprintf(stderr, "%s\n", fehler.c_str());
                return 1;
            }
            if (e.versatz + e.laenge > cas.size()) {
                std::fprintf(stderr, "Eintrag liegt hinter dem Ende der cas-Datei\n");
                return 1;
            }
            std::vector<uint8_t> stueck(cas.begin() + e.versatz,
                                        cas.begin() + e.versatz + e.laenge);
            if (s == basisSha) basis = std::move(stueck);
            else delta = std::move(stueck);
        }
        if (basis.empty() || delta.empty()) {
            std::fprintf(stderr, "Basis oder Delta steht nicht im Katalog\n");
            return 1;
        }
        fbcas::BlockLeser leser;
        if (argc >= 7) {
            std::vector<uint8_t> woerterbuch;
            if (fbcas::LiesDatei(argv[6], woerterbuch, fehler)) {
                leser.SetzeWoerterbuch(std::move(woerterbuch));
            }
        }
        std::vector<uint8_t> aus;
        if (!leser.EntpackeGepatcht(basis, delta, aus, fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        std::FILE* f = std::fopen(argv[5], "wb");
        if (f == nullptr) { std::fprintf(stderr, "kann %s nicht schreiben\n", argv[5]); return 1; }
        if (!aus.empty()) std::fwrite(aus.data(), 1, aus.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "%zu Byte aus dem Deltapfad\n", aus.size());
        return 0;
    }

    if (argc >= 5 && std::strcmp(argv[2], "--entpacke") == 0) {
        const int nr = std::atoi(argv[3]);
        if (nr < 0 || static_cast<size_t>(nr) >= kat.eintraege.size()) {
            std::fprintf(stderr, "Eintrag %d gibt es nicht (0 bis %zu)\n",
                         nr, kat.eintraege.size() - 1);
            return 1;
        }
        const fbcas::CatEintrag& e = kat.eintraege[static_cast<size_t>(nr)];
        char casName[64];
        std::snprintf(casName, sizeof casName, "/cas_%02d.cas", e.archiv);
        std::vector<uint8_t> cas;
        if (!fbcas::LiesDatei(OrdnerVon(katPfad) + casName, cas, fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        if (e.versatz + e.laenge > cas.size()) {
            std::fprintf(stderr, "Eintrag liegt hinter dem Ende der cas-Datei\n");
            return 1;
        }
        const std::vector<uint8_t> stueck(cas.begin() + e.versatz,
                                          cas.begin() + e.versatz + e.laenge);
        fbcas::BlockLeser leser;
        if (argc >= 6) {
            std::vector<uint8_t> woerterbuch;
            if (fbcas::LiesDatei(argv[5], woerterbuch, fehler)) {
                leser.SetzeWoerterbuch(std::move(woerterbuch));
            }
        }
        std::vector<uint8_t> aus;
        if (!leser.EntpackeAlles(stueck, aus, fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        std::FILE* f = std::fopen(argv[4], "wb");
        if (f == nullptr) { std::fprintf(stderr, "kann %s nicht schreiben\n", argv[4]); return 1; }
        if (!aus.empty()) std::fwrite(aus.data(), 1, aus.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "%zu Byte entpackt\n", aus.size());
        return 0;
    }

    std::printf("KATALOG %s\n", katPfad.c_str());
    std::printf("START %ld\n", fbcas::FindeCatKennung(roh));
    std::printf("EINTRAEGE %zu\n", kat.eintraege.size());
    std::printf("PATCHES %zu\n", kat.patches.size());
    for (size_t i = 0; i < kat.eintraege.size(); ++i) {
        const fbcas::CatEintrag& e = kat.eintraege[i];
        std::printf("E %zu %s %u %u %u %d\n", i, fbcas::Sha1Text(e.sha1).c_str(),
                    e.versatz, e.laenge, e.logischerVersatz, e.archiv);
    }
    for (size_t i = 0; i < kat.patches.size(); ++i) {
        const fbcas::CatPatch& p = kat.patches[i];
        std::printf("P %zu %s %s %s\n", i, fbcas::Sha1Text(p.sha1).c_str(),
                    fbcas::Sha1Text(p.basisSha1).c_str(),
                    fbcas::Sha1Text(p.deltaSha1).c_str());
    }
    return 0;
}
