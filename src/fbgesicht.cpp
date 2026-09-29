// ============================================================
//  fbgesicht.cpp - siehe fbgesicht.h
// ============================================================
#include "fbgesicht.h"
#include "fbebx.h"
#include "fbgdwerte.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace fbgesicht {

namespace {

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string Dreh(const std::string& x) {
    std::string r;
    for (size_t q = x.size(); q >= 2; q -= 2) r += x.substr(q - 2, 2);
    return r;
}

// Suche rekursiv nach einem Objekt mit dem Feld "FacePoserLibrary".
void SucheAntRefs(const fbebx::Wert& w, std::vector<std::string>& guids, int tiefe) {
    if (tiefe > 12) return;
    if (const fbebx::Wert* f = w.Feldwert("FacePoserLibrary")) {
        if (const fbebx::Wert* g = f->Feldwert("AssetGuid")) guids.push_back(Klein(g->text));
    }
    for (const auto& fe : w.felder) if (fe.wert) SucheAntRefs(*fe.wert, guids, tiefe + 1);
    for (const auto& l : w.liste) if (l) SucheAntRefs(*l, guids, tiefe + 1);
}

const fbgd::Wert* Feld(const fbgd::Datensatz& d, const char* n) { return fbgd::Feld(d.felder, n); }

void FelderInsProtokoll(const std::string& titel, const fbgd::Datensatz& d, std::vector<std::string>& p) {
    size_t n = 0;
    for (const auto& fp : d.felder) {
        if (n++ >= 40) break;
        const fbgd::Wert& w = fp.second;
        char t[260];
        if (w.art == fbgd::Wert::Art::Feld) {
            std::string erste;
            for (size_t i = 0; i < w.werte.size() && i < 6; ++i) {
                const fbgd::Wert& x = w.werte[i];
                char z[40];
                if (x.art == fbgd::Wert::Art::Gleit) std::snprintf(z, sizeof z, "%g", static_cast<double>(x.gleit));
                else if (x.art == fbgd::Wert::Art::Hex) std::snprintf(z, sizeof z, "%s", x.text.c_str());
                else std::snprintf(z, sizeof z, "%lld", static_cast<long long>(x.ganz));
                erste += std::string(i ? "," : "") + z;
            }
            std::snprintf(t, sizeof t, "GESICHT   %s.%s: Array %s n=%u [%s]", titel.c_str(), fp.first.c_str(), w.typ.c_str(), w.anzahl, erste.c_str());
        } else if (w.art == fbgd::Wert::Art::Gleit) {
            std::snprintf(t, sizeof t, "GESICHT   %s.%s: %g", titel.c_str(), fp.first.c_str(), static_cast<double>(w.gleit));
        } else if (w.art == fbgd::Wert::Art::Hex || w.art == fbgd::Wert::Art::Text) {
            std::snprintf(t, sizeof t, "GESICHT   %s.%s: %s", titel.c_str(), fp.first.c_str(), w.text.c_str());
        } else {
            std::snprintf(t, sizeof t, "GESICHT   %s.%s: %lld", titel.c_str(), fp.first.c_str(), static_cast<long long>(w.ganz));
        }
        p.push_back(t);
    }
}

// Rueckfall (0.44.1): die Gesichtsbones aus dem ERSTEN Key des MainFace-Clips
// der Figur (UI_FrontEnd_<Figur>_MainFace_01). Bei DH repariert genau dieser
// Clip das Gesicht; genommen werden nur die FACIAL_-Bones.
bool PoseAusMainFace(fbanim::Quelle& q, const fbfigur::Modell& m, fbfigur::Basispose& aus) {
    const std::string schluessel = fbfigur::FigurSchluessel(m.quelle);
    if (schluessel.size() < 3) return false;
    std::map<std::string, size_t> boneIndex;
    for (size_t i = 0; i < m.bones.size(); ++i) boneIndex[m.bones[i].name] = i;
    for (const fbanim::ClipEintrag& ce : q.Clips()) {
        if (ce.klasse != "RawAnimationAsset" && ce.klasse != "FrameAnimationAsset") continue;
        const std::string a = Klein(ce.anzeige);
        if (a.find("mainface") == std::string::npos || a.find(schluessel) == std::string::npos) continue;
        fbanim::Clip c;
        std::string f;
        if (!q.Entpacke(ce, c, f)) continue;
        std::map<size_t, fbfigur::PoseBone> bones;
        for (const fbanim::Kanal& k : c.kanaele) {
            if (k.name.compare(0, 7, "FACIAL_") != 0 || k.name.size() < 3) continue;
            const std::string bn = k.name.substr(0, k.name.size() - 2);
            const auto bi = boneIndex.find(bn);
            if (bi == boneIndex.end()) continue;
            auto it = bones.find(bi->second);
            if (it == bones.end()) {
                fbfigur::PoseBone pb;
                pb.index = static_cast<int32_t>(bi->second);
                pb.name = bn;
                std::copy(m.bones[bi->second].ruhelage, m.bones[bi->second].ruhelage + 12, pb.lokal);
                it = bones.emplace(bi->second, pb).first;
            }
            fbfigur::PoseBone& pb = it->second;
            if (k.art == 'q' && k.werte.size() >= 4) {
                double x = k.werte[0], y = k.werte[1], z = k.werte[2], w = k.werte[3];
                const double n = std::sqrt(x * x + y * y + z * z + w * w);
                if (n < 1e-6) continue;
                x /= n; y /= n; z /= n; w /= n;
                const double R[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                                         { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                                         { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
                for (int r = 0; r < 3; ++r) for (int cc = 0; cc < 3; ++cc) pb.lokal[r * 3 + cc] = R[cc][r];
            } else if (k.art == 't' && k.werte.size() >= 3) {
                pb.lokal[9] = k.werte[0];
                pb.lokal[10] = k.werte[1];
                pb.lokal[11] = k.werte[2];
            }
        }
        if (bones.empty()) continue;
        for (const auto& kv : bones) aus.bones.push_back(kv.second);
        aus.deutung = "lokal";
        aus.protokoll.push_back("GESICHT Rueckfall: " + std::to_string(aus.bones.size()) + " FACIAL_-Bones aus dem ersten Key von " + ce.anzeige +
                                " (" + ce.name + ", " + q.BankName(ce.bank) + ")");
        return true;
    }
    aus.protokoll.push_back("GESICHT kein MainFace-Clip fuer \"" + schluessel + "\"");
    return false;
}

} // namespace

std::string KeyAusGuid(const std::string& guidText) {
    std::string hex;
    for (char c : Klein(guidText)) if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) hex += c;
    if (hex.size() != 32) return std::string();
    return Dreh(hex.substr(0, 8)) + Dreh(hex.substr(8, 4)) + Dreh(hex.substr(12, 4));
}

bool LiesGesichtspose(fbgame::Spiel& spiel, const fbindex::Index& idx, fbanim::Quelle& q,
                      const fbfigur::Modell& m, fbfigur::Basispose& aus) {
    aus = fbfigur::Basispose();
    const fbindex::BundleInfo* b = nullptr;
    for (const auto& bi : idx.bundles) if (bi.name == m.quelle) { b = &bi; break; }
    if (b == nullptr) { aus.protokoll.push_back("GESICHT Bundle nicht gefunden: " + m.quelle); return false; }
    // 1. FacePoserLibrary-AntRefs aus den EBX des Bundles
    std::vector<std::string> guids;
    for (const auto& kv : idx.ebx) {
        bool drin = false;
        for (size_t bi : kv.second.bundles) if (bi == b->nummer) { drin = true; break; }
        if (!drin) continue;
        std::vector<uint8_t> roh;
        std::string f;
        fbebx::Datei e;
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f) || !e.Lies(roh, f)) continue;
        for (const auto& o : e.Objekte()) if (o) SucheAntRefs(*o, guids, 0);
    }
    std::string bibKey;
    for (const std::string& g : guids) {
        if (g.find_first_not_of("0-") == std::string::npos) continue;          // leere GUID
        bibKey = KeyAusGuid(g);
        aus.protokoll.push_back("GESICHT FacePoserLibrary " + g + " -> Key " + bibKey);
        break;
    }
    if (bibKey.empty()) { aus.protokoll.push_back("GESICHT kein FacePoserLibrary-Verweis im Bundle"); return false; }
    // 2. FacePoseLibraryAsset -> BindPoseAsset
    fbanim::ClipEintrag bib;
    if (!q.FindeEintrag(bibKey, bib)) { aus.protokoll.push_back("GESICHT Key " + bibKey + " in keiner Bank"); return false; }
    fbgd::Datensatz bd;
    q.Felder(bib, bd);
    const fbgd::Wert* bpk = Feld(bd, "BindPoseAsset");
    aus.protokoll.push_back("GESICHT " + bib.klasse + " \"" + bib.name + "\" in " + q.BankName(bib.bank) + ", BindPoseAsset " +
                            (bpk != nullptr ? bpk->text : std::string("-")));
    if (bpk == nullptr || bpk->text.empty() || bpk->text == "0000000000000000") return false;
    fbanim::ClipEintrag bp;
    if (!q.FindeEintrag(bpk->text, bp)) { aus.protokoll.push_back("GESICHT BindPoseAsset " + bpk->text + " in keiner Bank"); return PoseAusMainFace(q, m, aus); }
    fbgd::Datensatz pd;
    q.Felder(bp, pd);
    // Messung (0.44.1): alle Felder des BindPoseAsset und des FacePoseJointDofsAsset.
    FelderInsProtokoll("BindPose", pd, aus.protokoll);
    if (const fbgd::Wert* jd = Feld(bd, "FacePoseJointDofsAsset")) {
        fbanim::ClipEintrag je;
        if (!jd->text.empty() && jd->text != "0000000000000000" && q.FindeEintrag(jd->text, je)) {
            fbgd::Datensatz jdd;
            q.Felder(je, jdd);
            aus.protokoll.push_back("GESICHT FacePoseJointDofsAsset " + je.klasse + " \"" + je.name + "\"");
            FelderInsProtokoll("JointDofs", jdd, aus.protokoll);
        }
    }
    auto ganze = [&](const char* n) { std::vector<long long> v; if (const fbgd::Wert* w = Feld(pd, n)) for (const auto& x : w->werte) v.push_back(x.ganz); return v; };
    auto floats = [&](const char* n) { std::vector<float> v; if (const fbgd::Wert* w = Feld(pd, n)) for (const auto& x : w->werte) v.push_back(x.gleit); return v; };
    const std::vector<long long> qi = ganze("BindPoseQuaternionIndexList"), ti = ganze("BindPoseTranslationIndexList");
    const std::vector<float> ql = floats("BindPoseQuaternionList"), tl = floats("BindPoseTranslationList");
    const int qBreite = qi.empty() ? 0 : static_cast<int>(ql.size() / qi.size());
    const int tBreite = ti.empty() ? 0 : static_cast<int>(tl.size() / ti.size());
    long long qMax = -1, tMax = -1;
    for (long long x : qi) qMax = std::max(qMax, x);
    for (long long x : ti) tMax = std::max(tMax, x);
    char t[300];
    std::snprintf(t, sizeof t, "GESICHT %s \"%s\": Drehungen %zu (je %d Werte, groesster Index %lld), Verschiebungen %zu (je %d Werte, groesster Index %lld); Skelett %zu Bones",
                  bp.klasse.c_str(), bp.name.c_str(), qi.size(), qBreite, qMax, ti.size(), tBreite, tMax, m.bones.size());
    aus.protokoll.push_back(t);
    // Die Werte stehen nicht unter diesen Namen (gemessen 0.44.0: "je 0 Werte",
    // groesste Indizes 60/71 - das sind Indizes in die Gesichtsgelenke, nicht
    // ins Skelett). Bis der Aufbau aus der Messung oben feststeht: Rueckfall.
    if (qBreite < 4 && tBreite < 3) return PoseAusMainFace(q, m, aus);
    if (qMax >= static_cast<long long>(m.bones.size()) || tMax >= static_cast<long long>(m.bones.size())) {
        aus.protokoll.push_back("GESICHT Indizes groesser als das Skelett - Zuordnung unklar");
        return PoseAusMainFace(q, m, aus);
    }
    // 3. Basispose: je Bone lokale Lage = Ruhelage, Drehung/Verschiebung aus dem BindPose.
    std::map<long long, fbfigur::PoseBone> bones;
    auto bone = [&](long long i) -> fbfigur::PoseBone& {
        auto it = bones.find(i);
        if (it != bones.end()) return it->second;
        fbfigur::PoseBone pb;
        pb.index = static_cast<int32_t>(i);
        pb.name = m.bones[static_cast<size_t>(i)].name;
        std::copy(m.bones[static_cast<size_t>(i)].ruhelage, m.bones[static_cast<size_t>(i)].ruhelage + 12, pb.lokal);
        return bones.emplace(i, pb).first->second;
    };
    std::vector<double> winkel, abstand;
    for (size_t k = 0; k < qi.size() && qBreite >= 4; ++k) {
        const size_t o = k * static_cast<size_t>(qBreite);
        double x = ql[o], y = ql[o + 1], z = ql[o + 2], w = ql[o + 3];
        const double n = std::sqrt(x * x + y * y + z * z + w * w);
        if (n < 1e-6) continue;
        x /= n; y /= n; z /= n; w /= n;
        const double R[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                                 { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                                 { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
        fbfigur::PoseBone& pb = bone(qi[k]);
        double spur = 0.0;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                const double neu = R[c][r];                                      // Zeilen = Spalten von R(q), gemessen
                spur += pb.lokal[r * 3 + c] * neu;
                pb.lokal[r * 3 + c] = neu;
            }
        }
        winkel.push_back(std::acos(std::max(-1.0, std::min(1.0, (spur - 1.0) / 2.0))) * 180.0 / 3.14159265358979);
    }
    for (size_t k = 0; k < ti.size() && tBreite >= 3; ++k) {
        const size_t o = k * static_cast<size_t>(tBreite);
        fbfigur::PoseBone& pb = bone(ti[k]);
        const double dx = tl[o] - pb.lokal[9], dy = tl[o + 1] - pb.lokal[10], dz = tl[o + 2] - pb.lokal[11];
        abstand.push_back(std::sqrt(dx * dx + dy * dy + dz * dz));
        pb.lokal[9] = tl[o];
        pb.lokal[10] = tl[o + 1];
        pb.lokal[11] = tl[o + 2];
    }
    auto median = [](std::vector<double> v) { if (v.empty()) return -1.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    size_t facial = 0;
    for (const auto& kv : bones) {
        if (kv.second.name.compare(0, 7, "FACIAL_") == 0) ++facial;
        aus.bones.push_back(kv.second);
    }
    aus.deutung = "lokal";
    aus.medianLokal = median(abstand);
    std::snprintf(t, sizeof t, "GESICHT Basispose: %zu Bones (%zu FACIAL_); Abstand zur Ruhelage: Drehung Median %.2f Grad, Verschiebung Median %.2f mm",
                  aus.bones.size(), facial, median(winkel), median(abstand) * 1000.0);
    aus.protokoll.push_back(t);
    return !aus.bones.empty();
}

} // namespace fbgesicht
