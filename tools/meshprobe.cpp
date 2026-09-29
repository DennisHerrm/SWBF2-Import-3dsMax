// ============================================================
//  meshprobe.cpp - castool --meshprobe (1.41.0)
//
//  Misst, worauf die BoneIndices eines MeshSets zeigen. Anlass: der AT-AT
//  hat Knochenindizes ohne Gewichtsstrom, und die Werte passen nicht zur
//  boneRefs-Tabelle der Section (Kopf 0..66 bei 17 Eintraegen).
//
//  Je Section werden mehrere Deutungen durchgerechnet und je Deutung der
//  mittlere Abstand jedes Vertex zu "seinem" Bone (ModelPose) gemessen.
//  Die richtige Deutung haengt Teile an nahe Bones; eine falsche verteilt
//  sie wahllos ueber das Skelett (Kopf an Hinterbeinen, wie in 1.39.0).
//
//    castool --meshprobe <spielordner> <meshset-teilname> <skelett-teilname>
//            [--cache <index.fbidx>] [--roh n]
// ============================================================
#include "fbebx.h"
#include "fbgame.h"
#include "fbindex.h"
#include "fbmeshset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool HolePuffer(fbgame::Spiel& spiel, const fbindex::Index& idx, const fbmesh::MeshSet& ms, size_t li,
                std::vector<uint8_t>& buf, std::string& f) {
    const fbmesh::Lod& lod = ms.lods[li];
    std::vector<uint8_t> puffer;
    bool haben = false;
    if (!lod.chunkId.empty() && lod.chunkId != "00000000-0000-0000-0000-000000000000") {
        const auto it = idx.chunks.find(lod.chunkId);
        if (it != idx.chunks.end() && !it->second.sha1.empty()) haben = spiel.HoleNachSha1(it->second.sha1, puffer, f);
        if (!haben) {
            for (const fbgame::ManifestChunk& mc : spiel.Chunks()) {
                char t[40];
                std::snprintf(t, sizeof t, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                              mc.guid[3], mc.guid[2], mc.guid[1], mc.guid[0], mc.guid[5], mc.guid[4], mc.guid[7], mc.guid[6],
                              mc.guid[8], mc.guid[9], mc.guid[10], mc.guid[11], mc.guid[12], mc.guid[13], mc.guid[14], mc.guid[15]);
                if (lod.chunkId == t) { haben = spiel.HoleManifestChunk(mc, puffer, f); break; }
            }
        }
    }
    if (!haben && !ms.inlineDaten.empty()) { puffer = ms.inlineDaten; haben = true; }
    if (!haben) { f = "kein Puffer"; return false; }
    const size_t versatz = fbmesh::LodVersatz(ms, li, puffer.size());
    const size_t brauch = lod.vertexBufferSize + lod.indexBufferSize;
    if (puffer.size() < versatz + brauch) { f = "Puffer zu kurz"; return false; }
    buf.assign(puffer.begin() + static_cast<long>(versatz), puffer.begin() + static_cast<long>(versatz + brauch));
    return true;
}

} // namespace

int MeshProbe(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "castool --meshprobe <spiel> <meshset> <skelett> [--cache f] [--roh n]\n"); return 2; }
    const std::string spielordner = argv[2], teil = Klein(argv[3]), skTeil = Klein(argv[4]);
    std::string cache;
    size_t roh = 6;
    for (int i = 5; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
        if (std::strcmp(argv[i], "--roh") == 0) roh = static_cast<size_t>(std::atoi(argv[i + 1]));
    }
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund))
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }

    // ---- Skelett: erster Eintrag mit Bones -----------------------------
    fbebx::Skelett sk;
    std::string skName;
    for (const auto& kv : idx.ebx) {
        if (Klein(kv.first).find(skTeil) == std::string::npos) continue;
        std::vector<uint8_t> d;
        std::string f;
        fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, d, f) || !e.Lies(d, f)) continue;
        fbebx::Skelett s = fbebx::LiesSkelett(e);
        if (!s.gefunden || s.namen.empty()) continue;
        sk = s; skName = kv.first;
        break;
    }
    if (sk.namen.empty()) { std::fprintf(stderr, "kein Skelett mit %s\n", skTeil.c_str()); return 1; }
    const size_t nb = sk.namen.size();
    // Weltlage je Bone: ModelPose, sonst aus LocalPose die Kette hoch.
    std::vector<std::array<double, 3>> weltPos(nb);
    const bool mitModell = sk.modell.size() == nb;
    {
        std::vector<std::array<double, 12>> w(nb);
        for (size_t i = 0; i < nb; ++i) {
            const fbebx::Lage& l = mitModell ? sk.modell[i] : (i < sk.lokal.size() ? sk.lokal[i] : fbebx::Lage());
            double L[12];
            for (int k = 0; k < 3; ++k) { L[k] = l.right[k]; L[3 + k] = l.up[k]; L[6 + k] = l.forward[k]; L[9 + k] = l.trans[k]; }
            const int e = i < sk.hierarchie.size() ? sk.hierarchie[i] : -1;
            if (mitModell || e < 0 || static_cast<size_t>(e) >= i) { std::copy(L, L + 12, w[i].data()); }
            else {
                const double* P = w[static_cast<size_t>(e)].data();
                for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c) {
                    double v = 0; for (int k = 0; k < 3; ++k) v += L[r * 3 + k] * P[k * 3 + c];
                    if (r == 3) v += P[9 + c];
                    w[i][static_cast<size_t>(r * 3 + c)] = v;
                }
            }
            weltPos[i] = { w[i][9], w[i][10], w[i][11] };
        }
    }
    std::printf("MESHPROBE Skelett %s: %zu Bones (%s)\n", skName.c_str(), nb, mitModell ? "ModelPose" : "aus LocalPose");

    size_t gezeigt = 0;
    for (const auto& kv : idx.res) {
        if (fbindex::ResTypName(kv.second.resType) != "MeshSet") continue;
        if (Klein(kv.first).find(teil) == std::string::npos) continue;
        std::vector<uint8_t> daten;
        std::string f;
        fbmesh::MeshSet ms;
        if (!spiel.HoleNachSha1(kv.second.sha1, daten, f) || !fbmesh::LiesMeshSet(daten, ms, f, kv.second.resMeta) || ms.lods.empty()) {
            std::printf("MESHPROBE %s: %s\n", kv.first.c_str(), f.c_str());
            continue;
        }
        std::printf("\nMESHPROBE %s  typ %u  flags 0x%x  LODs %zu  Kopf roh %u/%u  boneCount %u, Liste %zu:", kv.first.c_str(), ms.meshTypeId,
                    ms.flags, ms.lods.size(), ms.kopfWerte[0], ms.kopfWerte[1], ms.kopfBoneCount, ms.kopfBoneListe.size());
        for (size_t i = 0; i < ms.kopfBoneListe.size() && i < 160; ++i) std::printf(" %u", ms.kopfBoneListe[i]);
        std::printf("\n");
        for (size_t o = 0x60; o < 0xE0 && o + 16 <= daten.size(); o += 16) {
            std::printf("    %04zx:", o);
            for (size_t k = 0; k < 16; ++k) std::printf(" %02x", daten[o + k]);
            std::printf("\n");
        }
        std::vector<uint8_t> buf;
        if (!HolePuffer(spiel, idx, ms, 0, buf, f)) { std::printf("  LOD0: %s\n", f.c_str()); continue; }
        // Composite: je Teil die Lage und der naechste Bone (Verschiebung und Drehung).
        if (!ms.teilTransformen.empty()) {
            std::printf("  Teile: %zu\n", ms.teilTransformen.size());
            size_t exakt = 0;
            for (size_t t = 0; t < ms.teilTransformen.size(); ++t) {
                const auto& m = ms.teilTransformen[t];
                double best = 1e30; size_t bb = 0;
                for (size_t b = 0; b < nb; ++b) {
                    const double dx = m[12] - weltPos[b][0], dy = m[13] - weltPos[b][1], dz = m[14] - weltPos[b][2];
                    const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (d < best) { best = d; bb = b; }
                }
                double drehung = -1;
                if (mitModell) {
                    const fbebx::Lage& l = sk.modell[bb];
                    // Spur von R_teil * R_bone^T -> Winkel
                    const double* z[3] = { l.right, l.up, l.forward };
                    double spur = 0;
                    for (int r = 0; r < 3; ++r) for (int k = 0; k < 3; ++k) spur += m[static_cast<size_t>(r * 4 + k)] * z[r][k];
                    drehung = std::acos(std::max(-1.0, std::min(1.0, (spur - 1.0) / 2.0))) * 180.0 / 3.14159265358979;
                }
                if (best < 0.001 && drehung >= 0 && drehung < 0.5) ++exakt;
                if (t < 70) std::printf("    T%-3zu trans %8.3f %8.3f %8.3f  pad %g %g %g %g  naechster Bone %-28s %7.4f m  Drehung %6.2f Grad\n", t,
                                        m[12], m[13], m[14], m[3], m[7], m[11], m[15], sk.namen[bb].c_str(), best, drehung);
            }
            std::printf("  Teile exakt auf einem Bone (<1 mm, <0,5 Grad): %zu von %zu\n", exakt, ms.teilTransformen.size());
            // Schwerpunkt der Vertices je Teil (alle Sections): liegt er bei der
            // Teillage (Modellraum) oder beim Ursprung (Teilraum)?
            std::map<int, std::array<double, 4>> schwer;
            for (const fbmesh::Section& sec : ms.lods[0].sections) {
                if (fbmesh::IstTiefenSection(sec)) continue;
                std::vector<fbmesh::Vertex> verts;
                int decl = 0;
                fbmesh::Bewertung note;
                fbmesh::LiesVerticesGemessen(sec, buf, verts, decl, note);
                for (const fbmesh::Vertex& v : verts) {
                    const auto it = v.find("BoneIndices");
                    const auto p = v.find("Pos");
                    if (it == v.end() || p == v.end() || it->second.empty() || p->second.size() < 3) continue;
                    auto& s = schwer[static_cast<int>(std::lround(it->second[0]))];
                    s[0] += p->second[0]; s[1] += p->second[1]; s[2] += p->second[2]; s[3] += 1;
                }
            }
            for (const auto& kv2 : schwer) {
                const auto& s = kv2.second;
                const size_t t = static_cast<size_t>(kv2.first);
                const double cx = s[0] / s[3], cy = s[1] / s[3], cz = s[2] / s[3];
                double dt = -1;
                if (t < ms.teilTransformen.size()) {
                    const auto& m = ms.teilTransformen[t];
                    dt = std::sqrt((cx - m[12]) * (cx - m[12]) + (cy - m[13]) * (cy - m[13]) + (cz - m[14]) * (cz - m[14]));
                }
                std::printf("    Teil %2zu: %6.0f Vertices, Schwerpunkt %7.3f %7.3f %7.3f, Abstand zur Teillage %.3f m, zum Ursprung %.3f m\n", t, s[3], cx, cy, cz,
                            dt, std::sqrt(cx * cx + cy * cy + cz * cz));
            }
        }
        for (const fbmesh::Section& sec : ms.lods[0].sections) {
            if (fbmesh::IstTiefenSection(sec)) continue;
            std::vector<fbmesh::Vertex> verts;
            int decl = 0;
            fbmesh::Bewertung note;
            fbmesh::LiesVerticesGemessen(sec, buf, verts, decl, note);
            std::string fmt;
            for (const fbmesh::Element& e : sec.decls[decl].elements)
                if (e.usage != 0) fmt += " " + e.usageName + ":" + e.formatName + "@" + std::to_string(e.offset) + "/s" + std::to_string(e.streamIndex);
            std::printf("  S%d %-28s vtx %u bpv %u decl %d boneList %zu:", sec.index, sec.materialName.c_str(), sec.vertexCount,
                        sec.bonesPerVertex, decl, sec.boneList.size());
            for (uint16_t b : sec.boneList) std::printf(" %u", b);
            std::printf("\n    Elemente:%s\n", fmt.c_str());
            // Rohwerte
            for (size_t i = 0; i < verts.size() && i < roh; ++i) {
                std::printf("    v%zu", i);
                for (const char* k : { "BoneIndices", "BoneWeights", "Pos" }) {
                    const auto it = verts[i].find(k);
                    if (it == verts[i].end()) continue;
                    std::printf("  %s", k);
                    for (double x : it->second) std::printf(" %g", x);
                }
                std::printf("\n");
            }
            // Histogramm Platz 0 und Deutungen
            std::map<int, size_t> hist;
            size_t mitIdx = 0;
            for (const fbmesh::Vertex& v : verts) {
                const auto it = v.find("BoneIndices");
                if (it == v.end() || it->second.empty()) continue;
                ++mitIdx;
                ++hist[static_cast<int>(std::lround(it->second[0]))];
            }
            if (mitIdx == 0) { std::printf("    keine BoneIndices\n"); continue; }
            std::printf("    Platz0: %zu verschiedene Werte:", hist.size());
            size_t z = 0;
            for (const auto& h : hist) { if (++z > 40) { std::printf(" ..."); break; } std::printf(" %d(%zu)", h.first, h.second); }
            std::printf("\n");
            struct Deutung { const char* name; std::vector<int> abb; };
            std::vector<Deutung> deutungen;
            const int maxWert = hist.rbegin()->first;
            auto baue = [&](const char* n, auto fn) { Deutung d{ n, {} }; for (int w = 0; w <= maxWert; ++w) d.abb.push_back(fn(w)); deutungen.push_back(d); };
            baue("section[w]", [&](int w) { return w < static_cast<int>(sec.boneList.size()) ? sec.boneList[static_cast<size_t>(w)] : -1; });
            baue("kopf[w]", [&](int w) { return w < static_cast<int>(ms.kopfBoneListe.size()) ? ms.kopfBoneListe[static_cast<size_t>(w)] : -1; });
            baue("skelett[w]", [&](int w) { return w < static_cast<int>(nb) ? w : -1; });
            baue("section[w/3]", [&](int w) { return w / 3 < static_cast<int>(sec.boneList.size()) ? sec.boneList[static_cast<size_t>(w / 3)] : -1; });
            baue("section[w/4]", [&](int w) { return w / 4 < static_cast<int>(sec.boneList.size()) ? sec.boneList[static_cast<size_t>(w / 4)] : -1; });
            for (const Deutung& d : deutungen) {
                double summe = 0; size_t n = 0, draussen = 0;
                std::map<int, size_t> jeBone;
                for (const fbmesh::Vertex& v : verts) {
                    const auto it = v.find("BoneIndices");
                    const auto p = v.find("Pos");
                    if (it == v.end() || p == v.end() || it->second.empty() || p->second.size() < 3) continue;
                    const int w = static_cast<int>(std::lround(it->second[0]));
                    const int b = (w >= 0 && w < static_cast<int>(d.abb.size())) ? d.abb[static_cast<size_t>(w)] : -1;
                    if (b < 0 || static_cast<size_t>(b) >= nb) { ++draussen; continue; }
                    const double dx = p->second[0] - weltPos[static_cast<size_t>(b)][0];
                    const double dy = p->second[1] - weltPos[static_cast<size_t>(b)][1];
                    const double dz = p->second[2] - weltPos[static_cast<size_t>(b)][2];
                    summe += std::sqrt(dx * dx + dy * dy + dz * dz); ++n;
                    ++jeBone[b];
                }
                std::vector<std::pair<size_t, int>> top;
                for (const auto& jb : jeBone) top.push_back({ jb.second, jb.first });
                std::sort(top.rbegin(), top.rend());
                std::printf("    %-13s Abstand %7.3f m  gebunden %zu, ausserhalb %zu  |", d.name, n ? summe / static_cast<double>(n) : -1.0, n, draussen);
                for (size_t i = 0; i < top.size() && i < 4; ++i) std::printf(" %s(%zu)", sk.namen[static_cast<size_t>(top[i].second)].c_str(), top[i].first);
                std::printf("\n");
            }
        }
        if (++gezeigt >= 12) break;
    }
    if (gezeigt == 0) std::printf("MESHPROBE kein MeshSet mit %s\n", teil.c_str());
    return 0;
}

// ------------------------------------------------------------
//  castool --ebxfeld <spiel> <pfadteil> <feldmuster[,muster2]> [--cache f]
//  Alle EBX, deren Pfad <pfadteil> enthaelt: jedes Feld, dessen Name eines
//  der Muster enthaelt, mit Objekttyp und Wert. Zum Finden der Zuordnung
//  Teil -> Bone bei Composite-Meshes (1.41.0).
// ------------------------------------------------------------
namespace {

void FeldSuche(const fbebx::Wert& w, const std::string& pfad, const std::vector<std::string>& muster, size_t& zeilen) {
    for (const fbebx::Feld& f : w.felder) {
        if (!f.wert) continue;
        const std::string n = Klein(f.name);
        bool treffer = false;
        for (const std::string& m : muster) if (n.find(m) != std::string::npos) treffer = true;
        const std::string p = pfad + "." + f.name;
        if (treffer && zeilen < 4000) {
            const fbebx::Wert& v = *f.wert;
            std::string wert;
            switch (v.art) {
                case fbebx::Art::Bool: wert = v.wahr ? "true" : "false"; break;
                case fbebx::Art::Ganz: wert = std::to_string(v.zahl); break;
                case fbebx::Art::Gleit: wert = std::to_string(v.gleit); break;
                case fbebx::Art::Text: case fbebx::Art::Guid: case fbebx::Art::Sha1: case fbebx::Art::Import: wert = v.text; break;
                case fbebx::Art::Zeiger: wert = "-> " + std::to_string(v.verweis); break;
                case fbebx::Art::Liste: {
                    wert = "[" + std::to_string(v.liste.size()) + "]";
                    for (size_t i = 0; i < v.liste.size() && i < 140; ++i) {
                        const fbebx::Wert* e = v.liste[i].get();
                        if (!e) continue;
                        if (e->art == fbebx::Art::Ganz) wert += " " + std::to_string(e->zahl);
                        else if (e->art == fbebx::Art::Text) wert += " " + e->text;
                        else if (e->art == fbebx::Art::Gleit) wert += " " + std::to_string(e->gleit);
                        else if (e->art == fbebx::Art::Zeiger) wert += " ->" + std::to_string(e->verweis);
                        else if (e->art == fbebx::Art::Objekt) wert += " {" + e->typ + "}";
                    }
                    break;
                }
                case fbebx::Art::Objekt: wert = "{" + v.typ + "}"; break;
                default: break;
            }
            std::printf("    %s = %s\n", p.c_str(), wert.c_str());
            ++zeilen;
        }
        if (f.wert->art == fbebx::Art::Objekt) FeldSuche(*f.wert, p, muster, zeilen);
        if (f.wert->art == fbebx::Art::Liste)
            for (size_t i = 0; i < f.wert->liste.size() && i < 400; ++i)
                if (f.wert->liste[i] && f.wert->liste[i]->art == fbebx::Art::Objekt)
                    FeldSuche(*f.wert->liste[i], p + "[" + std::to_string(i) + "]", muster, zeilen);
    }
}

} // namespace

int EbxFeld(int argc, char** argv) {
    if (argc < 5) return 2;
    const std::string spielordner = argv[2], teil = Klein(argv[3]);
    std::vector<std::string> muster;
    { std::string m = Klein(argv[4]); size_t a = 0; for (size_t i = 0; i <= m.size(); ++i) if (i == m.size() || m[i] == ',') { muster.push_back(m.substr(a, i - a)); a = i + 1; } }
    std::string cache;
    for (int i = 5; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund))
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    size_t zeilen = 0;
    for (const auto& kv : idx.ebx) {
        if (Klein(kv.first).find(teil) == std::string::npos) continue;
        std::vector<uint8_t> d;
        std::string f;
        fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, d, f) || !e.Lies(d, f)) continue;
        const size_t vorher = zeilen;
        std::string typen;
        for (size_t o = 0; o < e.Objekte().size(); ++o) {
            const auto& obj = e.Objekte()[o];
            if (!obj) continue;
            const size_t z0 = zeilen;
            FeldSuche(*obj, "#" + std::to_string(o) + " " + obj->typ, muster, zeilen);
            (void)z0;
        }
        if (zeilen > vorher) std::printf("  ^^ EBX %s\n", kv.first.c_str());
    }
    std::printf("EBXFELD %zu Zeilen\n", zeilen);
    return 0;
}

// castool --ebxguid <spiel> <pfadteil> [--cache f]: Datei-GUID und erster Objekttyp je EBX (1.41.0)
int EbxGuid(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::string spielordner = argv[2], teil = Klein(argv[3]);
    std::string cache;
    for (int i = 4; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund))
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    for (const auto& kv : idx.ebx) {
        if (Klein(kv.first).find(teil) == std::string::npos) continue;
        std::vector<uint8_t> d; std::string f; fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, d, f) || !e.Lies(d, f)) continue;
        std::printf("EBXGUID %s  %-28s %s\n", e.DateiGuid().c_str(), (!e.Objekte().empty() && e.Objekte()[0]) ? e.Objekte()[0]->typ.c_str() : "-", kv.first.c_str());
    }
    return 0;
}
