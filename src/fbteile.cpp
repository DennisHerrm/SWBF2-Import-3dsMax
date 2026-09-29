// ============================================================
//  fbteile.cpp - Teile eines Composite-MeshSets an Bones binden (1.41.0).
//  Herleitung und Messung: siehe fbteile.h.
// ============================================================
#include "fbteile.h"
#include "fbebx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace fbteile {
namespace {

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

using M12 = std::array<double, 12>;

M12 AusLage(const fbebx::Wert* t) {
    M12 r = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
    if (t == nullptr) return r;
    const char* zeilen[4] = { "right", "up", "forward", "trans" };
    const char* achsen[3] = { "x", "y", "z" };
    for (int i = 0; i < 4; ++i) {
        const fbebx::Wert* v = t->Feldwert(zeilen[i]);
        if (v == nullptr) continue;
        for (int j = 0; j < 3; ++j) {
            const fbebx::Wert* c = v->Feldwert(achsen[j]);
            if (c != nullptr) r[static_cast<size_t>(i * 3 + j)] = c->gleit;
        }
    }
    return r;
}

M12 Mal(const M12& a, const M12& b) {              // a * b (Zeilenvektoren): Kind * Eltern
    M12 n{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) {
            double v = 0.0;
            for (int k = 0; k < 3; ++k) v += a[static_cast<size_t>(r * 3 + k)] * b[static_cast<size_t>(k * 3 + c)];
            if (r == 3) v += b[static_cast<size_t>(9 + c)];
            n[static_cast<size_t>(r * 3 + c)] = v;
        }
    }
    return n;
}

// Der naechste Bone zu einer Weltlage: Abstand der Verschiebung, bei
// Gleichstand entscheidet die Drehung (1 Grad wiegt 1 mm), danach die Naehe
// in der Hierarchie. `unter` >= 0: nur dieser Bone und seine Nachfahren.
int Naechster(const std::vector<M12>& boneWelt, const std::vector<int>& eltern, int unter, const M12& w, double& abstand, double& grad) {
    int best = -1;
    double punkte = 1e300;
    abstand = 0.0; grad = 0.0;
    for (size_t b = 0; b < boneWelt.size(); ++b) {
        int stufen = 0;
        if (unter >= 0) {
            int x = static_cast<int>(b);
            while (x >= 0 && x != unter && stufen < 256) { x = (static_cast<size_t>(x) < eltern.size()) ? eltern[static_cast<size_t>(x)] : -1; ++stufen; }
            if (x != unter) continue;
        }
        const M12& l = boneWelt[b];
        const double dx = w[9] - l[9], dy = w[10] - l[10], dz = w[11] - l[11];
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        double spur = 0.0;
        for (int k = 0; k < 9; ++k) spur += w[static_cast<size_t>(k)] * l[static_cast<size_t>(k)];
        const double g = std::acos(std::max(-1.0, std::min(1.0, (spur - 1.0) / 2.0))) * 57.29577951308232;
        const double p = d + g * 0.001 + stufen * 1e-7;
        if (p < punkte) { punkte = p; best = static_cast<int>(b); abstand = d; grad = g; }
    }
    return best;
}

// Hat eines der Objekte ein Feld "Mesh", das auf die Datei meshGuid zeigt?
bool ZeigtAufMesh(const fbebx::Datei& e, const std::string& meshGuid) {
    for (const auto& o : e.Objekte()) {
        if (!o) continue;
        const fbebx::Wert* m = o->Feldwert("Mesh");
        if (m != nullptr && m->art == fbebx::Art::Import && Klein(m->text) == meshGuid) return true;
    }
    return false;
}

// Die Zuordnung aus EINEM Blueprint. false, wenn es keine passende
// MultiBodyPhysicsComponentData hat.
bool AusBlueprint(const fbebx::Datei& e, size_t teile, const std::vector<M12>& boneWelt, const std::vector<int>& boneEltern, Zuordnung& aus) {
    const auto& obj = e.Objekte();
    const fbebx::Wert* parts = nullptr;
    for (const auto& o : obj) {
        if (!o || o->typ != "MultiBodyPhysicsComponentData") continue;
        const fbebx::Wert* p = o->Feldwert("Parts");
        if (p != nullptr && p->liste.size() == teile) { parts = p; break; }
    }
    if (parts == nullptr) return false;

    // Elternteil jedes Objekts ueber die Components-Listen.
    std::vector<int64_t> eltern(obj.size(), -1);
    for (size_t o = 0; o < obj.size(); ++o) {
        if (!obj[o]) continue;
        const fbebx::Wert* c = obj[o]->Feldwert("Components");
        if (c == nullptr) continue;
        for (const auto& r : c->liste)
            if (r && r->art == fbebx::Art::Zeiger && r->verweis >= 0 && static_cast<size_t>(r->verweis) < obj.size())
                eltern[static_cast<size_t>(r->verweis)] = static_cast<int64_t>(o);
    }
    auto typ = [&](int64_t o) -> const std::string& {
        static const std::string leer;
        return (o >= 0 && obj[static_cast<size_t>(o)]) ? obj[static_cast<size_t>(o)]->typ : leer;
    };
    // Weltlage eines Objekts: die Transform-Kette hinauf, solange es Bone-
    // oder Teilkomponenten sind (darueber liegt die Fahrzeugwurzel).
    auto welt = [&](size_t o) {
        M12 w = AusLage(obj[o]->Feldwert("Transform"));
        for (int64_t p = eltern[o]; typ(p) == "BoneComponentData" || typ(p) == "PartComponentData"; p = eltern[static_cast<size_t>(p)])
            w = Mal(w, AusLage(obj[static_cast<size_t>(p)]->Feldwert("Transform")));
        return w;
    };

    // Bone je BoneComponentData, von oben nach unten: die Wurzel frei, jedes
    // Kind nur unter dem Bone seines naechsten BoneComponentData-Vorfahren.
    std::vector<int> boneVon(obj.size(), -2);            // -2 = noch nicht gerechnet
    std::vector<double> abstVon(obj.size(), 0.0), gradVon(obj.size(), 0.0);
    std::function<int(size_t)> BoneVon = [&](size_t o) -> int {
        if (boneVon[o] != -2) return boneVon[o];
        boneVon[o] = -1;                                  // gegen Kreise
        int64_t p = eltern[o];
        while (p >= 0 && typ(p) != "BoneComponentData") p = eltern[static_cast<size_t>(p)];
        const int unter = (p >= 0) ? BoneVon(static_cast<size_t>(p)) : -1;
        const M12 w = welt(o);
        double a = 0.0, g = 0.0;
        int b = Naechster(boneWelt, boneEltern, unter, w, a, g);
        if (b < 0) b = Naechster(boneWelt, boneEltern, -1, w, a, g);
        boneVon[o] = b; abstVon[o] = a; gradVon[o] = g;
        return b;
    };

    aus.bone.assign(teile, -1);
    aus.exakt = aus.naeherung = aus.ohne = 0;
    aus.groessterAbstand = 0.0;
    for (size_t i = 0; i < teile; ++i) {
        const fbebx::Wert* p = parts->liste[i].get();
        const fbebx::Wert* tn = p ? p->Feldwert("TransformNode") : nullptr;
        if (tn == nullptr || tn->art != fbebx::Art::Zeiger || tn->verweis < 0 || static_cast<size_t>(tn->verweis) >= obj.size()) { ++aus.ohne; continue; }
        int64_t up = eltern[static_cast<size_t>(tn->verweis)];
        while (up >= 0 && typ(up) != "BoneComponentData") up = eltern[static_cast<size_t>(up)];
        if (up < 0) { ++aus.ohne; continue; }
        const int b = BoneVon(static_cast<size_t>(up));
        if (b < 0) { ++aus.ohne; continue; }
        const double abstand = abstVon[static_cast<size_t>(up)], grad = gradVon[static_cast<size_t>(up)];
        aus.bone[i] = b;
        if (abstand < 0.002 && grad < 1.0) ++aus.exakt;
        else { ++aus.naeherung; aus.groessterAbstand = std::max(aus.groessterAbstand, abstand); }
    }
    return true;
}

} // namespace

bool TeileZuBones(fbgame::Spiel& spiel, const fbindex::Index& idx, const std::string& meshsetName, size_t teile,
                  const std::vector<std::array<double, 12>>& boneWelt, const std::vector<int>& boneEltern, Zuordnung& aus) {
    aus = Zuordnung();
    if (teile == 0 || boneWelt.empty()) { aus.protokoll.push_back("TEILE keine Teile oder kein Skelett"); return false; }
    const std::string meshKlein = Klein(meshsetName);

    // 1) Datei-GUID des Mesh-EBX (gleicher Pfad wie das MeshSet).
    std::string meshGuid;
    for (const auto& kv : idx.ebx) {
        if (Klein(kv.first) != meshKlein) continue;
        std::vector<uint8_t> d;
        std::string f;
        fbebx::Datei e;
        if (spiel.HoleNachSha1(kv.second.sha1, d, f) && e.Lies(d, f)) meshGuid = Klein(e.DateiGuid());
        break;
    }
    if (meshGuid.empty()) { aus.protokoll.push_back("TEILE Mesh-EBX " + meshsetName + " nicht lesbar"); return false; }

    // 2) Anwaerter: erst der Ordner des Meshs, dann alles unter
    //    gameplay/vehicles/ mit dem Ordnernamen des Fahrzeugs (ohne - und _).
    const size_t schnitt = meshKlein.find_last_of('/');
    const std::string ordner = schnitt == std::string::npos ? std::string() : meshKlein.substr(0, schnitt + 1);
    std::string schluessel;
    {
        const std::string vor = "gameplay/vehicles/";
        if (meshKlein.compare(0, vor.size(), vor) == 0) {
            const size_t a = meshKlein.find('/', vor.size());
            const size_t b = a == std::string::npos ? std::string::npos : meshKlein.find('/', a + 1);
            if (b != std::string::npos) for (char c : meshKlein.substr(a + 1, b - a - 1)) if (c != '-' && c != '_') schluessel += c;
        }
    }
    auto dicht = [](const std::string& s) { std::string r; for (char c : s) if (c != '-' && c != '_') r += c; return r; };
    std::vector<const std::pair<const std::string, fbindex::Eintrag>*> erst, dann;
    for (const auto& kv : idx.ebx) {
        const std::string k = Klein(kv.first);
        if (k == meshKlein) continue;
        if (!ordner.empty() && k.compare(0, ordner.size(), ordner) == 0) erst.push_back(&kv);
        else if (schluessel.size() >= 3 && k.compare(0, 18, "gameplay/vehicles/") == 0 && dicht(k).find(schluessel) != std::string::npos) dann.push_back(&kv);
    }
    size_t gelesen = 0;
    for (const auto* liste : { &erst, &dann }) {
        for (const auto* kv : *liste) {
            if (++gelesen > 800) break;
            std::vector<uint8_t> d;
            std::string f;
            fbebx::Datei e;
            if (!spiel.HoleNachSha1(kv->second.sha1, d, f) || !e.Lies(d, f)) continue;
            if (!ZeigtAufMesh(e, meshGuid)) continue;
            Zuordnung z;
            if (!AusBlueprint(e, teile, boneWelt, boneEltern, z)) {
                aus.protokoll.push_back("TEILE " + kv->first + " zeigt auf das Mesh, hat aber keine " + std::to_string(teile) + " Teile mit Bones");
                continue;
            }
            // Das Blueprint mit den meisten exakt gebundenen Teilen gewinnt.
            if (aus.blueprint.empty() || z.exakt > aus.exakt) {
                z.protokoll = aus.protokoll;
                aus = z;
                aus.blueprint = kv->first;
            }
        }
        if (!aus.blueprint.empty()) break;     // im eigenen Ordner gefunden
    }
    if (aus.blueprint.empty()) {
        aus.protokoll.push_back("TEILE kein Blueprint mit IMPORT Mesh " + meshGuid + " gefunden (" + std::to_string(gelesen) + " EBX gelesen)");
        return false;
    }
    char t[320];
    std::snprintf(t, sizeof t, "TEILE %s: %zu Teile aus %s - %zu exakt auf einem Bone, %zu Naeherung (bis %.3f m), %zu ohne Bone",
                  meshsetName.c_str(), teile, aus.blueprint.c_str(), aus.exakt, aus.naeherung, aus.groessterAbstand, aus.ohne);
    aus.protokoll.push_back(t);
    return true;
}

} // namespace fbteile
