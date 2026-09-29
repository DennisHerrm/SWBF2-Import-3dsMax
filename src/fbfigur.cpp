// ============================================================
//  fbfigur.cpp - eine Figur zusammensetzen und als .fbmodel
//  schreiben.
// ============================================================
#include "fbfigur.h"
#include <array>
#include <charconv>
#include "fbdatei.h"
#include "fbebx.h"
#include "fbteile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace fbfigur {
namespace {

// ---- .fbmodel, Fassung 2 -------------------------------------
const char kMagic[8] = { 'F','B','M','O','D','E','L','1' };
constexpr uint32_t kVersion = 2;
constexpr uint32_t kKopfBytes = 64;

enum Bit {
    kPos = 1, kNormal = 2, kTangent = 4, kUv0 = 8, kUv1 = 16, kFarbe = 32,
    kBoneIdx = 64, kBoneWgt = 128, kBoneIdx2 = 256, kBoneWgt2 = 512
};

// Welcher Verwendungsname im dekodierten Vertex zu welcher Spalte gehoert.
struct Quelle { const char* spalte; const char* namen[2]; int laenge; };
const Quelle kQuellen[10] = {
    { "pos",      { "Pos", nullptr },           3 },
    { "normal",   { "Normal", nullptr },        3 },
    { "tangent",  { "Tangent", nullptr },       4 },
    { "uv0",      { "TexCoord0", nullptr },     2 },
    { "uv1",      { "TexCoord1", nullptr },     2 },
    { "farbe",    { "Color0", "Color" },        4 },
    { "boneIdx",  { "BoneIndices", nullptr },   4 },
    { "boneWgt",  { "BoneWeights", nullptr },   4 },
    { "boneIdx2", { "BoneIndices2", nullptr },  4 },
    { "boneWgt2", { "BoneWeights2", nullptr },  4 },
};

// ---- Stringtabelle wie in fb_model ---------------------------
struct Strings {
    // Der Puffer beginnt mit EINEM Nullbyte, und die leere Zeichenkette liegt
    // fest auf Versatz 0. So macht es fb_model, und ohne das verschiebt sich
    // die ganze Tabelle um ein Byte - die Datei ist dann gleich GROSS, aber
    // ab dem 65. Byte verschieden.
    std::vector<uint8_t> puffer{ 0 };
    std::map<std::string, uint32_t> bekannt{ { std::string(), 0u } };

    uint32_t operator()(const std::string& s) {
        const auto it = bekannt.find(s);
        if (it != bekannt.end()) return it->second;
        const uint32_t off = static_cast<uint32_t>(puffer.size());
        puffer.insert(puffer.end(), s.begin(), s.end());
        puffer.push_back(0);
        bekannt[s] = off;
        return off;
    }
};

void SchreibeU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
void SchreibeI32(std::vector<uint8_t>& b, int32_t v) { SchreibeU32(b, static_cast<uint32_t>(v)); }
void SchreibeU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void SchreibeF32(std::vector<uint8_t>& b, double v) {
    const float f = static_cast<float>(v);
    uint32_t u; std::memcpy(&u, &f, 4);
    SchreibeU32(b, u);
}
void Fuellen(std::vector<uint8_t>& b) { while (b.size() % 4) b.push_back(0); }

// Ein Element aus einem dekodierten Vertex holen, auf feste Laenge bringen.
bool Hole(const fbmesh::Vertex& v, const Quelle& q, std::vector<double>& aus) {
    for (const char* n : q.namen) {
        if (n == nullptr) break;
        const auto it = v.find(n);
        if (it == v.end()) continue;
        for (int i = 0; i < q.laenge; ++i) {
            aus.push_back(i < static_cast<int>(it->second.size()) ? it->second[static_cast<size_t>(i)] : 0.0);
        }
        return true;
    }
    return false;
}

} // namespace

namespace {

// Vertex- und Indexpuffer eines LOD: Chunk (Index oder Manifest) oder die
// Inline-Daten der .res - derselbe Weg wie in BaueFigur.
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
    if (!haben) { f = "kein Puffer (Chunk " + lod.chunkId + ")"; return false; }
    const size_t versatz = fbmesh::LodVersatz(ms, li, puffer.size());
    const size_t brauch = lod.vertexBufferSize + lod.indexBufferSize;
    if (puffer.size() < versatz + brauch) { f = "Puffer zu kurz"; return false; }
    buf.assign(puffer.begin() + static_cast<long>(versatz), puffer.begin() + static_cast<long>(versatz + brauch));
    return true;
}

} // namespace

std::string FigurSchluessel(const std::string& bundleName) {
    std::vector<std::string> teile;
    size_t a = 0;
    for (size_t i = 0; i <= bundleName.size(); ++i) {
        if (i == bundleName.size() || bundleName[i] == '/') { teile.push_back(bundleName.substr(a, i - a)); a = i + 1; }
    }
    for (size_t i = 0; i + 2 < teile.size(); ++i) {
        if (teile[i] == "characters") {
            std::string k = teile[i + 2];
            for (char& c : k) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return k;
        }
    }
    return std::string();
}

namespace {

// 4x3-Zeilenmatrizen (Zeilen right, up, forward, trans).
void Mal(const double* a, const double* b, double* aus) {      // aus = a * b
    double n[12];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) {
            double v = 0.0;
            for (int k = 0; k < 3; ++k) v += a[r * 3 + k] * b[k * 3 + c];
            if (r == 3) v += b[9 + c];
            n[r * 3 + c] = v;
        }
    }
    std::copy(n, n + 12, aus);
}
void Invers(const double* a, double* aus) {                    // starr: R^T, -T R^T
    double n[12];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) n[r * 3 + c] = a[c * 3 + r];
    for (int c = 0; c < 3; ++c) n[9 + c] = -(a[9] * n[c] + a[10] * n[3 + c] + a[11] * n[6 + c]);
    std::copy(n, n + 12, aus);
}
void LageNach12(const fbebx::Lage& l, double* d) {
    for (int i = 0; i < 3; ++i) { d[i] = l.right[i]; d[3 + i] = l.up[i]; d[6 + i] = l.forward[i]; d[9 + i] = l.trans[i]; }
}

// 1.43.0: PROZEDURALE Bones. Ein Bone-Index mit gesetztem Bit 0x8000 zeigt
// nicht in die boneList, sondern auf einen Bone, den das Spiel zur Laufzeit
// anhaengt (renderboneexpressions/verlet_singlebone: Schulterpolster, Taschen,
// Kopfschmuck). Gemessen am ARC Trooper: 32768..32772 im Koerper, 8.730 von
// 45.907 Vertices trugen NUR solche Indizes und blieben in Max ohne Gewicht -
// sie standen in der Bindepose still, waehrend der Koerper lief.
// Das Skelett kennt diese Bones nicht. Jeder wird deshalb auf den echten Bone
// umgelegt, an dem er haengt: aus den Vertices, die ihn mit echten Bones
// mischen (der mit dem groessten Gewicht); gibt es keine, der naechste Bone
// zur Mitte seiner Vertices. Im Spiel schwingt das Teil zusaetzlich - in Max
// geht es starr mit dem Elternknochen mit.
bool Koerperbone(const std::string& n) {
    static const char* const weg[] = { "FACIAL", "Reference", "AITrajectory", "Trajectory", "Connect", "Wep", "Camera", "Prop" };
    for (const char* w : weg) if (n.compare(0, std::strlen(w), w) == 0) return false;
    return true;
}

void LoeseProzeduraleBones(Mesh& me, const std::vector<Bone>& bones, const std::vector<std::array<double, 12>>& boneWelt,
                           const std::string& kurz, std::vector<std::string>& hinweise) {
    const size_t n = me.vertexCount;
    if (n == 0 || bones.empty() || boneWelt.size() != bones.size()) return;
    const size_t s1 = me.boneIdx.size() / n, s2 = me.boneIdx2.size() / n;
    const size_t w1 = me.boneWgt.size() / n, w2 = me.boneWgt2.size() / n;
    const size_t sp = me.pos.size() / n;
    auto echterBone = [&](uint16_t i) -> int {
        if ((i & 0x8000) != 0 || i >= me.boneRefs.size()) return -1;
        const uint16_t b = me.boneRefs[i];
        return b < bones.size() ? static_cast<int>(b) : -1;
    };
    struct Sammlung { std::map<int, double> echt; double mitte[3] = {}; size_t zahl = 0; };
    std::map<uint16_t, Sammlung> proz;
    std::vector<std::pair<uint16_t, double>> e;
    for (size_t v = 0; v < n; ++v) {
        e.clear();
        for (size_t k = 0; k < s1 && k < w1; ++k) e.emplace_back(me.boneIdx[v * s1 + k], me.boneWgt[v * w1 + k]);
        for (size_t k = 0; k < s2 && k < w2; ++k) e.emplace_back(me.boneIdx2[v * s2 + k], me.boneWgt2[v * w2 + k]);
        for (const auto& p : e) {
            if (p.second <= 0.0 || (p.first & 0x8000) == 0) continue;
            Sammlung& s = proz[p.first];
            ++s.zahl;
            if (sp >= 3) for (int a = 0; a < 3; ++a) s.mitte[a] += me.pos[v * sp + static_cast<size_t>(a)];
            for (const auto& q : e) {
                const int b = echterBone(q.first);
                if (q.second > 0.0 && b >= 0) s.echt[b] += q.second * p.second;
            }
        }
    }
    if (proz.empty()) return;
    std::map<uint16_t, uint16_t> neu;                             // Vertexwert -> Platz in boneRefs
    for (auto& kv : proz) {
        const Sammlung& s = kv.second;
        int wahl = -1;
        double best = 0.0;
        const char* weg = "Mischung";
        for (const auto& b : s.echt) if (b.second > best) { best = b.second; wahl = b.first; }
        if (wahl < 0) {
            weg = "naechster Bone";
            double m[3];
            for (int a = 0; a < 3; ++a) m[a] = s.mitte[a] / static_cast<double>(std::max<size_t>(1, s.zahl));
            // Ueber ALLE Koerperknochen: nur die schon benutzten zu nehmen, legte
            // beim ARC Trooper das linke Polster an LeftArm_Phys_01, das rechte
            // an Spine2_Phys_Ext_01 (RightArm_Phys_01 benutzt das Mesh sonst nicht).
            double kleinste = 1e30;
            for (size_t b = 0; b < bones.size(); ++b) {
                if (!Koerperbone(bones[b].name)) continue;
                double d = 0.0;
                for (int a = 0; a < 3; ++a) { const double x = boneWelt[b][9 + static_cast<size_t>(a)] - m[a]; d += x * x; }
                if (d < kleinste) { kleinste = d; wahl = static_cast<int>(b); }
            }
        }
        if (wahl < 0 || me.boneRefs.size() >= 0x7FFF) continue;
        neu[kv.first] = static_cast<uint16_t>(me.boneRefs.size());
        me.boneRefs.push_back(static_cast<uint16_t>(wahl));
        char t[240];
        std::snprintf(t, sizeof t, "PROZEDURAL %s: Bone %u -> %s (%zu Vertices, %s)", kurz.c_str(),
                      static_cast<unsigned>(kv.first & 0x7FFF), bones[static_cast<size_t>(wahl)].name.c_str(), s.zahl, weg);
        hinweise.push_back(t);
    }
    for (uint16_t& i : me.boneIdx) { const auto it = neu.find(i); if (it != neu.end()) i = it->second; }
    for (uint16_t& i : me.boneIdx2) { const auto it = neu.find(i); if (it != neu.end()) i = it->second; }
}

} // namespace

bool LiesBasispose(fbgame::Spiel& spiel, const fbindex::Index& idx, const Modell& m, Basispose& aus) {
    aus = Basispose();
    const fbindex::BundleInfo* b = nullptr;
    for (const auto& bi : idx.bundles) if (bi.name == m.quelle) { b = &bi; break; }
    if (b == nullptr) { aus.protokoll.push_back("POSE Bundle nicht gefunden: " + m.quelle); return false; }
    // Weltlage des Skeletts (Ruhelage) fuer die Deutung.
    const size_t n = m.bones.size();
    std::vector<std::array<double, 12>> welt(n);
    for (size_t i = 0; i < n; ++i) {
        const int e = m.bones[i].eltern;
        if (e >= 0 && static_cast<size_t>(e) < i) Mal(m.bones[i].ruhelage, welt[static_cast<size_t>(e)].data(), welt[i].data());
        else std::copy(m.bones[i].ruhelage, m.bones[i].ruhelage + 12, welt[i].data());
    }
    // 1.43.0: Bones, die jedes Mesh wirklich benutzt (Gewicht > 0), nach MeshSet.
    // Eine BasePose gehoert zum Objekt ihres Meshes und darf nur dessen Bones
    // setzen. Gemessen an Han Solo (Endor): das Handmodell einer ANDEREN Figur
    // (del_act2_02_hands) brachte 80 Eintraege mit, auch fuer Kiefer und Zaehne -
    // Hans Zaehne standen 1,9 m aus dem Mund.
    auto klein = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return x; };
    std::map<std::string, std::set<int32_t>> meshBones;
    for (const Mesh& me : m.meshes) {
        std::set<int32_t>& s = meshBones[klein(me.meshAsset)];
        const size_t nv = me.vertexCount;
        if (nv == 0) continue;
        const std::vector<uint16_t>* idxe[2] = { &me.boneIdx, &me.boneIdx2 };
        const std::vector<double>* gew[2] = { &me.boneWgt, &me.boneWgt2 };
        for (int q = 0; q < 2; ++q) {
            const size_t si = idxe[q]->size() / nv, sw = gew[q]->size() / nv;
            for (size_t v = 0; v < nv; ++v)
                for (size_t k = 0; k < si && k < sw; ++k) {
                    const uint16_t i = (*idxe[q])[v * si + k];
                    if ((*gew[q])[v * sw + k] > 0.0 && i < me.boneRefs.size()) s.insert(me.boneRefs[i]);
                }
        }
    }
    // Vorfahren gehoeren dazu: eine Pose am Elternknochen verschiebt die
    // benutzten Kinder mit (Boba Fett: LeftAnkle ueber dem Fuss).
    for (auto& mb : meshBones) {
        std::vector<int32_t> offen(mb.second.begin(), mb.second.end());
        for (int32_t bi : offen)
            for (int32_t e2 = (bi >= 0 && static_cast<size_t>(bi) < n) ? m.bones[static_cast<size_t>(bi)].eltern : -1;
                 e2 >= 0 && static_cast<size_t>(e2) < n && mb.second.insert(e2).second; e2 = m.bones[static_cast<size_t>(e2)].eltern) {}
    }
    std::map<int32_t, std::pair<fbebx::Lage, std::string>> roh;     // Index -> Transform, woher
    for (const auto& kv : idx.ebx) {
        bool drin = false;
        for (size_t bi : kv.second.bundles) if (bi == b->nummer) { drin = true; break; }
        if (!drin) continue;
        std::vector<uint8_t> daten;
        std::string f;
        fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, daten, f) || !e.Lies(daten, f)) continue;
        const auto eigen = meshBones.find(klein(kv.first) + "_mesh");
        for (const fbebx::BasisPose& bp : fbebx::LiesBasisPosen(e)) {
            size_t neu = 0, doppelt = 0, draussen = 0, fremd = 0;
            std::string fremdNamen;
            for (const auto& ein : bp.eintraege) {
                if (ein.first < 0 || static_cast<size_t>(ein.first) >= n) { ++draussen; continue; }
                if (eigen != meshBones.end() && eigen->second.count(ein.first) == 0) {
                    if (++fremd <= 12) fremdNamen += " " + m.bones[static_cast<size_t>(ein.first)].name;
                    continue;
                }
                if (roh.count(ein.first)) { ++doppelt; continue; }
                roh[ein.first] = { ein.second, kv.first };
                ++neu;
            }
            aus.protokoll.push_back("POSE " + kv.first + " (" + bp.objekt + "): " + std::to_string(bp.eintraege.size()) + " Eintraege, Count " +
                                    std::to_string(bp.count) + ", neu " + std::to_string(neu) + ", doppelt " + std::to_string(doppelt) +
                                    ", ausserhalb des Skeletts " + std::to_string(draussen) +
                                    (eigen == meshBones.end() ? std::string(", Mesh nicht importiert")
                                                              : ", nicht vom eigenen Mesh benutzt " + std::to_string(fremd) + (fremd ? " (" + fremdNamen + " )" : std::string())));
        }
    }
    if (roh.empty()) { aus.protokoll.push_back("POSE keine BasePoseTransforms im Bundle"); return false; }
    // Deutung messen: Abstand der Verschiebung zur lokalen Ruhelage bzw. zur Weltlage.
    std::vector<double> dl, dm;
    for (const auto& kv : roh) {
        const fbebx::Lage& l = kv.second.first;
        const double* L = m.bones[static_cast<size_t>(kv.first)].ruhelage;
        const double* Wl = welt[static_cast<size_t>(kv.first)].data();
        auto abst = [&](const double* t) { const double x = l.trans[0] - t[0], y = l.trans[1] - t[1], z = l.trans[2] - t[2]; return std::sqrt(x * x + y * y + z * z); };
        dl.push_back(abst(L + 9));
        dm.push_back(abst(Wl + 9));
    }
    auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    aus.medianLokal = median(dl);
    aus.medianModell = median(dm);
    aus.deutung = aus.medianLokal <= aus.medianModell ? "lokal" : "modell";
    // Immer als LOKALE Lage ablegen. Im Modellraum: lokal = Welt * inverse(Elternwelt),
    // wobei ein Elternteil mit eigener Pose seine KORRIGIERTE Welt beitraegt.
    std::vector<std::array<double, 12>> weltNeu = welt;
    std::vector<char> hat(n, 0);
    for (const auto& kv : roh) {
        hat[static_cast<size_t>(kv.first)] = 1;
        double d[12];
        LageNach12(kv.second.first, d);
        if (aus.deutung == "modell") std::copy(d, d + 12, weltNeu[static_cast<size_t>(kv.first)].data());
    }
    for (size_t i = 0; i < n; ++i) {
        const int e = m.bones[i].eltern;
        if (aus.deutung == "lokal") {
            if (hat[i]) { double d[12]; LageNach12(roh[static_cast<int32_t>(i)].first, d); std::copy(d, d + 12, weltNeu[i].data());
                          if (e >= 0 && static_cast<size_t>(e) < i) Mal(d, weltNeu[static_cast<size_t>(e)].data(), weltNeu[i].data()); }
            else if (e >= 0 && static_cast<size_t>(e) < i) Mal(m.bones[i].ruhelage, weltNeu[static_cast<size_t>(e)].data(), weltNeu[i].data());
        }
        if (!hat[i]) continue;
        PoseBone pb;
        pb.index = static_cast<int32_t>(i);
        pb.name = m.bones[i].name;
        if (aus.deutung == "lokal") {
            LageNach12(roh[static_cast<int32_t>(i)].first, pb.lokal);
        } else {
            double inv[12];
            if (e >= 0 && static_cast<size_t>(e) < n) { Invers(weltNeu[static_cast<size_t>(e)].data(), inv); Mal(weltNeu[i].data(), inv, pb.lokal); }
            else std::copy(weltNeu[i].begin(), weltNeu[i].end(), pb.lokal);
        }
        aus.bones.push_back(pb);
    }
    char t[240];
    std::snprintf(t, sizeof t, "POSE %zu Bones; Deutung %s (Median-Abstand der Verschiebung: lokal %.2f mm, Modellraum %.2f mm)",
                  aus.bones.size(), aus.deutung.c_str(), aus.medianLokal * 1000.0, aus.medianModell * 1000.0);
    aus.protokoll.push_back(t);
    return !aus.bones.empty();
}

bool SchreibeBasispose(const std::string& utf8Pfad, const Basispose& p, std::string& fehler) {
    // Zahlen mit to_chars: unabhaengig von der Region (im Max-Prozess steht
    // sie auf Deutsch - snprintf schriebe dort Kommas).
    std::string t = "SWBF2POSE 1\nDEUTUNG " + p.deutung + "\n";
    for (const PoseBone& b : p.bones) {
        t += "BONE " + b.name;
        for (double v : b.lokal) {
            char z[40];
            const auto r = std::to_chars(z, z + sizeof z, v, std::chars_format::general, 9);
            t += ' ';
            t.append(z, r.ptr);
        }
        t += '\n';
    }
    return fbdatei::SchreibeAlles(utf8Pfad, std::vector<uint8_t>(t.begin(), t.end()), fehler);
}

bool FuegeWaffenHinzu(fbgame::Spiel& spiel, const fbindex::Index& idx, Modell& m, std::vector<std::string>& protokoll) {
    const std::string schluessel = FigurSchluessel(m.quelle);
    if (schluessel.size() < 3) { protokoll.push_back("WAFFE keine Figur im Bundlenamen erkannt"); return false; }
    int wep = -1;
    for (size_t i = 0; i < m.bones.size(); ++i) if (m.bones[i].name == "Wep_Root") { wep = static_cast<int>(i); break; }
    if (wep < 0) { protokoll.push_back("WAFFE kein Bone Wep_Root im Skelett"); return false; }
    // Welt von Wep_Root (Spielraum, Zeilenvektoren): lokal * Eltern-Welt, die Kette hoch.
    double W[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
    for (int b = wep; b >= 0 && b < static_cast<int>(m.bones.size()); b = m.bones[static_cast<size_t>(b)].eltern) {
        const double* L = m.bones[static_cast<size_t>(b)].ruhelage;
        double N[12];
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 3; ++c) {
                double v = 0.0;
                for (int k = 0; k < 3; ++k) v += W[r * 3 + k] * L[k * 3 + c];
                if (r == 3) v += L[9 + c];
                N[r * 3 + c] = v;
            }
        }
        std::copy(N, N + 12, W);
    }
    auto klein = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return x; };
    std::vector<const fbindex::Eintrag*> kandidaten;
    for (const auto& kv : idx.res) {
        if (kv.second.resType != 0x49B156D4u) continue;
        const std::string n = klein(kv.first);
        if (n.compare(0, 19, "gameplay/equipment/") != 0 || n.find(schluessel) == std::string::npos) continue;
        if (n.find("frontend") != std::string::npos || n.find("1p") != std::string::npos) continue;
        kandidaten.push_back(&kv.second);
    }
    if (kandidaten.empty()) { protokoll.push_back("WAFFE keine Waffe mit \"" + schluessel + "\" unter gameplay/equipment/"); return false; }
    std::map<std::string, int32_t> matIndex;
    for (size_t i = 0; i < m.materialien.size(); ++i) matIndex[m.materialien[i].name] = static_cast<int32_t>(i);
    size_t neu = 0;
    for (const fbindex::Eintrag* v : kandidaten) {
        const std::string kurz = v->name.substr(v->name.find_last_of('/') + 1);
        std::vector<uint8_t> daten;
        std::string f;
        fbmesh::MeshSet ms;
        if (!spiel.HoleNachSha1(v->sha1, daten, f) || !fbmesh::LiesMeshSet(daten, ms, f, v->resMeta) || ms.lods.empty()) {
            protokoll.push_back("WAFFE " + kurz + ": " + f);
            continue;
        }
        std::vector<uint8_t> buf;
        if (!HolePuffer(spiel, idx, ms, 0, buf, f)) { protokoll.push_back("WAFFE " + kurz + ": " + f); continue; }
        const fbmesh::Lod& lod = ms.lods[0];
        for (const fbmesh::Section& sec : lod.sections) {
            if (fbmesh::IstTiefenSection(sec)) continue;
            std::vector<fbmesh::Vertex> verts;
            int decl = 0;
            fbmesh::Bewertung note;
            fbmesh::LiesVerticesGemessen(sec, buf, verts, decl, note);
            std::vector<uint32_t> indices;
            if (!fbmesh::LiesIndices(lod, buf, sec, indices, f)) { protokoll.push_back("WAFFE " + kurz + ": " + f); continue; }
            if (fbmesh::IstSchattengeometrie(sec, verts)) continue;
            const std::string mname = sec.materialName.empty() ? kurz : sec.materialName;
            if (matIndex.find(mname) == matIndex.end()) {
                matIndex[mname] = static_cast<int32_t>(m.materialien.size());
                Material mm;
                mm.name = mname;
                m.materialien.push_back(mm);
            }
            Mesh me;
            char t[256];
            std::snprintf(t, sizeof t, "weapon_%s_lod0_s%d_%s", kurz.c_str(), sec.index, mname.c_str());
            me.name = t;
            me.lod = 0;
            me.material = matIndex[mname];
            me.vertexCount = static_cast<uint32_t>(verts.size());
            me.dreiecke = static_cast<uint32_t>(indices.size() / 3);
            me.sectionIndex = sec.index;
            me.meshAsset = v->name;
            me.materialId = sec.materialId;
            me.bonesJeVertex = 4;
            me.tiefe = 0;
            me.indices = indices;
            std::vector<double>* spalten[6] = { &me.pos, &me.normal, &me.tangent, &me.uv0, &me.uv1, &me.farbe };
            for (int q = 0; q < 6; ++q) {
                std::vector<double> werte;
                bool fehlt = false;
                for (const fbmesh::Vertex& vv : verts) if (!Hole(vv, kQuellen[q], werte)) { fehlt = true; break; }
                if (fehlt || werte.empty()) continue;
                me.hat[q] = true;
                *spalten[q] = werte;
            }
            // Waffenraum -> Modellraum: Punkt mal W, Richtungen nur mal Drehung.
            auto dreh = [&](std::vector<double>& w, int breite, bool punkt) {
                for (size_t i = 0; i + 2 < w.size(); i += static_cast<size_t>(breite)) {
                    const double x = w[i], y = w[i + 1], z = w[i + 2];
                    for (int c = 0; c < 3; ++c) w[i + static_cast<size_t>(c)] = x * W[c] + y * W[3 + c] + z * W[6 + c] + (punkt ? W[9 + c] : 0.0);
                }
            };
            if (me.hat[0]) dreh(me.pos, 3, true);
            if (me.hat[1]) dreh(me.normal, 3, false);
            if (me.hat[2]) dreh(me.tangent, 4, false);
            // Starr an Wep_Root: je Vertex Index 0 (-> boneRefs[0]) mit Gewicht 1.
            me.boneRefs = { static_cast<uint16_t>(wep) };
            me.hat[6] = me.hat[7] = true;
            me.boneIdx.assign(static_cast<size_t>(me.vertexCount) * 4, 0);
            me.boneWgt.assign(static_cast<size_t>(me.vertexCount) * 4, 0.0);
            for (size_t i = 0; i < me.vertexCount; ++i) me.boneWgt[i * 4] = 1.0;
            protokoll.push_back("WAFFE " + me.name + ": " + std::to_string(me.vertexCount) + " Vertices, " + std::to_string(me.dreiecke) +
                                " Dreiecke, starr an Wep_Root (Bone " + std::to_string(wep) + ")");
            m.meshes.push_back(std::move(me));
            ++neu;
        }
    }
    return neu > 0;
}

// 0.86.0: gemeinsamer Kern. `nurMeshSet` leer = alle MeshSets des Bundles
// (Figuren); sonst genau dieses MeshSet (Fahrzeuge).
static bool BaueIntern(fbgame::Spiel& spiel, const fbindex::Index& idx,
                       const std::string& bundleName, const std::string& nurMeshSet, const std::string& skelettSchluessel,
                       int lodWunsch, bool alleLods, Modell& aus, std::string& fehler);

bool BaueFigur(fbgame::Spiel& spiel, const fbindex::Index& idx,
               const std::string& bundleName, const std::string& skelettSchluessel,
               int lodWunsch, bool alleLods, Modell& aus, std::string& fehler) {
    return BaueIntern(spiel, idx, bundleName, std::string(), skelettSchluessel, lodWunsch, alleLods, aus, fehler);
}

bool BaueMeshSet(fbgame::Spiel& spiel, const fbindex::Index& idx,
                 const std::string& meshsetName, const std::string& skelettSchluessel,
                 int lodWunsch, bool alleLods, Modell& aus, std::string& fehler) {
    // Ein Bundle suchen, das dieses MeshSet enthaelt.
    auto klein2 = [](std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return s; };
    const std::string suche = klein2(meshsetName);
    const fbindex::Eintrag* eintrag = nullptr;
    for (const auto& kv : idx.res) {
        if (fbindex::ResTypName(kv.second.resType) != "MeshSet") continue;
        if (klein2(kv.first) == suche) { eintrag = &kv.second; break; }
    }
    if (eintrag == nullptr) { fehler = "kein MeshSet " + meshsetName; return false; }
    if (eintrag->bundles.empty()) { fehler = "MeshSet " + meshsetName + " liegt in keinem Bundle"; return false; }
    const size_t bi = eintrag->bundles.front();
    if (bi >= idx.bundles.size()) { fehler = "MeshSet " + meshsetName + ": Bundle unbekannt"; return false; }
    return BaueIntern(spiel, idx, idx.bundles[bi].name, suche, skelettSchluessel, lodWunsch, alleLods, aus, fehler);
}

// 1.41.0: Eintrag in boneRefs fuer "dieses Teil hat keinen Bone" - liegt
// ausserhalb jedes Skeletts, Max bindet solche Vertices nicht.
constexpr uint16_t kKeinBone = 0xFFFF;

static bool BaueIntern(fbgame::Spiel& spiel, const fbindex::Index& idx,
                       const std::string& bundleName, const std::string& nurMeshSet, const std::string& skelettSchluessel,
                       int lodWunsch, bool alleLods, Modell& aus, std::string& fehler) {
    // ---- Das Bundle finden ---------------------------------------
    auto klein = [](std::string s) {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    };
    const std::string suche = klein(bundleName);
    // Erst der GENAUE Name, dann ein Teilstring. Das Fenster uebergibt volle
    // Namen; ein Teilstring-Treffer koennte sonst die Variante
    // "..._bpb_bundle1p_win32" erwischen, wenn sie im Manifest vorn steht.
    // castool uebergibt Kurznamen und landet wie bisher beim Teilstring.
    const fbindex::BundleInfo* bundle = nullptr;
    for (const fbindex::BundleInfo& b : idx.bundles) {
        if (klein(b.name) == suche) { bundle = &b; break; }
    }
    if (bundle == nullptr) {
        for (const fbindex::BundleInfo& b : idx.bundles) {
            if (klein(b.name).find(suche) != std::string::npos) { bundle = &b; break; }
        }
    }
    if (bundle == nullptr) { fehler = "kein Bundle mit " + bundleName; return false; }
    aus.quelle = bundle->name;

    // ---- Skelett -------------------------------------------------
    // 0.90.0: Der erste Treffer muss nicht der richtige sein - beim AT-ST
    // heisst der gesuchte Eintrag atst_masterskeleton, aber der Index enthaelt
    // auch atst_masterskeleton_animset u. ae. ohne Bones. Deshalb weitersuchen,
    // bis ein Eintrag wirklich ein Skelett mit Bones liefert (frueher brach
    // die Schleife beim ersten Treffer ab - Ergebnis: bones=0).
    const std::string skKlein = klein(skelettSchluessel);
    for (const auto& kv : idx.ebx) {
        if (klein(kv.first).find(skKlein) == std::string::npos) continue;
        std::vector<uint8_t> roh;
        std::string f2;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f2)) continue;
        fbebx::Datei e;
        if (!e.Lies(roh, f2)) continue;
        const fbebx::Skelett sk = fbebx::LiesSkelett(e);
        if (!sk.gefunden || sk.namen.empty()) continue;
        aus.skelett = sk.name.empty() ? skelettSchluessel : sk.name;
        for (size_t i = 0; i < sk.namen.size(); ++i) {
            Bone b;
            b.name = sk.namen[i];
            b.eltern = (i < sk.hierarchie.size()) ? sk.hierarchie[i] : -1;
            if (i < sk.lokal.size()) {
                const fbebx::Lage& l = sk.lokal[i];
                const double* z[4] = { l.right, l.up, l.forward, l.trans };
                for (int r = 0; r < 4; ++r) for (int k = 0; k < 3; ++k) b.ruhelage[r * 3 + k] = z[r][k];
            }
            aus.bones.push_back(b);
        }
        break;
    }
    if (aus.bones.empty()) {
        aus.hinweise.push_back("Skelett " + skelettSchluessel + " nicht gefunden");
        aus.skelett.clear();
    }

    // ---- MeshSets des Bundles, nach Namen sortiert ---------------
    std::vector<const fbindex::Eintrag*> meshsets;
    for (const auto& kv : idx.res) {
        if (fbindex::ResTypName(kv.second.resType) != "MeshSet") continue;
        if (!nurMeshSet.empty() && klein(kv.first) != nurMeshSet) continue;      // 0.86.0
        for (size_t bi : kv.second.bundles) {
            if (bi == bundle->nummer) { meshsets.push_back(&kv.second); break; }
        }
    }
    std::sort(meshsets.begin(), meshsets.end(),
              [](const fbindex::Eintrag* a, const fbindex::Eintrag* b) { return a->name < b->name; });

    std::map<std::string, int32_t> matIndex;

    // 1.41.0: Weltlage je Bone (Modellraum) - fuer die Teilzuordnung der
    // Composite-MeshSets (fbteile). Einmal je Aufruf.
    std::vector<std::array<double, 12>> boneWelt;
    std::vector<int> boneEltern;
    for (const Bone& b : aus.bones) boneEltern.push_back(b.eltern);
    for (size_t i = 0; i < aus.bones.size(); ++i) {
        std::array<double, 12> w{};
        std::copy(aus.bones[i].ruhelage, aus.bones[i].ruhelage + 12, w.begin());
        const int e = aus.bones[i].eltern;
        if (e >= 0 && static_cast<size_t>(e) < i) Mal(aus.bones[i].ruhelage, boneWelt[static_cast<size_t>(e)].data(), w.data());
        boneWelt.push_back(w);
    }

    for (const fbindex::Eintrag* v : meshsets) {
        const std::string kurz = v->name.substr(v->name.find_last_of('/') + 1);
        std::vector<uint8_t> daten;
        std::string f;
        if (!spiel.HoleNachSha1(v->sha1, daten, f)) { aus.hinweise.push_back(kurz + ": " + f); continue; }
        fbmesh::MeshSet ms;
        if (!fbmesh::LiesMeshSet(daten, ms, f, v->resMeta)) { aus.hinweise.push_back(kurz + ": " + f); continue; }

        // 1.41.0: COMPOSITE-MeshSet (AT-AT, AT-ST): die BoneIndices im Vertex
        // sind Teilnummern, die boneList der Section nur die Liste der darin
        // vorkommenden Teile. 1.36.0-1.40.0 lasen sie als Tabellenplatz bzw.
        // Skelettindex - der Kopf hing an Hinterbeinen. Die Zuordnung Teil ->
        // Bone kommt aus dem Fahrzeug-Blueprint (fbteile); boneRefs wird dann
        // zur Tabelle je Teil, und die starre Bindung in Max greift unveraendert.
        std::vector<uint16_t> teilTabelle;
        if (ms.meshTypeId == 2 && !boneWelt.empty()) {
            size_t teile = ms.kopfWerte[0];
            if (teile == 0)
                for (const fbmesh::Lod& l : ms.lods)
                    for (const fbmesh::Section& sc : l.sections)
                        for (uint16_t t : sc.boneList) teile = std::max<size_t>(teile, static_cast<size_t>(t) + 1);
            fbteile::Zuordnung z;
            const bool ok2 = fbteile::TeileZuBones(spiel, idx, v->name, teile, boneWelt, boneEltern, z);
            for (const std::string& zeile : z.protokoll) aus.hinweise.push_back(zeile);
            teilTabelle.assign(teile, kKeinBone);
            if (ok2) {
                for (size_t t = 0; t < teile && t < z.bone.size(); ++t)
                    if (z.bone[t] >= 0) teilTabelle[t] = static_cast<uint16_t>(z.bone[t]);
                std::string liste;
                for (size_t t = 0; t < teile && t < 80; ++t)
                    liste += " " + std::to_string(t) + ":" + (teilTabelle[t] == kKeinBone ? std::string("-") : aus.bones[teilTabelle[t]].name);
                aus.hinweise.push_back("TEILE " + kurz + " Teil:Bone" + liste);
            } else {
                aus.hinweise.push_back("TEILE " + kurz + ": keine Zuordnung - die Teile bleiben ungebunden (starr in der Ruhelage)");
            }
        }

        std::vector<size_t> lods;
        if (alleLods) { for (size_t i = 0; i < ms.lods.size(); ++i) lods.push_back(i); }
        else if (!ms.lods.empty()) {
            lods.push_back(static_cast<size_t>(std::min<int>(lodWunsch, static_cast<int>(ms.lods.size()) - 1)));
        }

        for (size_t li : lods) {
            const fbmesh::Lod& lod = ms.lods[li];
            // Den Puffer holen: ueber den Chunk oder, wenn keiner da ist,
            // aus den Inline-Daten.
            std::vector<uint8_t> puffer;
            bool haben = false;
            if (!lod.chunkId.empty() && lod.chunkId != "00000000-0000-0000-0000-000000000000") {
                // Erst ueber den Index und die SHA-1, dann - wenn das nichts
                // gibt - ueber die Manifest-Chunks. Manche Chunks stehen NUR
                // im Manifest und haben gar keine SHA-1.
                const auto it = idx.chunks.find(lod.chunkId);
                if (it != idx.chunks.end() && !it->second.sha1.empty()) {
                    haben = spiel.HoleNachSha1(it->second.sha1, puffer, f);
                }
                if (!haben) {
                    for (const fbgame::ManifestChunk& mc : spiel.Chunks()) {
                        char t[40];
                        std::snprintf(t, sizeof t,
                            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                            mc.guid[3], mc.guid[2], mc.guid[1], mc.guid[0], mc.guid[5], mc.guid[4],
                            mc.guid[7], mc.guid[6], mc.guid[8], mc.guid[9], mc.guid[10],
                            mc.guid[11], mc.guid[12], mc.guid[13], mc.guid[14], mc.guid[15]);
                        if (lod.chunkId == t) { haben = spiel.HoleManifestChunk(mc, puffer, f); break; }
                    }
                }
            }
            if (!haben && !ms.inlineDaten.empty()) {
                // Kein Chunk: der Puffer steht in der .res selbst.
                puffer = ms.inlineDaten;
                haben = true;
            }
            if (!haben) {
                char t[256];
                std::snprintf(t, sizeof t, "%s LOD %zu: kein Puffer (Chunk %s)%s%s",
                              kurz.c_str(), li, lod.chunkId.c_str(),
                              f.empty() ? "" : " - ", f.c_str());
                aus.hinweise.push_back(t);
                continue;
            }

            const size_t versatz = fbmesh::LodVersatz(ms, li, puffer.size());
            const size_t brauch = lod.vertexBufferSize + lod.indexBufferSize;
            if (puffer.size() < versatz + brauch) {
                char t[160];
                std::snprintf(t, sizeof t, "%s LOD %zu: Puffer zu kurz (%zu < %zu)",
                              kurz.c_str(), li, puffer.size(), versatz + brauch);
                aus.hinweise.push_back(t);
                continue;
            }
            const std::vector<uint8_t> buf(puffer.begin() + static_cast<long>(versatz),
                                           puffer.begin() + static_cast<long>(versatz + brauch));

            for (const fbmesh::Section& sec : lod.sections) {
                if (fbmesh::IstTiefenSection(sec)) continue;
                std::vector<fbmesh::Vertex> verts;
                int decl = 0;
                fbmesh::Bewertung note;
                fbmesh::LiesVerticesGemessen(sec, buf, verts, decl, note);
                std::vector<uint32_t> indices;
                if (!fbmesh::LiesIndices(lod, buf, sec, indices, f)) {
                    aus.hinweise.push_back(kurz + ": " + f);
                    continue;
                }
                if (decl != 0) ++aus.anders;
                if (fbmesh::IstSchattengeometrie(sec, verts)) { ++aus.schatten; continue; }

                const std::string mname = sec.materialName;
                if (!mname.empty() && matIndex.find(mname) == matIndex.end()) {
                    matIndex[mname] = static_cast<int32_t>(aus.materialien.size());
                    Material m; m.name = mname;
                    aus.materialien.push_back(m);
                }

                Mesh me;
                char t[256];
                std::snprintf(t, sizeof t, "%s_lod%zu_s%d%s%s", kurz.c_str(), li, sec.index,
                              mname.empty() ? "" : "_", mname.c_str());
                me.name = t;
                me.lod = static_cast<int32_t>(li);
                me.material = mname.empty() ? -1 : matIndex[mname];
                me.vertexCount = static_cast<uint32_t>(verts.size());
                me.dreiecke = static_cast<uint32_t>(indices.size() / 3);
                me.sectionIndex = sec.index;
                me.meshAsset = v->name;
                me.materialId = sec.materialId;
                me.bonesJeVertex = sec.bonesPerVertex;
                me.tiefe = 0;
                me.boneRefs = teilTabelle.empty() ? sec.boneList : teilTabelle;   // 1.41.0
                me.indices = indices;

                std::vector<double>* spalten[10] = {
                    &me.pos, &me.normal, &me.tangent, &me.uv0, &me.uv1, &me.farbe,
                    nullptr, &me.boneWgt, nullptr, &me.boneWgt2
                };
                for (int q = 0; q < 10; ++q) {
                    std::vector<double> werte;
                    bool fehlt = false;
                    for (const fbmesh::Vertex& vv : verts) {
                        if (!Hole(vv, kQuellen[q], werte)) { fehlt = true; break; }
                    }
                    if (fehlt || werte.empty()) continue;
                    me.hat[q] = true;
                    if (q == 6 || q == 8) {
                        // Bone-Indizes werden gerundet und als u16 abgelegt.
                        std::vector<uint16_t>& ziel = (q == 6) ? me.boneIdx : me.boneIdx2;
                        for (double x : werte) {
                            const double g = std::floor(x + 0.5);
                            ziel.push_back(static_cast<uint16_t>(g < 0 ? 0 : (g > 65535 ? 65535 : g)));
                        }
                    } else if (spalten[q] != nullptr) {
                        *spalten[q] = werte;
                    }
                }
                if (ms.meshTypeId != 2) LoeseProzeduraleBones(me, aus.bones, boneWelt, me.name, aus.hinweise);   // 1.43.0
                // 1.43.0: Mesh OHNE Gewichte in einer Figur (Boba Fetts Umhang: im
                // Spiel Stoffsimulation, boneRefs leer). Max liess es in der
                // Bindepose stehen, waehrend die Figur lief. Jetzt starr an den
                // naechsten Koerperknochen zur Mitte des Meshes - Max haengt ein
                // Mesh mit genau einem boneRef und ohne Indizes als Kind an.
                if (ms.meshTypeId != 2 && me.boneRefs.empty() && !me.hat[6] && me.vertexCount > 0 && boneWelt.size() == aus.bones.size() &&
                    skelettSchluessel.size() > 11 && klein(skelettSchluessel).compare(0, 11, "characters/") == 0) {
                    double mitte[3] = {};
                    const size_t sp = me.pos.size() / me.vertexCount;
                    for (size_t vi = 0; sp >= 3 && vi < me.vertexCount; ++vi)
                        for (int a = 0; a < 3; ++a) mitte[a] += me.pos[vi * sp + static_cast<size_t>(a)] / static_cast<double>(me.vertexCount);
                    int wahl = -1;
                    double kleinste = 1e30;
                    // Nur Rumpfknochen: ein Umhang ueber der Schulter folgt dem
                    // Rumpf, nicht dem Arm (Boba Fett: sonst LeftArmRoll).
                    static const char* const rumpf[] = { "Hips", "Spine", "Spine1", "Spine2", "Spine3", "Neck", "Neck1", "Head", "LeftShoulder", "RightShoulder" };
                    for (size_t bi = 0; bi < aus.bones.size(); ++bi) {
                        bool istRumpf = false;
                        for (const char* r : rumpf) if (aus.bones[bi].name == r) istRumpf = true;
                        if (!istRumpf) continue;
                        double d = 0.0;
                        for (int a = 0; a < 3; ++a) { const double x = boneWelt[bi][9 + static_cast<size_t>(a)] - mitte[a]; d += x * x; }
                        if (d < kleinste) { kleinste = d; wahl = static_cast<int>(bi); }
                    }
                    if (wahl >= 0) {
                        me.boneRefs = { static_cast<uint16_t>(wahl) };
                        aus.hinweise.push_back("OHNE GEWICHTE " + me.name + ": starr an " + aus.bones[static_cast<size_t>(wahl)].name +
                                               " (naechster Koerperknochen, im Spiel vermutlich Stoffsimulation)");
                    }
                }
                aus.meshes.push_back(std::move(me));
            }
        }
    }
    fehler.clear();
    return true;
}

bool BaueFbmodelBytes(const Modell& m, std::vector<uint8_t>& aus, std::string& fehler) {
    Strings st;

    std::vector<uint8_t> teilBones;
    for (const Bone& b : m.bones) {
        SchreibeU32(teilBones, st(b.name));
        SchreibeI32(teilBones, b.eltern);
        SchreibeI32(teilBones, b.typ);
        SchreibeU32(teilBones, 0);
        for (int i = 0; i < 12; ++i) SchreibeF32(teilBones, b.ruhelage[i]);
    }

    std::vector<uint8_t> teilMat;
    for (const Material& mm : m.materialien) {
        SchreibeU32(teilMat, st(mm.name));
        for (int i = 0; i < 4; ++i) {
            SchreibeU32(teilMat, st(i < static_cast<int>(mm.texturen.size()) ? mm.texturen[static_cast<size_t>(i)] : std::string()));
        }
    }

    std::vector<uint8_t> teilMesh;
    for (const Mesh& me : m.meshes) {
        uint32_t stroeme = 0;
        const uint32_t bits[10] = { kPos, kNormal, kTangent, kUv0, kUv1, kFarbe,
                                    kBoneIdx, kBoneWgt, kBoneIdx2, kBoneWgt2 };
        for (int q = 0; q < 10; ++q) if (me.hat[q]) stroeme |= bits[q];

        SchreibeU32(teilMesh, st(me.name));
        SchreibeU32(teilMesh, static_cast<uint32_t>(me.lod));
        SchreibeI32(teilMesh, me.material);
        SchreibeU32(teilMesh, me.vertexCount);
        SchreibeU32(teilMesh, me.dreiecke);
        SchreibeU32(teilMesh, static_cast<uint32_t>(me.boneRefs.size()));
        SchreibeU32(teilMesh, stroeme);
        SchreibeU32(teilMesh, me.bonesJeVertex);
        SchreibeU32(teilMesh, static_cast<uint32_t>(me.sectionIndex));
        SchreibeU32(teilMesh, static_cast<uint32_t>(me.tiefe));
        SchreibeU32(teilMesh, 0);
        SchreibeU32(teilMesh, 0);

        const std::vector<double>* gleit[10] = {
            &me.pos, &me.normal, &me.tangent, &me.uv0, &me.uv1, &me.farbe,
            nullptr, &me.boneWgt, nullptr, &me.boneWgt2
        };
        for (int q = 0; q < 10; ++q) {
            if (!me.hat[q]) continue;
            if (q == 6 || q == 8) {
                const std::vector<uint16_t>& v = (q == 6) ? me.boneIdx : me.boneIdx2;
                for (uint16_t x : v) SchreibeU16(teilMesh, x);
            } else if (gleit[q] != nullptr) {
                for (double x : *gleit[q]) SchreibeF32(teilMesh, x);
            }
        }
        for (uint32_t i : me.indices) SchreibeU32(teilMesh, i);
        std::vector<uint8_t> refs;
        for (uint16_t r : me.boneRefs) SchreibeU16(refs, r);
        Fuellen(refs);
        teilMesh.insert(teilMesh.end(), refs.begin(), refs.end());
    }

    const uint32_t quelleOff = st(m.quelle);
    const uint32_t skelettOff = st(m.skelett);
    std::vector<uint8_t> strings = st.puffer;
    Fuellen(strings);

    std::vector<uint8_t> kopf;
    kopf.insert(kopf.end(), kMagic, kMagic + 8);
    SchreibeU32(kopf, kVersion);
    SchreibeU32(kopf, kKopfBytes);
    SchreibeU32(kopf, static_cast<uint32_t>(m.meshes.size()));
    SchreibeU32(kopf, static_cast<uint32_t>(m.bones.size()));
    SchreibeU32(kopf, static_cast<uint32_t>(m.materialien.size()));
    SchreibeU32(kopf, static_cast<uint32_t>(strings.size()));
    SchreibeU32(kopf, m.bones.empty() ? 0u : 1u);
    SchreibeF32(kopf, m.einheit);
    SchreibeU32(kopf, quelleOff);
    SchreibeU32(kopf, skelettOff);
    for (int i = 0; i < 4; ++i) SchreibeU32(kopf, 0);
    if (kopf.size() != kKopfBytes) { fehler = "Kopf hat die falsche Groesse"; return false; }

    aus.clear();
    aus.reserve(kopf.size() + strings.size() + teilBones.size() + teilMat.size() + teilMesh.size());
    for (const std::vector<uint8_t>* teil : { &kopf, &strings, &teilBones, &teilMat, &teilMesh })
        aus.insert(aus.end(), teil->begin(), teil->end());
    return true;
}

bool SchreibeFbmodel(const std::string& pfad, const Modell& m, std::string& fehler) {
    std::vector<uint8_t> bytes;
    if (!BaueFbmodelBytes(m, bytes, fehler)) return false;
    return fbdatei::SchreibeAlles(pfad, bytes, fehler);
}

} // namespace fbfigur
