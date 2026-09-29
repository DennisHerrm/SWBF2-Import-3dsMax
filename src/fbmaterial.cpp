// ============================================================
//  fbmaterial.cpp - siehe fbmaterial.h
// ============================================================
#include "fbmaterial.h"

#include "fbdatei.h"
#include "fbebx.h"
#include "fbtextur.h"

#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <memory>

namespace fbmaterial {

namespace {

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string Blatt(const std::string& n) {
    const size_t p = n.find_last_of('/');
    return p == std::string::npos ? n : n.substr(p + 1);
}

const fbindex::BundleInfo* FindeBundle(const fbindex::Index& idx, const std::string& name) {
    for (const auto& b : idx.bundles) if (b.name == name) return &b;
    for (const auto& b : idx.bundles) if (!name.empty() && b.name.find(name) != std::string::npos) return &b;
    return nullptr;
}

bool ImBundle(const fbindex::Eintrag& e, size_t nummer) {
    for (size_t n : e.bundles) if (n == nummer) return true;
    return false;
}

struct MatQuelle {
    std::string shader;                                    // EBX-Name des ShaderGraph (0.46.0)
    std::string instanz;                                   // GUID der MeshMaterial-Instanz, klein
    std::vector<std::pair<std::string, std::string>> tex;  // Parametername -> Textur-EBX (oder "?guid")
};

void LiesTexturParameter(const fbebx::Wert* liste, const std::map<std::string, std::string>& guidName,
                         std::vector<std::pair<std::string, std::string>>& aus) {
    if (liste == nullptr || liste->art != fbebx::Art::Liste) return;
    for (const auto& p : liste->liste) {
        if (!p) continue;
        const fbebx::Wert* name = p->Feldwert("ParameterName");
        const fbebx::Wert* wert = p->Feldwert("Value");
        if (name == nullptr || wert == nullptr) continue;
        std::string ziel;
        if (wert->art == fbebx::Art::Import) {
            const auto it = guidName.find(Klein(wert->text));
            ziel = (it != guidName.end()) ? it->second : "?" + wert->text;
        }
        if (!ziel.empty()) aus.emplace_back(name->text, ziel);
    }
}

// Eine Nachkommastelle mit PUNKT. snprintf("%.1f") folgt der Region, und im
// Max-Prozess steht die auf Deutsch - dort kam "R=35,5" ins Protokoll.
std::string Z1(double x) {
    const long long z = std::llround(x * 10.0);
    return std::to_string(z / 10) + "." + std::to_string(std::llabs(z % 10));
}

std::string MillisText(std::chrono::steady_clock::time_point t0) {
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    return std::to_string(ms) + " ms";
}

} // namespace

bool Loese(fbgame::Spiel& spiel, const fbindex::Index& idx, const fbfigur::Modell& m,
           std::vector<fbbeipack::MeshMaterial>& aus, std::vector<std::string>& protokoll,
           std::string& fehler) {
    // 0.98.0: Fahrzeuge liegen in LEVEL-Bundles; ihre MeshVariationDatabase
    // steht dort ebenso wie bei Figuren, aber die Quelle kann ein Bundle sein,
    // das den Namen nur als Teilstring traegt. FindeBundle deckt beides ab.
    const fbindex::BundleInfo* b = FindeBundle(idx, m.quelle);
    if (b == nullptr) { fehler = "Bundle nicht gefunden: " + m.quelle; return false; }

    // Alle EBX des Bundles lesen, GUID -> Name merken (0.99.0: dazu die Bundles
    // aus m.weitereBundles - bei Fahrzeugen liegt jedes Teil in einem anderen).
    std::set<size_t> nummern{ b->nummer };
    for (const std::string& w : m.weitereBundles) {
        const fbindex::BundleInfo* b2 = FindeBundle(idx, w);
        if (b2 != nullptr) nummern.insert(b2->nummer);
    }
    // 1.01.0: Auch die Bundles JEDES Meshes dazunehmen. Gemessen an DHs Import:
    // alle sechs Teile des Transporters kamen aus demselben Level-Bundle, die
    // MeshVariationDatabase der uebrigen fuenf steht aber woanders - nur 6 von
    // 28 Meshes bekamen Texturen. Der Index kennt zu jedem Mesh seine Bundles.
    {
        std::set<std::string> schon;
        for (const fbfigur::Mesh& me : m.meshes) {
            if (me.meshAsset.empty() || !schon.insert(Klein(me.meshAsset)).second) continue;
            for (const auto& kv : idx.res) {
                if (Klein(kv.first) != Klein(me.meshAsset)) continue;
                for (size_t n : kv.second.bundles) nummern.insert(n);
                break;
            }
            for (const auto& kv : idx.ebx) {
                if (Klein(kv.first) != Klein(me.meshAsset)) continue;
                for (size_t n : kv.second.bundles) nummern.insert(n);
                break;
            }
        }
    }
    std::map<std::string, std::unique_ptr<fbebx::Datei>> ebx;
    std::map<std::string, std::string> guidName;
    for (const auto& kv : idx.ebx) {
        bool drin = false;
        for (size_t n : nummern) if (ImBundle(kv.second, n)) { drin = true; break; }
        if (!drin) continue;
        std::vector<uint8_t> roh;
        std::string f;
        auto d = std::make_unique<fbebx::Datei>();
        if (!spiel.HoleNachSha1(kv.second.sha1, roh, f) || !d->Lies(roh, f)) continue;
        guidName[Klein(d->DateiGuid())] = kv.first;
        ebx[kv.first] = std::move(d);
    }

    // MeshAssets: Materials in Reihenfolge, je mit Instanz-GUID.
    std::map<std::string, std::vector<MatQuelle>> asset;
    for (const auto& kv : ebx) {
        const auto& obj = kv.second->Objekte();
        for (const auto& o : obj) {
            if (!o) continue;
            const fbebx::Wert* mats = o->Feldwert("Materials");
            if (mats == nullptr || mats->art != fbebx::Art::Liste) continue;
            auto& ziel = asset[kv.first];
            for (const auto& it : mats->liste) {
                MatQuelle q;
                if (it && it->art == fbebx::Art::Zeiger && it->verweis >= 0 &&
                    static_cast<size_t>(it->verweis) < obj.size() && obj[static_cast<size_t>(it->verweis)]) {
                    const fbebx::Wert& z = *obj[static_cast<size_t>(it->verweis)];
                    q.instanz = Klein(z.guid);
                    const fbebx::Wert* sh = z.Feldwert("Shader");
                    LiesTexturParameter(sh != nullptr ? sh->Feldwert("TextureParameters") : nullptr, guidName, q.tex);
                }
                ziel.push_back(q);
            }
            break;
        }
    }

    // MeshVariationDatabase: Eintraege ohne Variation (Hash 0) zuerst.
    std::map<std::string, std::vector<MatQuelle>> mvdb;
    for (int durchgang = 0; durchgang < 2; ++durchgang) {
        for (const auto& kv : ebx) {
            for (const auto& o : kv.second->Objekte()) {
                if (!o || o->typ.find("MeshVariationDatabase") == std::string::npos) continue;
                const fbebx::Wert* eintr = o->Feldwert("Entries");
                if (eintr == nullptr) continue;
                for (const auto& e : eintr->liste) {
                    if (!e) continue;
                    const fbebx::Wert* hash = e->Feldwert("VariationAssetNameHash");
                    const bool grund = (hash == nullptr || hash->zahl == 0);
                    if ((durchgang == 0) != grund) continue;
                    const fbebx::Wert* mesh = e->Feldwert("Mesh");
                    if (mesh == nullptr || mesh->art != fbebx::Art::Import) continue;
                    const auto it = guidName.find(Klein(mesh->text));
                    if (it == guidName.end() || mvdb.count(it->second)) continue;
                    auto& ziel = mvdb[it->second];
                    const fbebx::Wert* mats = e->Feldwert("Materials");
                    if (mats == nullptr) continue;
                    for (const auto& mt : mats->liste) {
                        MatQuelle q;
                        if (mt) {
                            const fbebx::Wert* mat = mt->Feldwert("Material");
                            if (mat != nullptr && mat->art == fbebx::Art::Import) q.instanz = Klein(mat->typ);
                            LiesTexturParameter(mt->Feldwert("TextureParameters"), guidName, q.tex);
                            if (const fbebx::Wert* sg = mt->Feldwert("SurfaceShaderGuid")) {
                                const auto is = guidName.find(Klein(sg->text));
                                if (is != guidName.end()) q.shader = is->second;
                            }
                        }
                        ziel.push_back(q);
                    }
                }
            }
        }
    }

    // Je Mesh zuordnen.
    aus.assign(m.meshes.size(), fbbeipack::MeshMaterial());
    size_t mitTex = 0;
    for (size_t i = 0; i < m.meshes.size(); ++i) {
        const fbfigur::Mesh& me = m.meshes[i];
        fbbeipack::MeshMaterial& r = aus[i];
        r.meshAsset = me.meshAsset;
        r.materialId = me.materialId;
        if (me.material >= 0 && static_cast<size_t>(me.material) < m.materialien.size()) {
            r.materialName = m.materialien[static_cast<size_t>(me.material)].name;
        }
        const MatQuelle* treffer = nullptr;
        const auto ia = asset.find(me.meshAsset);
        const auto im = mvdb.find(me.meshAsset);
        // 1.02.0: Wenn nichts passt, sagen warum - steht das MeshAsset ueberhaupt
        // unter den gelesenen EBX, kennt der Index seine GUID, gibt es einen
        // MVDB-Eintrag? Bisher stand nur "ueber NICHTS".
        std::string warum;
        if (ia == asset.end() && im == mvdb.end()) {
            const bool ebxDa = ebx.count(me.meshAsset) != 0;
            bool imIndex = false;
            size_t bundleZahl = 0;
            for (const auto& kv : idx.res) {
                if (Klein(kv.first) != Klein(me.meshAsset)) continue;
                imIndex = true;
                bundleZahl = kv.second.bundles.size();
                break;
            }
            warum = std::string(" [EBX gelesen: ") + (ebxDa ? "ja" : "nein") + ", im Index: " + (imIndex ? "ja" : "nein") +
                    ", Bundles des Meshes: " + std::to_string(bundleZahl) + "]";
        }
        const bool idGut = me.materialId >= 0;
        if (ia != asset.end() && idGut && static_cast<size_t>(me.materialId) < ia->second.size()) {
            const MatQuelle& a = ia->second[static_cast<size_t>(me.materialId)];
            if (im != mvdb.end() && !a.instanz.empty()) {
                for (const MatQuelle& v : im->second) {
                    if (v.instanz == a.instanz) { treffer = &v; r.weg = "guid"; break; }
                }
            }
            if (treffer == nullptr && !a.tex.empty()) { treffer = &a; r.weg = "shader"; }
        }
        if (treffer == nullptr && im != mvdb.end() && idGut && static_cast<size_t>(me.materialId) < im->second.size()) {
            treffer = &im->second[static_cast<size_t>(me.materialId)];
            r.weg = "reihenfolge";
        }
        // Augen und Zaehne (0.46.0): der MVDB-Eintrag hat keine Texturparameter,
        // der Shader nimmt seine eigenen. Gemessen bei Anakin: mat_eyes haengt
        // an characters/heads/_shared/eyes/eyes_mp/ss_character_eye_mp_grey, im
        // Bundle liegt dazu t_eye_mp_da_grey; die Zaehne haengen an
        // shaders/presets/ss_characterspreset_teeth, ihre Texturen heissen
        // characters/heads/_shared/teeth/t_mp_teeth_*. Regel: erst der Ordner
        // des Shaders (samt texture/), bei gleicher Endung die Fassung mit dem
        // Namenszusatz des Shaders (grey); sonst Texturen unter
        // characters/heads/_shared/, die das Wort des Materials tragen
        // (eyes -> eye, teeth).
        MatQuelle shaderQuelle;
        if (treffer == nullptr || treffer->tex.empty()) {
            std::string shader;
            if (im != mvdb.end() && idGut && static_cast<size_t>(me.materialId) < im->second.size()) shader = im->second[static_cast<size_t>(me.materialId)].shader;
            const std::string sk = Klein(shader);
            const std::string ordner = sk.substr(0, sk.find_last_of('/') + 1);
            const std::string zusatz = sk.substr(sk.find_last_of('_') + 1);
            std::string wort = Klein(r.materialName);
            for (const char* v : { "mat_", "m_" }) if (wort.compare(0, std::strlen(v), v) == 0) wort = wort.substr(std::strlen(v));
            if (wort.size() > 4 && wort.back() == 's') wort.pop_back();                       // eyes -> eye
            std::string farbeName, normalName;
            int farbeGuete = -1, normalGuete = -1;
            for (const auto& t2 : idx.res) {
                if (t2.second.resType != 0x6BDE20BAu) continue;
                const std::string tn = Klein(t2.first);
                const bool imOrdner = !ordner.empty() && ordner.compare(0, 8, "shaders/") != 0 && tn.compare(0, ordner.size(), ordner) == 0;
                const bool perWort = wort.size() >= 3 && tn.compare(0, 24, "characters/heads/_shared") == 0 &&
                                     tn.find(wort, tn.find_last_of('/')) != std::string::npos;
                if (!imOrdner && !perWort) continue;
                const std::string bl = tn.substr(tn.find_last_of('/') + 1);
                auto hat = [&](const char* e) { const size_t l = std::strlen(e); return bl.size() > l && bl.compare(bl.size() - l, l, e) == 0; };
                const int bonus = (imOrdner ? 10 : 0) + ((!zusatz.empty() && bl.size() > zusatz.size() && hat(("_" + zusatz).c_str())) ? 5 : 0);
                int gf = -1, gn = -1;
                if (bl.find("_da") != std::string::npos || hat("_d") || hat("_c") || hat("_cs")) gf = bonus;
                if (hat("_n") || hat("_nrs") || hat("_nm")) gn = bonus;
                if (gf > farbeGuete) { farbeGuete = gf; farbeName = t2.first; }
                if (gn > normalGuete) { normalGuete = gn; normalName = t2.first; }
            }
            if (!farbeName.empty()) shaderQuelle.tex.emplace_back("BaseColor", farbeName);
            if (!normalName.empty()) shaderQuelle.tex.emplace_back("Normal", normalName);
            if (!shaderQuelle.tex.empty()) { treffer = &shaderQuelle; r.weg = shader.empty() ? "materialname" : "shaderordner"; }
        }
        // Waffen (0.42.0): nicht in der MVDB des Figuren-Bundles. Ihre Texturen
        // liegen im Ordner der Waffe (t_<waffe>_cs = Farbe, _nam/_nm/_n = Normalen).
        // 1.02.0: derselbe Weg fuer FAHRZEUGE - die MVDB der meisten Teile liegt
        // nicht in den Bundles, aus denen sie geladen werden. Ihre Texturen
        // stehen aber im selben Ordner wie das Mesh.
        MatQuelle ordnerQuelle;
        if (treffer == nullptr && Klein(me.meshAsset).compare(0, 18, "gameplay/vehicles/") == 0) {
            const std::string ordner = Klein(me.meshAsset.substr(0, me.meshAsset.find_last_of('/') + 1));
            // Welcher Texturensatz? "…textureset2" -> "_02_"
            std::string satz;
            {
                const std::string mn = Klein(r.materialName);
                const size_t p2 = mn.find("textureset");
                if (p2 != std::string::npos && p2 + 10 < mn.size() && mn[p2 + 10] >= '1' && mn[p2 + 10] <= '9')
                    satz = std::string("_0") + mn[p2 + 10] + "_";
            }
            for (const auto& t : idx.res) {
                if (t.second.resType != 0x6BDE20BAu) continue;
                const std::string tn = Klein(t.first);
                if (tn.compare(0, ordner.size(), ordner) != 0 || tn.find('/', ordner.size()) != std::string::npos) continue;
                auto endet = [&](const char* x) { const size_t l = std::strlen(x); return tn.size() >= l && tn.compare(tn.size() - l, l, x) == 0; };
                // Fahrzeugtexturen heissen ..._cs (Farbe) und ..._nma (Normalen) -
                // gemessen an DHs Import (t_aalstormtroopertransport_01_cs/_nma).
                // 1.03.0: Der Ordner enthaelt MEHRERE Saetze (_01_, _02_, _03_),
                // und das Material sagt, welcher gemeint ist: die Namen enden auf
                // "textureset1/2/3". Ohne diese Zuordnung bekam jedes Teil den
                // ersten Satz - das Schiff sah falsch aus, obwohl Texturen da waren.
                if (!satz.empty() && tn.find(satz) == std::string::npos) continue;
                if (endet("_cs") || endet("_c")) ordnerQuelle.tex.emplace_back("BaseColor", t.first);
                else if (endet("_nma") || endet("_nam") || endet("_nm") || endet("_n")) ordnerQuelle.tex.emplace_back("Normal", t.first);
            }
            if (!ordnerQuelle.tex.empty()) { treffer = &ordnerQuelle; r.weg = "ordner"; }
        }
        if (treffer == nullptr && Klein(me.meshAsset).compare(0, 19, "gameplay/equipment/") == 0) {
            const std::string ordner = Klein(me.meshAsset.substr(0, me.meshAsset.find_last_of('/') + 1));
            for (const auto& t : idx.res) {
                if (t.second.resType != 0x6BDE20BAu) continue;
                const std::string tn = Klein(t.first);
                if (tn.compare(0, ordner.size(), ordner) != 0 || tn.find('/', ordner.size()) != std::string::npos) continue;
                auto endet = [&](const char* x) { const size_t l = std::strlen(x); return tn.size() >= l && tn.compare(tn.size() - l, l, x) == 0; };
                if (endet("_cs") || endet("_c")) ordnerQuelle.tex.emplace_back("BaseColor", t.first);
                else if (endet("_nam") || endet("_nm") || endet("_n")) ordnerQuelle.tex.emplace_back("Normal", t.first);
            }
            if (!ordnerQuelle.tex.empty()) { treffer = &ordnerQuelle; r.weg = "ordner"; }
        }

        std::string zeile = "MATERIAL " + std::to_string(i) + " " + Blatt(me.meshAsset) + "#" + std::to_string(me.materialId) +
                            " " + r.materialName + " ueber " + (r.weg.empty() ? std::string("NICHTS") : r.weg) + (r.weg.empty() ? warum : std::string()) + ":";
        if (treffer != nullptr) {
            for (const auto& pt : treffer->tex) {
                fbbeipack::Textur t;
                t.parameter = pt.first;
                t.name = pt.second;
                t.art = fbbeipack::ArtVon(pt.first);
                r.texturen.push_back(t);
                zeile += " " + pt.first + "=" + (pt.second[0] == '?' ? std::string("(nicht im Bundle)") : Blatt(pt.second));
            }
            if (!r.texturen.empty()) ++mitTex;
        }
        protokoll.push_back(zeile);
    }
    protokoll.push_back("MATERIALIEN " + std::to_string(aus.size()) + " Meshes, davon " + std::to_string(mitTex) +
                        " mit Texturen; MeshAssets " + std::to_string(asset.size()) + ", MVDB-Eintraege " +
                        std::to_string(mvdb.size()) + ", Bundles " + std::to_string(nummern.size()));
    return true;
}

bool SchreibeTexturen(fbgame::Spiel& spiel, const fbindex::Index& idx,
                      std::vector<fbbeipack::MeshMaterial>& mm, const std::string& ordner,
                      std::vector<std::string>& protokoll, std::string& fehler) {
    const char trenner = (ordner.find('\\') != std::string::npos) ? '\\' : '/';
    struct Fertig { std::string datei, alpha, glanz, metall; int32_t format = 0; uint32_t b = 0, h = 0; bool ok = false; };
    // Kanalbelegung (0.45.0), GEMESSEN an Anakin (Mittel je Kanal, START.log):
    //   _cs  Farbe RGB + Alpha = Glanz (Koerper: A-Mittel 55, schwankt)
    //   _ns  Normale RG + B = Glanz (Kopf: B-Mittel 101, Haare 37/74), A = 255
    //   _nm  Normale RG + B = Metall (Stoff: B-Mittel 3,9), A = 255
    // Dazu passt die bekannte Frostbite-Packung "Color_Smoothness". Die
    // Auszuege werden als eigene Graustufen-PNGs geschrieben, BEVOR die
    // Normale ihr Z ins B bekommt.
    auto endung = [](const std::string& name, const char* e) {
        const size_t l = std::strlen(e);
        return name.size() > l && name.compare(name.size() - l, l, e) == 0;
    };
    auto schreibeKanal = [&](const std::string& ziel, const std::vector<uint8_t>& rgba, uint32_t b, uint32_t h, int kanal,
                             double& mittelAus, std::string& fe2) {
        std::vector<uint8_t> g(static_cast<size_t>(b) * h * 4);
        double summe = 0.0;
        for (size_t i = 0, n = static_cast<size_t>(b) * h; i < n; ++i) {
            const uint8_t w = rgba[i * 4 + static_cast<size_t>(kanal)];
            g[i * 4 + 0] = g[i * 4 + 1] = g[i * 4 + 2] = w;
            g[i * 4 + 3] = 255;
            summe += w;
        }
        mittelAus = summe / std::max<double>(1.0, static_cast<double>(b) * h);
        return fbtextur::SchreibePng(ziel, g, b, h, 3, fe2);
    };
    // Farbtexturen mit Endung _ca (Farbe + Alpha) und _cm (Farbe + Maske)
    // tragen im Alpha die DECKKRAFT - bei _cs ist es etwas anderes (Glanz).
    // Das ist eine Namensregel; gemessen wird dazu, wie zweigipflig das Alpha
    // ist (Anteil der Pixel unter 16 oder ueber 239).
    auto alphaIstDeckkraft = [](const std::string& name) {
        const size_t n = name.size();
        return n > 3 && name[n - 3] == '_' && name[n - 2] == 'c' && (name[n - 1] == 'a' || name[n - 1] == 'm');
    };
    std::map<std::string, Fertig> schon;
    size_t neu = 0, vorhanden = 0, fehlend = 0;
    for (auto& m : mm) {
        for (auto& t : m.texturen) {
            if (t.name.empty() || t.name[0] == '?') continue;
            const std::string schluessel = t.name + "|" + t.art;
            auto it = schon.find(schluessel);
            if (it == schon.end()) {
                Fertig f;
                const auto t0 = std::chrono::steady_clock::now();
                const auto res = idx.res.find(t.name);
                std::vector<uint8_t> kopfDaten;
                std::string fe;
                fbtextur::Kopf k;
                if (res == idx.res.end() || res->second.resType != 0x6BDE20BAu) {
                    protokoll.push_back("TEXTUR " + Blatt(t.name) + ": keine Textur-RES im Index");
                } else if (!spiel.HoleNachSha1(res->second.sha1, kopfDaten, fe) || !fbtextur::LiesKopf(kopfDaten, k, fe)) {
                    protokoll.push_back("TEXTUR " + Blatt(t.name) + ": Kopf nicht lesbar: " + fe);
                } else {
                    f.format = k.format;
                    f.b = k.breite;
                    f.h = k.hoehe;
                    f.datei = ordner + trenner + Blatt(t.name) + (t.art == "normal" ? "_n" : "") + ".png";
                    const bool mitAlpha = (t.art == "farbe") && alphaIstDeckkraft(t.name);
                    if (mitAlpha) f.alpha = ordner + trenner + Blatt(t.name) + "_a.png";
                    const std::string blatt = Blatt(t.name);
                    const bool glanzAusA = (t.art == "farbe") && endung(blatt, "_cs");
                    const bool glanzAusB = (t.art == "normal") && endung(blatt, "_ns");
                    const bool metallAusB = (t.art == "normal") && endung(blatt, "_nm");
                    if (glanzAusA || glanzAusB) f.glanz = ordner + trenner + blatt + "_gloss.png";
                    if (metallAusB) f.metall = ordner + trenner + blatt + "_metal.png";
                    if (fbdatei::Existiert(f.datei) && (!mitAlpha || fbdatei::Existiert(f.alpha)) &&
                        (f.glanz.empty() || fbdatei::Existiert(f.glanz)) && (f.metall.empty() || fbdatei::Existiert(f.metall))) {
                        f.ok = true;
                        ++vorhanden;
                        protokoll.push_back("TEXTUR " + Blatt(t.name) + " " + t.parameter + " " + fbtextur::FormatName(k.format) + " " +
                                            std::to_string(k.breite) + "x" + std::to_string(k.hoehe) + " vorhanden");
                    } else {
                        const auto ch = idx.chunks.find(fbtextur::ChunkGuid(k));
                        std::vector<uint8_t> chunk, rgba;
                        if (ch == idx.chunks.end()) {
                            protokoll.push_back("TEXTUR " + Blatt(t.name) + ": Chunk " + fbtextur::ChunkGuid(k) + " fehlt im Index");
                        } else if (!spiel.HoleNachSha1(ch->second.sha1, chunk, fe) || !fbtextur::DekodiereMip0(k, chunk, rgba, fe)) {
                            protokoll.push_back("TEXTUR " + Blatt(t.name) + ": " + fe);
                        } else {
                            double mittel[4];
                            fbtextur::Mittel(rgba, mittel);
                            std::string zusatz = " mittel R=" + Z1(mittel[0]) + " G=" + Z1(mittel[1]) + " B=" + Z1(mittel[2]) +
                                                 " A=" + Z1(mittel[3]);
                            int kanaele = 4;
                            {
                                double mw = 0.0;
                                std::string fk;
                                if (!f.glanz.empty()) {
                                    if (schreibeKanal(f.glanz, rgba, k.breite, k.hoehe, glanzAusA ? 3 : 2, mw, fk))
                                        zusatz += "; Glanz aus " + std::string(glanzAusA ? "A" : "B") + " (Mittel " + Z1(mw) + ")";
                                    else { protokoll.push_back("TEXTUR " + blatt + ": Glanz-PNG " + fk); f.glanz.clear(); }
                                }
                                if (!f.metall.empty()) {
                                    if (schreibeKanal(f.metall, rgba, k.breite, k.hoehe, 2, mw, fk))
                                        zusatz += "; Metall aus B (Mittel " + Z1(mw) + ")";
                                    else { protokoll.push_back("TEXTUR " + blatt + ": Metall-PNG " + fk); f.metall.clear(); }
                                }
                            }
                            if (t.art == "normal") {
                                double anteil = 0.0;
                                fbtextur::BaueNormale(rgba, anteil);
                                zusatz += "; altes B passte zu Z bei " + Z1(anteil * 100.0) + " %";
                                kanaele = 3;
                            } else if (t.art == "farbe") {
                                kanaele = 3;
                                if (mitAlpha) {
                                    std::vector<uint8_t> a(static_cast<size_t>(k.breite) * k.hoehe * 4);
                                    size_t zwei = 0;
                                    for (size_t i = 0, n = static_cast<size_t>(k.breite) * k.hoehe; i < n; ++i) {
                                        const uint8_t w = rgba[i * 4 + 3];
                                        a[i * 4 + 0] = a[i * 4 + 1] = a[i * 4 + 2] = w;
                                        a[i * 4 + 3] = 255;
                                        if (w < 16 || w > 239) ++zwei;
                                    }
                                    const double anteil = 100.0 * static_cast<double>(zwei) / (static_cast<double>(k.breite) * k.hoehe);
                                    zusatz += "; Alpha als Deckkraft, zweigipflig " + Z1(anteil) + " %";
                                    std::string fa;
                                    if (!fbtextur::SchreibePng(f.alpha, a, k.breite, k.hoehe, 3, fa)) {
                                        protokoll.push_back("TEXTUR " + Blatt(t.name) + ": Alpha-PNG " + fa);
                                        f.alpha.clear();
                                    }
                                }
                            }
                            if (fbtextur::SchreibePng(f.datei, rgba, k.breite, k.hoehe, kanaele, fe)) {
                                f.ok = true;
                                ++neu;
                                protokoll.push_back("TEXTUR " + Blatt(t.name) + " " + t.parameter + " " + fbtextur::FormatName(k.format) +
                                                    " " + std::to_string(k.breite) + "x" + std::to_string(k.hoehe) + zusatz +
                                                    " -> neu (" + MillisText(t0) + ")");
                            } else {
                                protokoll.push_back("TEXTUR " + Blatt(t.name) + ": PNG " + fe);
                            }
                        }
                    }
                }
                if (!f.ok) ++fehlend;
                it = schon.emplace(schluessel, f).first;
            }
            if (it->second.ok) {
                t.datei = it->second.datei;
                t.format = it->second.format;
                t.breite = it->second.b;
                t.hoehe = it->second.h;
            }
        }
        // Deckkraft-Eintraege erst NACH der Schleife anhaengen (sonst
        // verschiebt push_back die Liste, ueber die gerade gelaufen wird).
        std::vector<fbbeipack::Textur> dazu;
        for (const auto& t : m.texturen) {
            if (t.art != "farbe") continue;
            const auto it = schon.find(t.name + "|" + t.art);
            if (it == schon.end() || !it->second.ok || it->second.alpha.empty()) continue;
            fbbeipack::Textur d = t;
            d.parameter = t.parameter + ".Alpha";
            d.art = "deckkraft";
            d.datei = it->second.alpha;
            dazu.push_back(d);
        }
        for (const auto& t : m.texturen) {
            const auto it = schon.find(t.name + "|" + t.art);
            if (it == schon.end() || !it->second.ok) continue;
            if (!it->second.glanz.empty()) {
                fbbeipack::Textur d = t;
                d.parameter = t.parameter + (t.art == "farbe" ? ".Alpha" : ".B");
                d.art = "glanz";
                d.datei = it->second.glanz;
                dazu.push_back(d);
            }
            if (!it->second.metall.empty()) {
                fbbeipack::Textur d = t;
                d.parameter = t.parameter + ".B";
                d.art = "metall";
                d.datei = it->second.metall;
                dazu.push_back(d);
            }
        }
        for (const auto& d : dazu) m.texturen.push_back(d);
    }
    protokoll.push_back("TEXTUREN neu " + std::to_string(neu) + ", vorhanden " + std::to_string(vorhanden) +
                        ", fehlgeschlagen " + std::to_string(fehlend));
    (void)fehler;
    return true;
}

} // namespace fbmaterial
