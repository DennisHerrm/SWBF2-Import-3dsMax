// ============================================================
//  teilbones.cpp - castool --teilbones (1.41.0)
//
//  Composite-Fahrzeuge (AT-AT): Die BoneIndices im Vertex sind TEILNUMMERN
//  (Frosty FBXExporter: Composite -> Gewicht 1 auf Teil boneIndices[0]).
//  Welcher Bone ein Teil bewegt, steht im Fahrzeug-Blueprint:
//  MultiBodyPhysicsComponentData.Parts[i].TransformNode zeigt auf ein
//  PartComponentData, das als Kind (Components) unter einem
//  BoneComponentData haengt. Hier wird jede BoneComponentData-Weltlage aus
//  der Kette berechnet und gegen die ModelPose des Skeletts gehalten.
//
//    castool --teilbones <spiel> <blueprint-ebx> <skelett> [--cache f]
// ============================================================
#include "fbebx.h"
#include "fbgame.h"
#include "fbindex.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

std::string Klein2(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

struct M12 { double m[12]; };

M12 AusLage(const fbebx::Wert* t) {
    M12 r{ { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 } };
    if (!t) return r;
    const char* z[4] = { "right", "up", "forward", "trans" };
    const char* k[3] = { "x", "y", "z" };
    for (int i = 0; i < 4; ++i) {
        const fbebx::Wert* v = t->Feldwert(z[i]);
        if (!v) continue;
        for (int j = 0; j < 3; ++j) {
            const fbebx::Wert* c = v->Feldwert(k[j]);
            if (c) r.m[i * 3 + j] = c->gleit;
        }
    }
    return r;
}

M12 Mal12(const M12& a, const M12& b) {            // a * b (Zeilenvektoren): Kind * Eltern
    M12 n{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) {
            double v = 0;
            for (int k = 0; k < 3; ++k) v += a.m[r * 3 + k] * b.m[k * 3 + c];
            if (r == 3) v += b.m[9 + c];
            n.m[r * 3 + c] = v;
        }
    }
    return n;
}

} // namespace

int TeilBones(int argc, char** argv) {
    if (argc < 5) return 2;
    const std::string spielordner = argv[2], bp = Klein2(argv[3]), skTeil = Klein2(argv[4]);
    std::string cache;
    for (int i = 5; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--cache") == 0) cache = argv[i + 1];
    fbgame::Spiel spiel;
    std::string fehler, grund;
    if (!spiel.Oeffne(spielordner, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    fbindex::Index idx;
    if (cache.empty() || !fbindex::LadeIndex(idx, cache, spiel.Kopfnummer(), grund))
        if (!fbindex::BaueIndex(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }

    fbebx::Skelett sk;
    for (const auto& kv : idx.ebx) {
        if (Klein2(kv.first).find(skTeil) == std::string::npos) continue;
        std::vector<uint8_t> d;
        std::string f;
        fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, d, f) || !e.Lies(d, f)) continue;
        fbebx::Skelett s = fbebx::LiesSkelett(e);
        if (s.gefunden && !s.namen.empty() && s.modell.size() == s.namen.size()) { sk = s; break; }
    }
    if (sk.namen.empty()) { std::fprintf(stderr, "kein Skelett mit ModelPose\n"); return 1; }

    fbebx::Datei e;
    {
        const fbindex::Eintrag* ein = nullptr;
        for (const auto& kv : idx.ebx) if (Klein2(kv.first) == bp) { ein = &kv.second; break; }
        std::vector<uint8_t> d;
        std::string f;
        if (!ein || !spiel.HoleNachSha1(ein->sha1, d, f) || !e.Lies(d, f)) { std::fprintf(stderr, "Blueprint %s nicht lesbar\n", argv[3]); return 1; }
    }
    const auto& obj = e.Objekte();

    // Elternteil jedes Objekts ueber die Components-Listen.
    std::vector<int64_t> eltern(obj.size(), -1);
    for (size_t o = 0; o < obj.size(); ++o) {
        if (!obj[o]) continue;
        const fbebx::Wert* c = obj[o]->Feldwert("Components");
        if (!c) continue;
        for (const auto& r : c->liste)
            if (r && r->art == fbebx::Art::Zeiger && r->verweis >= 0 && static_cast<size_t>(r->verweis) < obj.size())
                eltern[static_cast<size_t>(r->verweis)] = static_cast<int64_t>(o);
    }
    auto istKette = [&](int64_t p) {
        return p >= 0 && obj[static_cast<size_t>(p)] &&
               (obj[static_cast<size_t>(p)]->typ == "BoneComponentData" || obj[static_cast<size_t>(p)]->typ == "PartComponentData");
    };
    auto welt = [&](size_t o) {
        M12 w = AusLage(obj[o]->Feldwert("Transform"));
        for (int64_t p = eltern[o]; istKette(p); p = eltern[static_cast<size_t>(p)])
            w = Mal12(w, AusLage(obj[static_cast<size_t>(p)]->Feldwert("Transform")));
        return w;
    };
    auto naechster = [&](const M12& w, double& abst, double& grad) {
        size_t bb = 0;
        abst = 1e30; grad = 0;
        double bestPunkte = 1e30;
        for (size_t b = 0; b < sk.namen.size(); ++b) {
            const fbebx::Lage& l = sk.modell[b];
            const double dx = w.m[9] - l.trans[0], dy = w.m[10] - l.trans[1], dz = w.m[11] - l.trans[2];
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double* z[3] = { l.right, l.up, l.forward };
            double spur = 0;
            for (int r = 0; r < 3; ++r) for (int k = 0; k < 3; ++k) spur += w.m[r * 3 + k] * z[r][k];
            const double g = std::acos(std::max(-1.0, std::min(1.0, (spur - 1.0) / 2.0))) * 57.29577951308232;
            const double punkte = d + g * 0.001;
            if (punkte < bestPunkte) { bestPunkte = punkte; abst = d; grad = g; bb = b; }
        }
        return bb;
    };

    // 1) BoneComponentData gegen das Skelett
    size_t n = 0, exakt = 0;
    std::map<size_t, size_t> boneVon;       // Objekt -> Bone
    for (size_t o = 0; o < obj.size(); ++o) {
        if (!obj[o] || obj[o]->typ != "BoneComponentData") continue;
        double a, g;
        const size_t b = naechster(welt(o), a, g);
        boneVon[o] = b;
        ++n;
        if (a < 0.002 && g < 1.0) ++exakt;
        const fbebx::Wert* fl = obj[o]->Feldwert("Flags");
        const long long flags = fl ? static_cast<long long>(fl->zahl) : 0;
        std::printf("BONECOMP #%-4zu Flags 0x%08llx (>>16: %4lld, &0xFFFF: %5lld)  -> %-28s (Bone %3zu) %7.4f m %6.2f Grad\n", o,
                    static_cast<unsigned long long>(flags), flags >> 16, flags & 0xFFFF, sk.namen[b].c_str(), b, a, g);
    }
    std::printf("BONECOMP %zu, davon exakt auf einem Bone (<2 mm, <1 Grad): %zu\n", n, exakt);

    // 2) Teile: MultiBodyPhysicsComponentData.Parts[i].TransformNode
    for (size_t o = 0; o < obj.size(); ++o) {
        if (!obj[o] || obj[o]->typ != "MultiBodyPhysicsComponentData") continue;
        const fbebx::Wert* parts = obj[o]->Feldwert("Parts");
        if (!parts) continue;
        std::printf("TEILE #%zu: %zu\n", o, parts->liste.size());
        for (size_t i = 0; i < parts->liste.size(); ++i) {
            const fbebx::Wert* p = parts->liste[i].get();
            const fbebx::Wert* tn = p ? p->Feldwert("TransformNode") : nullptr;
            if (!tn || tn->verweis < 0 || static_cast<size_t>(tn->verweis) >= obj.size()) { std::printf("  TEIL %2zu: kein TransformNode\n", i); continue; }
            const size_t pc = static_cast<size_t>(tn->verweis);
            int64_t up = eltern[pc];
            while (up >= 0 && obj[static_cast<size_t>(up)] && obj[static_cast<size_t>(up)]->typ != "BoneComponentData") up = eltern[static_cast<size_t>(up)];
            double a, g;
            const size_t direkt = naechster(welt(pc), a, g);
            const bool hatBone = up >= 0 && boneVon.count(static_cast<size_t>(up)) != 0;
            std::printf("  TEIL %2zu: #%zu %s, Eltern-BoneComp #%lld -> %-26s | eigene Weltlage: naechster Bone %s %.4f m %.2f Grad\n", i, pc,
                        obj[pc] ? obj[pc]->typ.c_str() : "?", static_cast<long long>(up),
                        hatBone ? sk.namen[boneVon[static_cast<size_t>(up)]].c_str() : "-", sk.namen[direkt].c_str(), a, g);
        }
        break;
    }
    return 0;
}

// ------------------------------------------------------------
//  castool --bindprobe <datei.fbmodel>  (1.41.0)
//  Liest die .fbmodel wie das Plugin und bindet jeden Vertex so, wie
//  BaueSkin es tut (starr: Platz 0 ueber boneRefs, Gewicht 1). Je Mesh:
//  gebunden/ungebunden, mittlerer Abstand Vertex -> Bone (Ruhelage) und die
//  Bones mit den meisten Vertices. So ist ohne Max zu sehen, ob der Kopf am
//  Kopf haengt.
// ------------------------------------------------------------
#include "fbdump.h"

int BindProbe(int argc, char** argv) {
    if (argc < 3) return 2;
    std::vector<uint8_t> daten;
    std::string f;
    fb::Model m;
    if (!fb::readFile(argv[2], daten, f) || !fb::readModel(daten, m, f)) { std::fprintf(stderr, "%s\n", f.c_str()); return 1; }
    const size_t nb = m.bones.size();
    std::vector<std::array<double, 12>> w(nb);
    for (size_t i = 0; i < nb; ++i) {
        std::array<double, 12> L{};
        for (int k = 0; k < 12; ++k) L[static_cast<size_t>(k)] = m.bones[i].rest[k];
        const int e = m.bones[i].parent;
        if (e >= 0 && static_cast<size_t>(e) < i) {
            const auto& P = w[static_cast<size_t>(e)];
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c) {
                double v = 0; for (int k = 0; k < 3; ++k) v += L[static_cast<size_t>(r * 3 + k)] * P[static_cast<size_t>(k * 3 + c)];
                if (r == 3) v += P[static_cast<size_t>(9 + c)];
                w[i][static_cast<size_t>(r * 3 + c)] = v;
            }
        } else w[i] = L;
    }
    std::printf("BINDPROBE %s: Quelle %s, %zu Bones, %zu Meshes\n", argv[2], m.source.c_str(), nb, m.meshes.size());
    size_t gesamt = 0, gebunden = 0;
    for (const fb::Mesh& me : m.meshes) {
        const size_t n4 = static_cast<size_t>(me.vertexCount) * 4;
        if (me.boneIndex.size() < n4) { std::printf("  %-50s keine BoneIndices\n", me.name.c_str()); continue; }
        const bool starr = me.weight.size() < n4;
        size_t ja = 0, nein = 0;
        double summe = 0;
        std::map<int, size_t> jeBone;
        for (uint32_t v = 0; v < me.vertexCount; ++v) {
            // wie BaueSkin: Platz 0 (starr) bzw. groesstes Gewicht
            size_t k0 = 0;
            if (!starr) for (size_t k = 1; k < 4; ++k) if (me.weight[v * 4 + k] > me.weight[v * 4 + k0]) k0 = k;
            const uint16_t lokal = me.boneIndex[v * 4 + k0];
            int b = me.boneRefs.empty() ? lokal : (lokal < me.boneRefs.size() ? me.boneRefs[lokal] : -1);
            if (b < 0 || static_cast<size_t>(b) >= nb) { ++nein; continue; }
            ++ja; ++jeBone[b];
            const double dx = me.pos[v * 3] - w[static_cast<size_t>(b)][9], dy = me.pos[v * 3 + 1] - w[static_cast<size_t>(b)][10],
                         dz = me.pos[v * 3 + 2] - w[static_cast<size_t>(b)][11];
            summe += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        gesamt += me.vertexCount; gebunden += ja;
        std::vector<std::pair<size_t, int>> top;
        for (const auto& jb : jeBone) top.push_back({ jb.second, jb.first });
        std::sort(top.rbegin(), top.rend());
        std::printf("  %-50s %s gebunden %6zu, ungebunden %6zu, Abstand %6.3f m |", me.name.c_str(), starr ? "starr" : "Gew. ", ja, nein,
                    ja ? summe / static_cast<double>(ja) : -1.0);
        for (size_t i = 0; i < top.size() && i < 5; ++i) std::printf(" %s(%zu)", m.bones[static_cast<size_t>(top[i].second)].name.c_str(), top[i].first);
        std::printf("\n");
    }
    std::printf("BINDPROBE %zu von %zu Vertices gebunden\n", gebunden, gesamt);
    return 0;
}

// ------------------------------------------------------------
//  castool --wurzel <spiel> <clipmuster> <skelett> [--cache f] [--clipcache f] [--max n]
//  (1.41.0) Erster Key der Wurzelkette gegen die Ruhelage (LocalPose), mit
//  der Lesart des Imports (Zeilen der Lage = Spalten von R(q)). Zweck: der
//  AT-AT-Befund "Hips 180 Grad" - Datenwert oder Rechenfehler?
// ------------------------------------------------------------
#include "fbanim.h"

namespace {

void ZeilenAusQuat(const float* q, double* L) {       // L: 3x3, Zeilen = Spalten von R(q)
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double R[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                             { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                             { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) L[r * 3 + c] = R[c][r];
}

double WinkelZwischen(const double* A, const double* B) {
    double spur = 0; for (int k = 0; k < 9; ++k) spur += A[k] * B[k];
    return std::acos(std::max(-1.0, std::min(1.0, (spur - 1.0) / 2.0))) * 57.29577951308232;
}

} // namespace

int Wurzel(int argc, char** argv) {
    if (argc < 5) return 2;
    const std::string spielordner = argv[2], muster = Klein2(argv[3]), skTeil = Klein2(argv[4]);
    std::string cache, clipcache;
    size_t hoechstens = 12;
    bool normiert = false;
    for (int i = 5; i < argc; ++i) if (std::strcmp(argv[i], "--normiert") == 0) normiert = true;
    for (int i = 5; i + 1 < argc; ++i) {
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
    fbebx::Skelett sk;
    for (const auto& kv : idx.ebx) {
        if (Klein2(kv.first).find(skTeil) == std::string::npos) continue;
        std::vector<uint8_t> d; std::string f; fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, d, f) || !e.Lies(d, f)) continue;
        fbebx::Skelett s = fbebx::LiesSkelett(e);
        if (s.gefunden && !s.namen.empty()) { sk = s; break; }
    }
    if (sk.namen.empty()) { std::fprintf(stderr, "kein Skelett\n"); return 1; }
    fbanim::Quelle q;
    if (clipcache.empty() || !q.Lade(spiel, clipcache, spiel.Kopfnummer(), grund))
        if (!q.Baue(spiel, idx, fehler)) { std::fprintf(stderr, "%s\n", fehler.c_str()); return 1; }
    const char* kette[] = { "Reference", "AITrajectory", "Trajectory", "TrajectoryEnd", "Connect", "ConnectEnd", "Hips", "Spine", "NeckRotate", "Head" };
    for (size_t i = 0; i < sk.namen.size() && i < 12; ++i) {
        const fbebx::Lage& l = i < sk.lokal.size() ? sk.lokal[i] : fbebx::Lage();
        const fbebx::Lage& mp = i < sk.modell.size() ? sk.modell[i] : fbebx::Lage();
        const int e = i < sk.hierarchie.size() ? sk.hierarchie[i] : -1;
        std::printf("SKELETT %2zu %-14s Eltern %-14s lokal right(%6.3f %6.3f %6.3f) fwd(%6.3f %6.3f %6.3f) | modell right(%6.3f %6.3f %6.3f) fwd(%6.3f %6.3f %6.3f)\n", i,
                    sk.namen[i].c_str(), e >= 0 ? sk.namen[static_cast<size_t>(e)].c_str() : "-", l.right[0], l.right[1], l.right[2], l.forward[0], l.forward[1],
                    l.forward[2], mp.right[0], mp.right[1], mp.right[2], mp.forward[0], mp.forward[1], mp.forward[2]);
    }
    size_t gezeigt = 0;
    std::map<std::string, std::vector<double>> winkelJe;
    for (const fbanim::ClipEintrag& ce : q.Clips()) {
        // "bone:<Name>" waehlt die Clips, die einen Kanal fuer diesen Bone haben.
        if (muster.compare(0, 5, "bone:") == 0) {
            std::vector<std::string> namen; std::string rig; size_t benannt = 0;
            if (!q.Kanalnamen(ce, namen, rig, benannt)) continue;
            bool drin = false;
            for (const std::string& n : namen) if (Klein2(n) == muster.substr(5) + ".q") { drin = true; break; }
            if (!drin) continue;
        } else if (Klein2(ce.name).find(muster) == std::string::npos && Klein2(ce.anzeige).find(muster) == std::string::npos) continue;
        fbanim::Clip c;
        std::string f;
        if (!q.Entpacke(ce, c, f)) { std::printf("CLIP %s: %s\n", ce.name.c_str(), f.c_str()); continue; }
        if (normiert) {
            double g = 0.0;
            if (fbanim::NormiereLaufrichtung(c, g)) std::printf("   (Laufrichtung %.0f Grad -> Grundrichtung)\n", g);
        }
        std::printf("CLIP %s  %s %s additiv %d  Keys %zu  Kanaele %zu\n", ce.name.c_str(), c.klasse.c_str(), c.codec.c_str(), c.additiv ? 1 : 0,
                    c.zeiten.size(), c.kanaele.size());
        for (const char* b : kette) {
            size_t bi = sk.namen.size();
            for (size_t i = 0; i < sk.namen.size(); ++i) if (sk.namen[i] == b) { bi = i; break; }
            if (bi >= sk.namen.size() || bi >= sk.lokal.size()) continue;
            const fbebx::Lage& l = sk.lokal[bi];
            const double ruhe[9] = { l.right[0], l.right[1], l.right[2], l.up[0], l.up[1], l.up[2], l.forward[0], l.forward[1], l.forward[2] };
            const fbanim::Kanal* kq = nullptr;
            const fbanim::Kanal* kt = nullptr;
            for (const fbanim::Kanal& k : c.kanaele) {
                if (k.name == std::string(b) + ".q") kq = &k;
                if (k.name == std::string(b) + ".t") kt = &k;
            }
            std::printf("   %-14s ruhe t(%7.3f %7.3f %7.3f)", b, l.trans[0], l.trans[1], l.trans[2]);
            if (kq && kq->werte.size() >= 4) {
                double L[9];
                ZeilenAusQuat(kq->werte.data(), L);
                const double wkl = WinkelZwischen(L, ruhe);
                winkelJe[b].push_back(wkl);
                size_t n = kq->konstant ? 1 : kq->werte.size() / 4;
                // groesste Abweichung ueber alle Keys
                double groesste = 0;
                for (size_t i = 0; i < n; ++i) { double Li[9]; ZeilenAusQuat(&kq->werte[i * 4], Li); groesste = std::max(groesste, WinkelZwischen(Li, ruhe)); }
                std::printf("  q0(%6.3f %6.3f %6.3f %6.3f)%s  Winkel zur Ruhe %6.1f (max %6.1f)", kq->werte[0], kq->werte[1], kq->werte[2], kq->werte[3],
                            kq->konstant ? " konst" : "      ", wkl, groesste);
            } else std::printf("  q -");
            if (kt && kt->werte.size() >= 3) {
                std::printf("  t0(%7.3f %7.3f %7.3f)%s", kt->werte[0], kt->werte[1], kt->werte[2], kt->konstant ? " konst" : "");
                const size_t e = kt->werte.size() - 3;
                if (!kt->konstant) std::printf(" tEnde(%7.3f %7.3f %7.3f)", kt->werte[e], kt->werte[e + 1], kt->werte[e + 2]);
            }
            std::printf("\n");
        }
        if (++gezeigt >= hoechstens) break;
    }
    for (const auto& kv : winkelJe) {
        std::vector<double> v = kv.second; std::sort(v.begin(), v.end());
        std::printf("WURZEL %-14s %zu Clips, Winkel erster Key zur Ruhe: min %.1f, Median %.1f, max %.1f\n", kv.first.c_str(), v.size(), v.front(), v[v.size() / 2], v.back());
    }
    return 0;
}
