// ============================================================
//  fbebx.cpp - EBX lesen.
// ============================================================
#include "fbebx.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <tuple>

namespace fbebx {
namespace {

constexpr uint32_t kMagicV2 = 0x0FB2D1CEu;
constexpr uint32_t kMagicV4 = 0x0FB4D1CEu;

const char* FeldTypName(uint16_t t) {
    static const char* namen[] = {
        "Inherited", "DbObject", "Struct", "Pointer", "Array", "FixedArray",
        "String", "CString", "Enum", "FileRef", "Boolean", "Int8", "UInt8",
        "Int16", "UInt16", "Int32", "UInt32", "UInt64", "Int64", "Float32",
        "Float64", "Guid", "Sha1", "ResourceRef", "Function", "TypeRef",
        "BoxedValueRef", "Interface", "Delegate"
    };
    const uint16_t art = (t >> 4) & 0x1F;    // die ART steht in Bit 4..8
    if (art < sizeof(namen) / sizeof(namen[0])) return namen[art];
    return "?";
}

struct R {
    const uint8_t* d; size_t n; size_t p = 0; bool bad = false;
    R(const std::vector<uint8_t>& v, size_t start = 0) : d(v.data()), n(v.size()), p(start) {}
    bool brauche(size_t k) { if (bad || p + k > n) { bad = true; return false; } return true; }
    uint8_t  u8()  { return brauche(1) ? d[p++] : 0; }
    int8_t   i8()  { return static_cast<int8_t>(u8()); }
    uint16_t u16() { if (!brauche(2)) return 0; const uint16_t v = static_cast<uint16_t>(d[p] | (d[p+1] << 8)); p += 2; return v; }
    int16_t  i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() {
        if (!brauche(4)) return 0;
        const uint32_t v = static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p+1]) << 8) |
                           (static_cast<uint32_t>(d[p+2]) << 16) | (static_cast<uint32_t>(d[p+3]) << 24);
        p += 4; return v;
    }
    int32_t  i32() { return static_cast<int32_t>(u32()); }
    uint64_t u64() { const uint32_t a = u32(), b = u32(); return (static_cast<uint64_t>(b) << 32) | a; }
    int64_t  i64() { return static_cast<int64_t>(u64()); }
    float    f32() { const uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }
    double   f64() { const uint64_t v = u64(); double f; std::memcpy(&f, &v, 8); return f; }
    void ueberspringe(size_t k) { if (brauche(k)) p += k; }
    void aufRunden(size_t k) { if (k) while (p % k) { if (!brauche(1)) return; ++p; } }
    std::string guid() {
        uint8_t g[16] = {};
        if (!brauche(16)) return std::string();
        std::memcpy(g, d + p, 16); p += 16;
        char b[40];
        std::snprintf(b, sizeof b,
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
            g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
        return b;
    }
    std::string hex(size_t k) {
        static const char* z = "0123456789abcdef";
        std::string s;
        if (!brauche(k)) return s;
        s.resize(k * 2);
        for (size_t i = 0; i < k; ++i) { s[i*2] = z[(d[p+i] >> 4) & 0xF]; s[i*2+1] = z[d[p+i] & 0xF]; }
        p += k;
        return s;
    }
    std::string cstr() {
        std::string s;
        while (brauche(1)) { const uint8_t c = d[p++]; if (c == 0) break; s.push_back(static_cast<char>(c)); }
        return s;
    }
};

WertPtr NeuerWert(Art a) { auto w = std::make_shared<Wert>(); w->art = a; return w; }

} // namespace

int32_t HashText(const std::string& s) {
    uint32_t h = 5381u;
    for (unsigned char c : s) h = static_cast<uint32_t>(h * 33u) ^ c;
    return static_cast<int32_t>(h);
}

const Wert* Wert::Feldwert(const std::string& name) const {
    for (const Feld& f : felder) if (f.name == name) return f.wert.get();
    return nullptr;
}

std::string Datei::TextAn(uint32_t versatz) const {
    if (versatz == 0xFFFFFFFFu) return std::string();
    const size_t p = stringsVersatz_ + versatz;
    if (p >= daten_.size()) return std::string();
    size_t e = p;
    while (e < daten_.size() && daten_[e] != 0) ++e;
    return std::string(reinterpret_cast<const char*>(daten_.data() + p), e - p);
}

std::vector<const Wert*> Datei::Suche(const std::string& typ) const {
    std::vector<const Wert*> aus;
    for (const WertPtr& o : objekte_) if (o && o->typ == typ) aus.push_back(o.get());
    return aus;
}

namespace {

struct Leserzustand {
    R* r;
    Datei* self;
};

} // namespace

bool Datei::Lies(const std::vector<uint8_t>& daten, std::string& fehler) {
    daten_ = daten;
    R r(daten_);
    const uint32_t magic = r.u32();
    if (magic != kMagicV2 && magic != kMagicV4) {
        char b[80];
        std::snprintf(b, sizeof b, "kein EBX: Magic 0x%08X", magic);
        fehler = b;
        return false;
    }
    fassung_ = (magic == kMagicV4) ? 4 : 2;
    stringsVersatz_ = r.u32();
    r.u32();                                   // stringsAndDataLen
    const uint32_t guidZahl = r.u32();
    const uint16_t instanzZahl = r.u16();
    const uint16_t exportiertZahl = r.u16();
    r.u16();
    const uint16_t klassenZahl = r.u16();
    const uint16_t feldZahl = r.u16();
    const uint16_t typNamenLaenge = r.u16();
    stringsLaenge_ = r.u32();
    const uint32_t arrayZahl = r.u32();
    datenLaenge_ = r.u32();
    arraysVersatz_ = stringsVersatz_ + stringsLaenge_ + datenLaenge_;
    dateiGuid_ = r.guid();
    uint32_t boxedZahl = 0;
    if (fassung_ == 4) { boxedZahl = r.u32(); r.u32(); }
    else r.aufRunden(16);

    importe_.clear();
    for (uint32_t i = 0; i < guidZahl && !r.bad; ++i) {
        const std::string a = r.guid();
        const std::string b = r.guid();
        importe_.emplace_back(a, b);
    }

    std::map<int32_t, std::string> namen;
    const size_t ende = r.p + typNamenLaenge;
    while (r.p < ende && !r.bad) {
        const std::string n = r.cstr();
        if (n.empty()) continue;
        namen.emplace(HashText(n), n);
    }
    r.p = ende;

    felder_.clear();
    for (uint16_t i = 0; i < feldZahl && !r.bad; ++i) {
        FeldTyp f;
        const int32_t h = r.i32();
        uint16_t t = r.u16();
        f.klassenRef = r.u16();
        f.datenVersatz = r.u32();
        r.u32();
        if (fassung_ != 2) t = static_cast<uint16_t>(t >> 1);
        f.typ = t;
        f.typName = FeldTypName(t);
        const auto it = namen.find(h);
        if (it != namen.end()) f.name = it->second;
        else { char b[16]; std::snprintf(b, sizeof b, "?%08x", static_cast<uint32_t>(h)); f.name = b; }
        felder_.push_back(f);
    }

    klassen_.clear();
    for (uint16_t i = 0; i < klassenZahl && !r.bad; ++i) {
        KlassenTyp k;
        const int32_t h = r.i32();
        k.feldIndex = r.i32();
        k.feldZahl = r.u8();
        k.ausrichtung = r.u8();
        uint16_t t = r.u16();
        if (fassung_ != 2) t = static_cast<uint16_t>(t >> 1);
        k.typ = t;
        k.typName = FeldTypName(t);
        k.groesse = r.u16();
        r.u16();
        const auto it = namen.find(h);
        if (it != namen.end()) k.name = it->second;
        else { char b[16]; std::snprintf(b, sizeof b, "?%08x", static_cast<uint32_t>(h)); k.name = b; }
        klassen_.push_back(k);
    }

    std::vector<std::tuple<uint16_t, uint16_t, bool>> instanzen;
    for (uint16_t i = 0; i < instanzZahl && !r.bad; ++i) {
        const uint16_t cr = r.u16();
        const uint16_t c = r.u16();
        instanzen.emplace_back(cr, c, i < exportiertZahl);
    }
    r.aufRunden(16);
    arrays_.clear();
    for (uint32_t i = 0; i < arrayZahl && !r.bad; ++i) {
        const uint32_t off = r.u32();
        const uint32_t anzahl = r.u32();
        const int32_t cr = r.i32();
        arrays_.emplace_back(off, anzahl, cr);
    }
    r.aufRunden(16);
    for (uint32_t i = 0; i < boxedZahl && !r.bad; ++i) { r.u32(); r.u16(); r.u16(); }

    if (r.bad) { fehler = "EBX: Kopf unvollstaendig"; return false; }

    // ---- Objekte -------------------------------------------------
    R ro(daten_, stringsVersatz_ + stringsLaenge_);
    int64_t index = 0;

    // Rekursion ueber Lambdas, damit die Zustaende beisammen bleiben.
    std::function<WertPtr(R&, const std::string&, uint16_t)> liesFeld;
    std::function<void(R&, const KlassenTyp&, Wert&)> liesKlasse;

    liesFeld = [&](R& rr, const std::string& t, uint16_t klassenRef) -> WertPtr {
        if (t == "Boolean") { auto w = NeuerWert(Art::Bool); w->wahr = (rr.u8() != 0); return w; }
        if (t == "Int8")    { auto w = NeuerWert(Art::Ganz); w->zahl = rr.i8(); return w; }
        if (t == "UInt8")   { auto w = NeuerWert(Art::Ganz); w->zahl = rr.u8(); return w; }
        if (t == "Int16")   { auto w = NeuerWert(Art::Ganz); w->zahl = rr.i16(); return w; }
        if (t == "UInt16")  { auto w = NeuerWert(Art::Ganz); w->zahl = rr.u16(); return w; }
        if (t == "Int32")   { auto w = NeuerWert(Art::Ganz); w->zahl = rr.i32(); return w; }
        if (t == "UInt32")  { auto w = NeuerWert(Art::Ganz); w->zahl = rr.u32(); return w; }
        if (t == "Int64")   { auto w = NeuerWert(Art::Ganz); w->zahl = rr.i64(); return w; }
        if (t == "UInt64")  { auto w = NeuerWert(Art::Ganz); w->zahl = static_cast<int64_t>(rr.u64()); return w; }
        if (t == "Float32") { auto w = NeuerWert(Art::Gleit); w->gleit = rr.f32(); return w; }
        if (t == "Float64") { auto w = NeuerWert(Art::Gleit); w->gleit = rr.f64(); return w; }
        if (t == "Guid")    { auto w = NeuerWert(Art::Guid); w->text = rr.guid(); return w; }
        if (t == "Sha1")    { auto w = NeuerWert(Art::Sha1); w->text = rr.hex(20); return w; }
        if (t == "ResourceRef") {
            auto w = NeuerWert(Art::Text);
            char b[24]; std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(rr.u64()));
            w->text = b; return w;
        }
        if (t == "String") {
            auto w = NeuerWert(Art::Text);
            std::string s;
            for (int i = 0; i < 32; ++i) { const uint8_t c = rr.u8(); if (c && s.size() == static_cast<size_t>(i)) s.push_back(static_cast<char>(c)); }
            const size_t nul = s.find('\0');
            if (nul != std::string::npos) s.resize(nul);
            w->text = s; return w;
        }
        if (t == "CString") { auto w = NeuerWert(Art::Text); w->text = TextAn(rr.u32()); return w; }
        if (t == "FileRef") { auto w = NeuerWert(Art::Text); w->text = TextAn(rr.u32()); rr.ueberspringe(4); return w; }
        if (t == "TypeRef" || t == "Delegate") { auto w = NeuerWert(Art::Text); w->text = TextAn(rr.u32()); return w; }
        if (t == "BoxedValueRef") { auto w = NeuerWert(Art::Boxed); w->zahl = rr.i32(); rr.u32(); return w; }
        if (t == "Enum")    { auto w = NeuerWert(Art::Ganz); w->zahl = rr.i32(); return w; }
        if (t == "Struct") {
            const KlassenTyp& st = klassen_[klassenRef < klassen_.size() ? klassenRef : 0];
            rr.aufRunden(st.ausrichtung);
            auto w = NeuerWert(Art::Objekt);
            w->typ = st.name;
            liesKlasse(rr, st, *w);
            return w;
        }
        if (t == "Pointer") {
            const uint32_t idx = rr.u32();
            if (idx == 0) return NeuerWert(Art::Nichts);
            if (idx >> 31) {
                auto w = NeuerWert(Art::Import);
                const size_t k = idx & 0x7FFFFFFFu;
                if (k < importe_.size()) { w->text = importe_[k].first; w->typ = importe_[k].second; }
                return w;
            }
            auto w = NeuerWert(Art::Zeiger);
            w->verweis = static_cast<int64_t>(idx) - 1;
            return w;
        }
        return NeuerWert(Art::Nichts);
    };

    liesKlasse = [&](R& rr, const KlassenTyp& cls, Wert& obj) {
        for (uint8_t j = 0; j < cls.feldZahl; ++j) {
            const size_t fi = static_cast<size_t>(cls.feldIndex) + j;
            if (fi >= felder_.size()) break;
            const FeldTyp& f = felder_[fi];
            const std::string& t = f.typName;
            if (t == "Inherited") {
                if (f.klassenRef < klassen_.size()) liesKlasse(rr, klassen_[f.klassenRef], obj);
                continue;
            }
            if (t == "ResourceRef" || t == "TypeRef" || t == "FileRef" ||
                t == "BoxedValueRef" || t == "UInt64" || t == "Int64" || t == "Float64") {
                rr.aufRunden(8);
            } else if (t == "Array" || t == "Pointer") {
                rr.aufRunden(4);
            }
            if (t == "Array") {
                auto w = NeuerWert(Art::Liste);
                const int32_t idx = rr.i32();
                if (idx >= 0 && static_cast<size_t>(idx) < arrays_.size() &&
                    f.klassenRef < klassen_.size()) {
                    const KlassenTyp& arrCls = klassen_[f.klassenRef];
                    const uint32_t off = std::get<0>(arrays_[static_cast<size_t>(idx)]);
                    const uint32_t anzahl = std::get<1>(arrays_[static_cast<size_t>(idx)]);
                    const size_t merk = rr.p;
                    rr.p = arraysVersatz_ + off;
                    const size_t efi = static_cast<size_t>(arrCls.feldIndex);
                    if (efi < felder_.size()) {
                        const FeldTyp& ef = felder_[efi];
                        for (uint32_t k = 0; k < anzahl && !rr.bad; ++k) {
                            w->liste.push_back(liesFeld(rr, ef.typName, ef.klassenRef));
                        }
                    }
                    rr.p = merk;
                }
                obj.felder.push_back(Feld{ f.name, w });
            } else {
                obj.felder.push_back(Feld{ f.name, liesFeld(rr, t, f.klassenRef) });
            }
        }
        rr.aufRunden(cls.ausrichtung);
    };

    for (const auto& inst : instanzen) {
        const uint16_t cr = std::get<0>(inst);
        const uint16_t anzahl = std::get<1>(inst);
        const bool exportiert = std::get<2>(inst);
        if (cr >= klassen_.size()) continue;
        const KlassenTyp& cls = klassen_[cr];
        for (uint16_t k = 0; k < anzahl && !ro.bad; ++k) {
            ro.aufRunden(cls.ausrichtung);
            const std::string guid = exportiert ? ro.guid() : std::string();
            // Bei einer anderen Ausrichtung als 4 stehen acht Fuellbytes davor.
            if (cls.ausrichtung != 4) ro.ueberspringe(8);
            auto obj = NeuerWert(Art::Objekt);
            obj->typ = cls.name;
            obj->guid = guid;
            obj->zahl = index++;
            liesKlasse(ro, cls, *obj);
            objekte_.push_back(obj);
        }
    }
    return true;
}

// ------------------------------------------------------------
namespace {

void TextRek(const Wert& w, const std::string& name, int tiefe, std::string& aus) {
    char b[512];
    aus.append(static_cast<size_t>(tiefe) * 2, ' ');
    switch (w.art) {
    case Art::Objekt:
        std::snprintf(b, sizeof b, "OBJ %s %s {%zu}", name.c_str(), w.typ.c_str(), w.felder.size());
        aus += b; aus += "\n";
        for (const Feld& f : w.felder) TextRek(*f.wert, f.name, tiefe + 1, aus);
        break;
    case Art::Liste:
        std::snprintf(b, sizeof b, "LISTE %s [%zu]", name.c_str(), w.liste.size());
        aus += b; aus += "\n";
        for (const WertPtr& s : w.liste) TextRek(*s, std::string(), tiefe + 1, aus);
        break;
    case Art::Bool:
        std::snprintf(b, sizeof b, "BOOL %s %d", name.c_str(), w.wahr ? 1 : 0); aus += b; aus += "\n"; break;
    case Art::Ganz:
        std::snprintf(b, sizeof b, "GANZ %s %lld", name.c_str(), static_cast<long long>(w.zahl)); aus += b; aus += "\n"; break;
    case Art::Gleit:
        std::snprintf(b, sizeof b, "GLEIT %s %.6f", name.c_str(), w.gleit); aus += b; aus += "\n"; break;
    case Art::Text:
        aus += "TEXT "; aus += name; aus += " "; aus += w.text; aus += "\n"; break;
    case Art::Guid:
        aus += "GUID "; aus += name; aus += " "; aus += w.text; aus += "\n"; break;
    case Art::Sha1:
        aus += "SHA1 "; aus += name; aus += " "; aus += w.text; aus += "\n"; break;
    case Art::Zeiger:
        std::snprintf(b, sizeof b, "REF %s %lld", name.c_str(), static_cast<long long>(w.verweis)); aus += b; aus += "\n"; break;
    case Art::Import:
        aus += "IMPORT "; aus += name; aus += " "; aus += w.text; aus += " "; aus += w.typ; aus += "\n"; break;
    case Art::Boxed:
        std::snprintf(b, sizeof b, "BOXED %s %lld", name.c_str(), static_cast<long long>(w.zahl)); aus += b; aus += "\n"; break;
    default:
        aus += "NICHTS "; aus += name; aus += "\n"; break;
    }
}

} // namespace

namespace {

Lage LageAus(const Wert& w) {
    Lage l;
    auto hole = [&](const char* name, double* ziel) {
        const Wert* v = w.Feldwert(name);
        if (v == nullptr) return;
        // Ein Vec3 ist ein Struct mit x, y, z (und einem Fuellwert).
        const char* achsen[3] = { "x", "y", "z" };
        for (int i = 0; i < 3; ++i) {
            const Wert* a = v->Feldwert(achsen[i]);
            if (a != nullptr) ziel[i] = (a->art == Art::Gleit) ? a->gleit
                                                              : static_cast<double>(a->zahl);
        }
    };
    hole("right", l.right);
    hole("up", l.up);
    hole("forward", l.forward);
    hole("trans", l.trans);
    return l;
}

void HoleLagen(const Wert& s, const char* feld, std::vector<Lage>& aus) {
    const Wert* v = s.Feldwert(feld);
    if (v == nullptr || v->art != Art::Liste) return;
    for (const WertPtr& p : v->liste) if (p) aus.push_back(LageAus(*p));
}

} // namespace

std::vector<BasisPose> LiesBasisPosen(const Datei& e) {
    std::vector<BasisPose> aus;
    for (const WertPtr& o : e.Objekte()) {
        if (!o) continue;
        const Wert* bp = o->Feldwert("BasePoseTransforms");
        if (bp == nullptr) continue;
        BasisPose p;
        p.objekt = o->typ;
        const Wert* ind = bp->Feldwert("Indices");
        const Wert* tr = bp->Feldwert("Transforms");
        if (const Wert* c = bp->Feldwert("Count")) p.count = c->zahl;
        if (ind != nullptr && tr != nullptr && ind->art == Art::Liste && tr->art == Art::Liste) {
            const size_t n = std::min(ind->liste.size(), tr->liste.size());
            for (size_t i = 0; i < n; ++i) {
                if (!ind->liste[i] || !tr->liste[i]) continue;
                p.eintraege.emplace_back(static_cast<int32_t>(ind->liste[i]->zahl), LageAus(*tr->liste[i]));
            }
        }
        aus.push_back(std::move(p));
    }
    return aus;
}

Skelett LiesSkelett(const Datei& e) {
    Skelett sk;
    const Wert* s = nullptr;
    for (const WertPtr& o : e.Objekte()) {
        if (o && o->typ == "SkeletonAsset") { s = o.get(); break; }
    }
    if (s == nullptr) {
        // Ersatzweise: das Objekt mit den MEISTEN BoneNames (0.92.0 - frueher
        // das erste; Fahrzeug-EBX enthalten oft mehrere Objekte, davon eins
        // mit einer kurzen Teilliste).
        size_t beste = 0;
        for (const WertPtr& o : e.Objekte()) {
            if (!o) continue;
            const Wert* bn = o->Feldwert("BoneNames");
            if (bn == nullptr || bn->liste.size() <= beste) continue;
            beste = bn->liste.size();
            s = o.get();
        }
    }
    if (s == nullptr) return sk;
    sk.gefunden = true;
    if (const Wert* n = s->Feldwert("Name")) sk.name = n->text;
    if (const Wert* n = s->Feldwert("BoneNames")) {
        for (const WertPtr& p : n->liste) if (p) sk.namen.push_back(p->text);
    }
    if (const Wert* h = s->Feldwert("Hierarchy")) {
        for (const WertPtr& p : h->liste) if (p) sk.hierarchie.push_back(static_cast<int32_t>(p->zahl));
    }
    HoleLagen(*s, "LocalPose", sk.lokal);
    HoleLagen(*s, "ModelPose", sk.modell);
    HoleLagen(*s, "InverseModelPose", sk.modellInvers);
    return sk;
}

std::string AlsText(const Datei& e, size_t grenze) {
    std::string aus;
    char b[128];
    std::snprintf(b, sizeof b, "EBX %s FASSUNG %d OBJEKTE %zu\n",
                  e.DateiGuid().c_str(), e.Fassung(), e.Objekte().size());
    aus += b;
    size_t i = 0;
    for (const WertPtr& o : e.Objekte()) {
        if (grenze && i >= grenze) break;
        std::snprintf(b, sizeof b, "-- %zu %s %s\n", i, o->typ.c_str(),
                      o->guid.empty() ? "-" : o->guid.c_str());
        aus += b;
        for (const Feld& f : o->felder) TextRek(*f.wert, f.name, 1, aus);
        ++i;
    }
    return aus;
}

} // namespace fbebx
