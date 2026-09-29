// ============================================================
//  fbgame.cpp - Pfade, layout.toc, Kataloge, Manifest, initfs.
// ============================================================
#include "fbgame.h"
#include "fbdatei.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <direct.h>
#else
#include <dirent.h>
#endif
#include <sys/stat.h>

namespace fbgame {
namespace {

bool IstOrdner(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    return (st.st_mode & S_IFMT) == S_IFDIR;
}

bool IstDatei(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    return (st.st_mode & S_IFMT) == S_IFREG;
}

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Trennzeichen vereinheitlichen: im Dump stehen Schraegstriche,
// Windows will Rueckstriche. Unter Linux ist beides recht.
std::string Pfad(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    std::string s = a;
    if (s.back() != '/' && s.back() != '\\') s += '/';
    s += b;
    return s;
}

std::string OhnePraefix(std::string s) {
    const char* p1 = "native_data/";
    const char* p2 = "native_patch/";
    if (s.rfind(p1, 0) == 0) s = s.substr(std::strlen(p1));
    else if (s.rfind(p2, 0) == 0) s = s.substr(std::strlen(p2));
    while (!s.empty() && (s.front() == '/' || s.front() == '\\')) s.erase(s.begin());
    return s;
}

struct RohLeser {
    const uint8_t* d; size_t n; size_t p = 0; bool bad = false;
    explicit RohLeser(const std::vector<uint8_t>& v) : d(v.data()), n(v.size()) {}
    bool brauche(size_t k) { if (bad || p + k > n) { bad = true; return false; } return true; }
    uint32_t u32() {
        if (!brauche(4)) return 0;
        const uint32_t v = static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p+1]) << 8) |
                           (static_cast<uint32_t>(d[p+2]) << 16) | (static_cast<uint32_t>(d[p+3]) << 24);
        p += 4; return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    int64_t i64() {
        if (!brauche(8)) return 0;
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | d[p + i];
        p += 8; return static_cast<int64_t>(v);
    }
    void guid(uint8_t aus[16]) { if (brauche(16)) { std::memcpy(aus, d + p, 16); p += 16; } }
    // Das Manifest ist klein-endig, hier wird NICHT gedreht - anders als im
    // Bundle, wo NativeReader.ReadGuid(Endian.Big) Data1..3 umdreht.
};

} // namespace

// ------------------------------------------------------------
void Spiel::QuelleDazu(const std::string& unterordner, bool durchsuchen) {
    const std::string p = Pfad(wurzel_, unterordner);
    if (!IstOrdner(p)) return;
    if (!durchsuchen) { pfade_.push_back(p); return; }
    // Update\<name>\Data, aber keine Patch-Zweige. Bei SWBF2 gibt es das
    // nicht; der Zweig bleibt trotzdem drin, damit die Reihenfolge stimmt.
#if !defined(_WIN32)
    DIR* dir = opendir(p.c_str());
    if (dir == nullptr) return;
    while (dirent* e = readdir(dir)) {
        if (e->d_name[0] == '.') continue;
        const std::string unter = Pfad(p, e->d_name);
        if (!IstOrdner(unter)) continue;
        if (Klein(unter).find("patch") != std::string::npos) continue;
        if (IstDatei(Pfad(unter, "package.mft"))) pfade_.push_back(Pfad(unter, "Data"));
    }
    closedir(dir);
#endif
}

std::string Spiel::Aufloesen(const std::string& name) const {
    if (name.rfind("native_patch/", 0) == 0 && pfade_.size() == 1) return std::string();
    size_t start = 0, ende = pfade_.size();
    if (name.rfind("native_data/", 0) == 0 && pfade_.size() > 1) start = 1;
    else if (name.rfind("native_patch/", 0) == 0) ende = 1;
    const std::string rest = OhnePraefix(name);
    for (size_t i = start; i < ende; ++i) {
        const std::string kand = Pfad(pfade_[i], rest);
        if (IstDatei(kand) || IstOrdner(kand)) return kand;
    }
    return std::string();
}

std::string Spiel::CasPfadFuerRef(int32_t dateiRef) const {
    const int katIndex = (dateiRef >> 12) - 1;
    const bool imPatch = (dateiRef & 0x100) != 0;
    const int casIndex = (dateiRef & 0xFF) + 1;
    std::string kat;
    if (katIndex >= 0 && static_cast<size_t>(katIndex) < kataloge_.size()) {
        kat = kataloge_[static_cast<size_t>(katIndex)].name;
    }
    char b[32];
    std::snprintf(b, sizeof b, "/cas_%02d.cas", casIndex);
    return (imPatch ? "native_patch/" : "native_data/") + kat + b;
}

bool Spiel::Oeffne(const std::string& wurzel, std::string& fehler) {
    wurzel_ = wurzel;
    pfade_.clear();
    // Reihenfolge wie Frosty: erst Patch, dann Update, dann Data.
    QuelleDazu("Patch", false);
    QuelleDazu("Update", true);
    QuelleDazu("Data", false);
    if (pfade_.empty()) { fehler = "weder Data noch Patch unter " + wurzel; return false; }

    if (!LiesLayouts(fehler)) return false;
    LiesInitfs();
    if (!LiesKataloge(fehler)) return false;

    if (const std::vector<uint8_t>* wb = SpeicherDatei("ebx.dict")) {
        leser_.SetzeWoerterbuch(*wb);
    }
    return true;
}

bool Spiel::LiesLayouts(std::string& fehler) {
    const std::string basisPfad = Aufloesen("native_data/layout.toc");
    const std::string patchPfad = Aufloesen("native_patch/layout.toc");
    if (basisPfad.empty()) { fehler = "Data/layout.toc fehlt"; return false; }

    auto lies = [&](const std::string& p, fbdb::WertPtr& aus) -> bool {
        std::vector<uint8_t> roh;
        if (!fbcas::LiesDatei(p, roh, fehler)) return false;
        return fbdb::LiesDbObjekt(roh, aus, fehler) && aus;
    };

    fbdb::WertPtr basis, patch;
    if (!lies(basisPfad, basis)) return false;
    if (const fbdb::Wert* sbs = basis->Feldwert("superBundles")) {
        for (const fbdb::WertPtr& sb : sbs->liste) {
            if (const fbdb::Wert* n = sb->Feldwert("name")) superbundles_.push_back(Klein(n->text));
        }
    }
    const fbdb::Wert* layout = basis.get();
    if (!patchPfad.empty() && lies(patchPfad, patch) && patch) {
        if (const fbdb::Wert* sbs = patch->Feldwert("superBundles")) {
            for (const fbdb::WertPtr& sb : sbs->liste) {
                if (const fbdb::Wert* n = sb->Feldwert("name")) {
                    const std::string k = Klein(n->text);
                    if (std::find(superbundles_.begin(), superbundles_.end(), k) == superbundles_.end()) {
                        superbundles_.push_back(k);
                    }
                }
            }
        }
        layout = patch.get();
    }
    if (const fbdb::Wert* v = layout->Feldwert("base")) basisNummer_ = v->zahl;
    if (const fbdb::Wert* v = layout->Feldwert("head")) kopfNummer_ = v->zahl;

    // ---- Kataloge aus dem installManifest --------------------
    const fbdb::Wert* im = layout->Feldwert("installManifest");
    if (im == nullptr) {
        Katalogangabe k; k.name = ""; k.immerInstalliert = true; k.gueltig = true;
        kataloge_.push_back(k);
    } else if (const fbdb::Wert* ics = im->Feldwert("installChunks")) {
        for (const fbdb::WertPtr& ic : ics->liste) {
            const fbdb::Wert* dlc = ic->Feldwert("testDLC");
            if (dlc != nullptr && dlc->wahr) continue;
            const fbdb::Wert* nm = ic->Feldwert("name");
            Katalogangabe k;
            k.name = "win32/" + (nm ? nm->text : std::string());
            const fbdb::Wert* imm = ic->Feldwert("alwaysInstalled");
            k.immerInstalliert = (imm != nullptr && imm->wahr);
            k.gueltig = true;
            if (Aufloesen(k.name + "/cas.cat").empty()) {
                const fbdb::Wert* dateien = ic->Feldwert("files");
                if (dateien == nullptr || dateien->liste.empty()) {
                    hinweise_.push_back("Katalog ohne cas.cat uebersprungen: " + k.name);
                    continue;
                }
            }
            const fbdb::Wert* pi = ic->Feldwert("persistentIndex");
            if (pi != nullptr) {
                if (kataloge_.empty()) kataloge_.resize(ics->liste.size());
                const size_t i = static_cast<size_t>(pi->zahl);
                if (i < kataloge_.size()) kataloge_[i] = k;
            } else {
                kataloge_.push_back(k);
            }
        }
    }

    return LiesManifest(*layout, fehler);
}

bool Spiel::LiesManifest(const fbdb::Wert& layout, std::string& fehler) {
    const fbdb::Wert* m = layout.Feldwert("manifest");
    if (m == nullptr) { fehler = "layout.toc hat kein 'manifest' - das ist kein SWBF2-Layout"; return false; }
    const fbdb::Wert* fref = m->Feldwert("file");
    const fbdb::Wert* voff = m->Feldwert("offset");
    const fbdb::Wert* vsize = m->Feldwert("size");
    if (fref == nullptr || voff == nullptr || vsize == nullptr) {
        fehler = "Manifest ohne file/offset/size"; return false;
    }
    const std::string nativ = CasPfadFuerRef(static_cast<int32_t>(fref->zahl));
    std::vector<uint8_t> daten;
    if (!LiesRoh(nativ, static_cast<uint32_t>(voff->zahl),
                 static_cast<uint32_t>(vsize->zahl), daten, fehler)) {
        return false;
    }

    RohLeser r(daten);
    const uint32_t nDateien = r.u32();
    const uint32_t nBundles = r.u32();
    const uint32_t nChunks = r.u32();
    manifestDateien_.clear();
    manifestDateien_.reserve(nDateien);
    for (uint32_t i = 0; i < nDateien && !r.bad; ++i) {
        ManifestDatei f;
        f.dateiRef = r.i32();
        f.versatz = r.u32();
        f.laenge = r.i64();
        manifestDateien_.push_back(f);
    }
    manifestBundles_.clear();
    manifestBundles_.reserve(nBundles);
    for (uint32_t i = 0; i < nBundles && !r.bad; ++i) {
        ManifestBundle b;
        b.hash = static_cast<uint32_t>(r.i32());
        const int32_t start = r.i32();
        const int32_t anzahl = r.i32();
        r.i32(); r.i32();
        for (int32_t k = 0; k < anzahl; ++k) {
            const size_t idx = static_cast<size_t>(start + k);
            if (idx < manifestDateien_.size()) b.dateien.push_back(manifestDateien_[idx]);
        }
        manifestBundles_.push_back(b);
    }
    manifestChunks_.clear();
    manifestChunks_.reserve(nChunks);
    for (uint32_t i = 0; i < nChunks && !r.bad; ++i) {
        ManifestChunk c;
        r.guid(c.guid);
        const int32_t fi = r.i32();
        if (fi >= 0 && static_cast<size_t>(fi) < manifestDateien_.size()) {
            c.datei = manifestDateien_[static_cast<size_t>(fi)];
        }
        manifestChunks_.push_back(c);
    }
    if (r.bad) { fehler = "Manifest endet mitten in den Eintraegen"; return false; }
    return true;
}

void Spiel::LiesInitfs() {
    const std::string p = Aufloesen("initfs_win32");
    if (p.empty()) {
        hinweise_.push_back("initfs_win32 nicht gefunden - zstd mit Woerterbuch schlaegt fehl");
        return;
    }
    std::vector<uint8_t> roh;
    std::string fehler;
    if (!fbcas::LiesDatei(p, roh, fehler)) return;
    fbdb::WertPtr baum;
    if (!fbdb::LiesDbObjekt(roh, baum, fehler) || !baum) return;
    for (const fbdb::WertPtr& stub : baum->liste) {
        const fbdb::Wert* f = stub->Feldwert("$file");
        if (f == nullptr) continue;
        const fbdb::Wert* nm = f->Feldwert("name");
        const fbdb::Wert* pl = f->Feldwert("payload");
        if (nm == nullptr || pl == nullptr) continue;
        if (speicherFs_.find(nm->text) == speicherFs_.end()) speicherFs_[nm->text] = pl->bytes;
    }
}

const std::vector<uint8_t>* Spiel::SpeicherDatei(const std::string& endung) const {
    const std::string e = Klein(endung);
    for (const auto& kv : speicherFs_) {
        const std::string n = Klein(kv.first);
        if (n.size() >= e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0) {
            return &kv.second;
        }
    }
    return nullptr;
}

bool Spiel::LiesKataloge(std::string& fehler) {
    for (const Katalogangabe& kat : kataloge_) {
        if (!kat.gueltig || kat.name.empty()) continue;
        for (const char* praefix : { "native_data/", "native_patch/" }) {
            const std::string nativ = std::string(praefix) + kat.name + "/cas.cat";
            const std::string p = Aufloesen(nativ);
            if (p.empty()) continue;
            std::vector<uint8_t> roh;
            if (!fbcas::LiesDatei(p, roh, fehler)) continue;
            fbcas::Katalog k;
            if (!fbcas::LiesKatalog(roh, k, fehler)) continue;
            const std::string katalogNativ = nativ.substr(0, nativ.size() - 8);  // ohne "/cas.cat"
            for (const fbcas::CatEintrag& e : k.eintraege) {
                if (e.logischerVersatz != 0) continue;
                const std::string s = fbcas::Sha1Text(e.sha1);
                if (catEintraege_.find(s) == catEintraege_.end()) {
                    catEintraege_[s] = CatFund{ e, katalogNativ };
                }
            }
            for (const fbcas::CatPatch& pp : k.patches) {
                const std::string s = fbcas::Sha1Text(pp.sha1);
                if (catPatches_.find(s) == catPatches_.end()) catPatches_[s] = pp;
            }
        }
    }
    fehler.clear();
    return true;
}

bool Spiel::LiesRoh(const std::string& nativPfad, uint32_t versatz, uint32_t laenge,
                    std::vector<uint8_t>& aus, std::string& fehler) const {
    const std::string p = Aufloesen(nativPfad);
    if (p.empty()) { fehler = "cas-Datei fehlt: " + nativPfad; return false; }
    // Bis 0.32.0 ging der Versatz als long an fseek - unter 64-Bit-Windows
    // sind das 32 Bit, ab 2 GiB also negativ. Der Rueckgabewert wurde nicht
    // geprueft, ein misslungener Sprung las still ab Dateianfang. Jetzt
    // uint64_t ueber _fseeki64/fseeko, und jeder Schritt wird geprueft.
    std::string grund;
    if (!fbdatei::LiesBereich(p, versatz, laenge, aus, grund)) {
        fehler = "cas-Datei " + grund;
        return false;
    }
    return true;
}

Spiel::Fundort Spiel::Finde(const std::string& sha1Hex) const {
    Fundort f;
    const std::string s = Klein(sha1Hex);
    auto pit = catPatches_.find(s);
    if (pit != catPatches_.end()) {
        f.art = "patched";
        auto b = catEintraege_.find(fbcas::Sha1Text(pit->second.basisSha1));
        auto d = catEintraege_.find(fbcas::Sha1Text(pit->second.deltaSha1));
        if (b != catEintraege_.end()) {
            char t[32]; std::snprintf(t, sizeof t, "/cas_%02d.cas", b->second.eintrag.archiv);
            f.basisCas = b->second.katalogNativ + t;
            f.basisVersatz = b->second.eintrag.versatz; f.basisLaenge = b->second.eintrag.laenge;
        }
        if (d != catEintraege_.end()) {
            char t[32]; std::snprintf(t, sizeof t, "/cas_%02d.cas", d->second.eintrag.archiv);
            f.deltaCas = d->second.katalogNativ + t;
            f.deltaVersatz = d->second.eintrag.versatz; f.deltaLaenge = d->second.eintrag.laenge;
        }
        return f;
    }
    auto it = catEintraege_.find(s);
    if (it == catEintraege_.end()) return f;
    char t[32]; std::snprintf(t, sizeof t, "/cas_%02d.cas", it->second.eintrag.archiv);
    f.art = "cas"; f.cas = it->second.katalogNativ + t;
    f.versatz = it->second.eintrag.versatz; f.laenge = it->second.eintrag.laenge;
    return f;
}

bool Spiel::HoleNachSha1(const std::string& sha1Hex, std::vector<uint8_t>& aus,
                         std::string& fehler) {
    const std::string s = Klein(sha1Hex);

    // Ein cas-Pfad aus einem Katalogeintrag: der Katalog steht im Namen,
    // die Archivnummer im Eintrag. Gesucht wird ueber alle Kataloge -
    // dieselbe SHA-1 kommt nur einmal vor.
    // Der Pfad kommt aus DEM Katalog, in dem der Eintrag steht - nicht aus
    // dem ersten, der zufaellig eine cas-Datei mit dieser Nummer hat.
    auto pfadFuer = [&](const CatFund& f) -> std::string {
        char t[32];
        std::snprintf(t, sizeof t, "/cas_%02d.cas", f.eintrag.archiv);
        return f.katalogNativ + t;
    };

    auto pit = catPatches_.find(s);
    if (pit != catPatches_.end()) {
        auto b = catEintraege_.find(fbcas::Sha1Text(pit->second.basisSha1));
        auto d = catEintraege_.find(fbcas::Sha1Text(pit->second.deltaSha1));
        if (b == catEintraege_.end() || d == catEintraege_.end()) {
            fehler = "Patch-Basis oder Delta fehlt fuer " + s;
            return false;
        }
        std::vector<uint8_t> basis, delta;
        if (!LiesRoh(pfadFuer(b->second), b->second.eintrag.versatz,
                     b->second.eintrag.laenge, basis, fehler)) return false;
        if (!LiesRoh(pfadFuer(d->second), d->second.eintrag.versatz,
                     d->second.eintrag.laenge, delta, fehler)) return false;
        aus.clear();
        return leser_.EntpackeGepatcht(basis, delta, aus, fehler);
    }

    auto it = catEintraege_.find(s);
    if (it == catEintraege_.end()) { fehler = "SHA1 steht in keinem cas.cat: " + s; return false; }
    std::vector<uint8_t> roh;
    if (!LiesRoh(pfadFuer(it->second), it->second.eintrag.versatz,
                 it->second.eintrag.laenge, roh, fehler)) return false;
    aus.clear();
    return leser_.EntpackeAlles(roh, aus, fehler);
}

bool Spiel::LiesBundleRoh(const ManifestBundle& mb, std::vector<uint8_t>& aus,
                          std::string& fehler) {
    if (mb.dateien.empty()) { fehler = "Bundle ohne Datei"; return false; }
    const ManifestDatei& f = mb.dateien[0];
    // NICHT entpacken. Der Bundleblob liegt ROH in der cas-Datei - anders als
    // EBX, RES und Chunks, die ueber ihre SHA-1 und die Blockentpackung
    // kommen. Wer ihn durch den Blockleser schickt, bekommt "Nutzdaten
    // reichen nicht", weil die ersten vier Byte die Bundlegroesse sind und
    // kein Blockkopf.
    return LiesRoh(CasPfadFuerRef(f.dateiRef), f.versatz,
                   static_cast<uint32_t>(f.laenge), aus, fehler);
}

bool Spiel::HoleManifestChunk(const ManifestChunk& mc, std::vector<uint8_t>& aus,
                              std::string& fehler) {
    std::vector<uint8_t> roh;
    if (!LiesRoh(CasPfadFuerRef(mc.datei.dateiRef), mc.datei.versatz,
                 static_cast<uint32_t>(mc.datei.laenge), roh, fehler)) {
        return false;
    }
    aus.clear();
    return leser_.EntpackeAlles(roh, aus, fehler);
}

} // namespace fbgame
