// ============================================================
//  meshtool - ein MeshSet lesen und als Text ausgeben.
//
//    meshtool <datei.res>
//    meshtool <datei.res> --vertices <chunk> <lod>
//
//  Die Textfassung ist genau das, was tools/VERGLEICHE_MESH.py
//  aus fb_meshset erzeugt.
// ============================================================
#include "fbmeshset.h"
#include "fbcas.h"
#include "fbebx.h"
#include "fbgd.h"
#include "fbgdwerte.h"
#include "fbfigur.h"
#include "fbdump.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "meshtool <datei.res> [--vertices <chunk> <lod>]\n"
                             "meshtool --ebx <datei.ebx> [grenze]\n"
                             "meshtool --gd <bank.res>\n"
                             "meshtool --skelett <datei.ebx>\n"
                             "meshtool --umschreiben <in.fbmodel> <out.fbmodel>\n");
        return 2;
    }
    // --- Den .fbmodel-Schreiber pruefen ----------------------------------
    //
    //  Eine echte .fbmodel einlesen und mit unserem Schreiber wieder
    //  ausgeben. Kommt Byte fuer Byte dasselbe heraus, stimmt das Format -
    //  und zwar ohne Spieldateien, nur mit dem Dump.
    if (std::strcmp(argv[1], "--umschreiben") == 0 && argc >= 4) {
        std::vector<uint8_t> roh;
        std::string f;
        if (!fbcas::LiesDatei(argv[2], roh, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
        fb::Model alt;
        if (!fb::readModel(roh, alt, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }

        fbfigur::Modell m;
        m.quelle = alt.source;
        m.skelett = alt.skeleton;
        m.einheit = alt.unit;
        for (const fb::Bone& b : alt.bones) {
            fbfigur::Bone n;
            n.name = b.name; n.eltern = b.parent; n.typ = b.type;
            for (int i = 0; i < 12; ++i) n.ruhelage[i] = b.rest[i];
            m.bones.push_back(n);
        }
        for (const fb::Material& mm : alt.materials) {
            fbfigur::Material n;
            n.name = mm.name;
            for (const std::string& t : mm.textures) n.texturen.push_back(t);
            m.materialien.push_back(n);
        }
        for (const fb::Mesh& me : alt.meshes) {
            fbfigur::Mesh n;
            n.name = me.name; n.lod = static_cast<int32_t>(me.lod); n.material = me.material;
            n.vertexCount = me.vertexCount; n.dreiecke = me.triangleCount;
            n.sectionIndex = static_cast<int32_t>(me.sectionIndex);
            n.bonesJeVertex = me.bonesPerVertex;
            n.tiefe = static_cast<int32_t>(me.depth);
            n.boneRefs = me.boneRefs;
            n.indices = me.indices;
            const std::vector<float>* gleit[10] = {
                &me.pos, &me.normal, &me.tangent, &me.uv0, &me.uv1, &me.color,
                nullptr, &me.weight, nullptr, &me.weight2 };
            std::vector<double>* ziel[10] = {
                &n.pos, &n.normal, &n.tangent, &n.uv0, &n.uv1, &n.farbe,
                nullptr, &n.boneWgt, nullptr, &n.boneWgt2 };
            const uint32_t bits[10] = { fb::STREAM_POS, fb::STREAM_NORMAL, fb::STREAM_TANGENT,
                                        fb::STREAM_UV0, fb::STREAM_UV1, fb::STREAM_COLOR,
                                        fb::STREAM_BONEIDX, fb::STREAM_BONEWGT,
                                        fb::STREAM_BONEIDX2, fb::STREAM_BONEWGT2 };
            for (int q = 0; q < 10; ++q) {
                if (!me.has(bits[q])) continue;
                n.hat[q] = true;
                if (q == 6) n.boneIdx = me.boneIndex;
                else if (q == 8) n.boneIdx2 = me.boneIndex2;
                else if (gleit[q] != nullptr) {
                    for (float x : *gleit[q]) ziel[q]->push_back(static_cast<double>(x));
                }
            }
            m.meshes.push_back(std::move(n));
        }
        if (!fbfigur::SchreibeFbmodel(argv[3], m, f)) {
            std::fprintf(stderr, "%s\n", f.c_str());
            return 1;
        }
        std::fprintf(stderr, "geschrieben: %s\n", argv[3]);
        return 0;
    }

    // --- Skelett aus einem EBX ------------------------------------------
    if (std::strcmp(argv[1], "--skelett") == 0 && argc >= 3) {
        std::vector<uint8_t> roh;
        std::string f;
        if (!fbcas::LiesDatei(argv[2], roh, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
        fbebx::Datei e;
        if (!e.Lies(roh, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
        const fbebx::Skelett sk = fbebx::LiesSkelett(e);
        if (!sk.gefunden) { std::fprintf(stderr, "kein SkeletonAsset darin\n"); return 1; }
        std::printf("SKELETT %s BONES %zu\n", sk.name.c_str(), sk.namen.size());
        for (size_t i = 0; i < sk.namen.size(); ++i) {
            const int32_t e2 = (i < sk.hierarchie.size()) ? sk.hierarchie[i] : -1;
            std::printf("B %zu %s eltern=%d", i, sk.namen[i].c_str(), e2);
            if (i < sk.lokal.size()) {
                const fbebx::Lage& l = sk.lokal[i];
                const double* z[4] = { l.right, l.up, l.forward, l.trans };
                for (int r = 0; r < 4; ++r) {
                    for (int k = 0; k < 3; ++k) std::printf(" %.6f", z[r][k]);
                }
            }
            std::printf("\n");
        }
        return 0;
    }

    // --- GD-Bank --------------------------------------------------------
    if (std::strcmp(argv[1], "--gd-werte") == 0 && argc >= 3) {
        std::vector<uint8_t> roh;
        std::string f;
        if (!fbcas::LiesDatei(argv[2], roh, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
        fbgd::Bank b;
        if (!fbgd::LiesBank(roh, b, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
        const std::string t = fbgd::WerteAlsText(roh, b);
        std::fwrite(t.data(), 1, t.size(), stdout);
        return 0;
    }
    if (std::strcmp(argv[1], "--gd") == 0 && argc >= 3) {
        std::vector<uint8_t> roh;
        std::string f;
        if (!fbcas::LiesDatei(argv[2], roh, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
        fbgd::Bank b;
        if (!fbgd::LiesBank(roh, b, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
        const std::string t = fbgd::AlsText(b);
        std::fwrite(t.data(), 1, t.size(), stdout);
        return 0;
    }

    // --- EBX ------------------------------------------------------------
    if (std::strcmp(argv[1], "--ebx") == 0 && argc >= 3) {
        std::vector<uint8_t> roh;
        std::string f;
        if (!fbcas::LiesDatei(argv[2], roh, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
        fbebx::Datei e;
        if (!e.Lies(roh, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
        const size_t grenze = (argc >= 4) ? static_cast<size_t>(std::atoi(argv[3])) : 0;
        const std::string t = fbebx::AlsText(e, grenze);
        std::fwrite(t.data(), 1, t.size(), stdout);
        return 0;
    }

    std::vector<uint8_t> daten;
    std::string fehler;
    if (!fbcas::LiesDatei(argv[1], daten, fehler)) {
        std::fprintf(stderr, "%s\n", fehler.c_str());
        return 1;
    }
    fbmesh::MeshSet ms;
    if (!fbmesh::LiesMeshSet(daten, ms, fehler)) {
        std::fprintf(stderr, "%s\n", fehler.c_str());
        return 1;
    }

    if (argc >= 5 && std::strcmp(argv[2], "--vertices") == 0) {
        std::vector<uint8_t> chunk;
        if (!fbcas::LiesDatei(argv[3], chunk, fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        const size_t li = static_cast<size_t>(std::atoi(argv[4]));
        if (li >= ms.lods.size()) { std::fprintf(stderr, "LOD %zu gibt es nicht\n", li); return 1; }
        const fbmesh::Lod& lod = ms.lods[li];
        const size_t off = fbmesh::LodVersatz(ms, li, chunk.size());
        const size_t laenge = lod.vertexBufferSize + lod.indexBufferSize;
        if (off + laenge > chunk.size()) {
            std::fprintf(stderr, "Chunk zu kurz: Versatz %zu, Laenge %zu, habe %zu\n",
                         off, laenge, chunk.size());
            return 1;
        }
        const std::vector<uint8_t> puffer(chunk.begin() + static_cast<long>(off),
                                          chunk.begin() + static_cast<long>(off + laenge));
        std::printf("VERSATZ %zu\n", off);
        for (const fbmesh::Section& s : lod.sections) {
            std::vector<fbmesh::Vertex> verts;
            int decl = 0;
            fbmesh::Bewertung note;
            fbmesh::LiesVerticesGemessen(s, puffer, verts, decl, note);
            std::printf("SEC %d decl=%d punkte=%.4f pos=%.4f gew=%.4f uv=%.4f n=%zu schatten=%d\n",
                        s.index, decl, note.punkte, note.pos, note.gewichte, note.uv,
                        note.anzahl, fbmesh::IstSchattengeometrie(s, verts) ? 1 : 0);
            // Erster und letzter Vertex, damit ein Zahlendreher auffaellt.
            for (size_t k : { size_t(0), verts.empty() ? size_t(0) : verts.size() - 1 }) {
                if (verts.empty()) break;
                std::printf("  V %zu", k);
                for (const auto& kv : verts[k]) {
                    std::printf(" %s=", kv.first.c_str());
                    for (size_t q = 0; q < kv.second.size(); ++q) {
                        std::printf("%s%.6f", q ? "," : "", kv.second[q]);
                    }
                }
                std::printf("\n");
                if (verts.size() == 1) break;
            }
            std::vector<uint32_t> idx;
            if (fbmesh::LiesIndices(lod, puffer, s, idx, fehler)) {
                std::printf("  IDX %zu", idx.size());
                for (size_t q = 0; q < idx.size() && q < 6; ++q) std::printf(" %u", idx[q]);
                std::printf("\n");
            } else {
                std::printf("  IDX FEHLER %s\n", fehler.c_str());
            }
        }
        return 0;
    }

    const std::string t = fbmesh::AlsText(ms);
    std::fwrite(t.data(), 1, t.size(), stdout);
    return 0;
}
