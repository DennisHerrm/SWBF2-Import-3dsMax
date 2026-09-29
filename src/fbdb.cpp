// ============================================================
//  fbdb.cpp - DbObject lesen.
// ============================================================
#include "fbdb.h"
#include "fbcas.h"

#include <cstdio>
#include <cstring>

namespace fbdb {
namespace {

struct Leser {
    const uint8_t* d;
    size_t n;
    size_t p = 0;
    bool bad = false;

    Leser(const std::vector<uint8_t>& v, size_t start) : d(v.data()), n(v.size()), p(start) {}

    bool brauche(size_t k) {
        if (bad || p + k > n) { bad = true; return false; }
        return true;
    }
    uint8_t u8() { return brauche(1) ? d[p++] : 0; }
    int32_t i32() {
        if (!brauche(4)) return 0;
        const int32_t v = static_cast<int32_t>(
            static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p+1]) << 8) |
            (static_cast<uint32_t>(d[p+2]) << 16) | (static_cast<uint32_t>(d[p+3]) << 24));
        p += 4; return v;
    }
    int64_t i64() {
        if (!brauche(8)) return 0;
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | d[p + i];
        p += 8; return static_cast<int64_t>(v);
    }
    float f32() { const int32_t v = i32(); float f; std::memcpy(&f, &v, 4); return f; }
    double f64() { const int64_t v = i64(); double f; std::memcpy(&f, &v, 8); return f; }

    // Read7BitEncodedInt: LEB128, sieben Datenbits je Byte.
    uint64_t v7() {
        uint64_t ergebnis = 0;
        int schub = 0;
        for (;;) {
            const uint8_t b = u8();
            ergebnis |= static_cast<uint64_t>(b & 0x7F) << schub;
            if (!(b & 0x80)) return ergebnis;
            schub += 7;
            if (schub > 63 || bad) { bad = true; return ergebnis; }
        }
    }
    std::string cstr() {
        std::string s;
        for (;;) {
            const uint8_t c = u8();
            if (bad || c == 0) break;
            s.push_back(static_cast<char>(c));
        }
        return s;
    }
    std::string text(size_t k) {
        if (!brauche(k)) return std::string();
        std::string s(reinterpret_cast<const char*>(d + p), k);
        p += k;
        // Die Laenge schliesst das abschliessende Nullbyte mit ein.
        while (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    }
    std::vector<uint8_t> roh(size_t k) {
        std::vector<uint8_t> v;
        if (!brauche(k)) return v;
        v.assign(d + p, d + p + k);
        p += k;
        return v;
    }
};

bool Lies(Leser& r, std::string& name, WertPtr& aus, std::string& fehler);

bool LiesBereich(Leser& r, uint64_t laenge, bool alsObjekt, Wert& ziel, std::string& fehler) {
    const size_t start = r.p;
    while (!r.bad && r.p - start < laenge) {
        std::string n;
        WertPtr sub;
        if (!Lies(r, n, sub, fehler)) return false;
        if (!sub) break;                       // Ende-Marke
        if (alsObjekt) ziel.felder.push_back(Feld{ n, sub });
        else ziel.liste.push_back(sub);
    }
    if (start + laenge > r.n) { r.bad = true; fehler = "DbObject: Bereich reicht ueber das Ende"; return false; }
    r.p = start + laenge;
    return true;
}

bool Lies(Leser& r, std::string& name, WertPtr& aus, std::string& fehler) {
    const uint8_t tmp = r.u8();
    if (r.bad) { fehler = "DbObject: Datei endet mitten im Baum"; return false; }
    const int t = tmp & 0x1F;
    if (t == 0) { aus.reset(); name.clear(); return true; }
    name = (tmp & 0x80) ? std::string() : r.cstr();

    auto w = std::make_shared<Wert>();
    switch (t) {
    case 1: w->art = Art::Liste;  if (!LiesBereich(r, r.v7(), false, *w, fehler)) return false; break;
    case 2: w->art = Art::Objekt; if (!LiesBereich(r, r.v7(), true,  *w, fehler)) return false; break;
    case 6:  w->art = Art::Bool;   w->wahr = (r.u8() == 1); break;
    case 7:  w->art = Art::Text;   w->text = r.text(static_cast<size_t>(r.v7())); break;
    case 8:  w->art = Art::Ganz;   w->zahl = r.i32(); break;
    case 9:  w->art = Art::Lang;   w->zahl = r.i64(); break;
    case 11: w->art = Art::Gleit;  w->gleit = r.f32(); break;
    case 12: w->art = Art::Doppel; w->gleit = r.f64(); break;
    case 15: w->art = Art::Guid;   w->bytes = r.roh(16); break;
    case 16: w->art = Art::Sha1;   w->bytes = r.roh(20); break;
    case 19: w->art = Art::Bytes;  w->bytes = r.roh(static_cast<size_t>(r.v7())); break;
    default: {
        char b[96];
        std::snprintf(b, sizeof b, "unbekannter DbType %d bei %zu", t, r.p - 1);
        fehler = b;
        return false;
    }
    }
    if (r.bad) { fehler = "DbObject: Datei endet mitten in einem Wert"; return false; }
    aus = w;
    return true;
}

void Text(const Wert& w, const std::string& name, int tiefe, int nachkomma, std::string& aus);

void Einruecken(std::string& aus, int tiefe) { aus.append(static_cast<size_t>(tiefe) * 2, ' '); }

std::string Hex(const std::vector<uint8_t>& b) {
    static const char* z = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (uint8_t x : b) { s.push_back(z[(x >> 4) & 0xF]); s.push_back(z[x & 0xF]); }
    return s;
}

void Text(const Wert& w, const std::string& name, int tiefe, int nachkomma, std::string& aus) {
    // Zahlen duerfen in einen festen Puffer, Zeichenketten und Bytes NICHT:
    // ein Woerterbuch von 4 KB ergibt 8.192 Hexzeichen, und snprintf haette
    // bei 512 Byte abgeschnitten - genau daran ist der erste Vergleich
    // gescheitert (492 statt 8.192 Zeichen).
    char b[128];
    Einruecken(aus, tiefe);
    switch (w.art) {
    case Art::Liste:
        aus += "LISTE "; aus += name;
        std::snprintf(b, sizeof b, " [%zu]", w.liste.size());
        aus += b; aus += "\n";
        for (const WertPtr& s : w.liste) Text(*s, std::string(), tiefe + 1, nachkomma, aus);
        break;
    case Art::Objekt:
        aus += "OBJEKT "; aus += name;
        std::snprintf(b, sizeof b, " {%zu}", w.felder.size());
        aus += b; aus += "\n";
        for (const Feld& f : w.felder) Text(*f.wert, f.name, tiefe + 1, nachkomma, aus);
        break;
    case Art::Bool:
        aus += "BOOL "; aus += name; aus += w.wahr ? " 1" : " 0"; aus += "\n"; break;
    case Art::Text:
        aus += "TEXT "; aus += name; aus += " "; aus += w.text; aus += "\n"; break;
    case Art::Ganz:
        std::snprintf(b, sizeof b, "GANZ %s %lld", name.c_str(), static_cast<long long>(w.zahl)); aus += b; aus += "\n"; break;
    case Art::Lang:
        std::snprintf(b, sizeof b, "LANG %s %lld", name.c_str(), static_cast<long long>(w.zahl)); aus += b; aus += "\n"; break;
    case Art::Gleit:
        aus += "GLEIT "; aus += name; std::snprintf(b, sizeof b, " %.*f", nachkomma, w.gleit); aus += b; aus += "\n"; break;
    case Art::Doppel:
        aus += "DOPPEL "; aus += name; std::snprintf(b, sizeof b, " %.*f", nachkomma, w.gleit); aus += b; aus += "\n"; break;
    case Art::Guid:
        aus += "GUID "; aus += name; aus += " "; aus += Hex(w.bytes); aus += "\n"; break;
    case Art::Sha1:
        aus += "SHA1 "; aus += name; aus += " "; aus += Hex(w.bytes); aus += "\n"; break;
    case Art::Bytes:
        aus += "BYTES "; aus += name; aus += " ";
        std::snprintf(b, sizeof b, "%zu ", w.bytes.size());
        aus += b; aus += Hex(w.bytes); aus += "\n"; break;
    default:
        aus += "ENDE\n"; break;
    }
}

} // namespace

const Wert* Wert::Feldwert(const std::string& name) const {
    for (const Feld& f : felder) if (f.name == name) return f.wert.get();
    for (const Feld& f : felder) {
        if (f.name.size() != name.size()) continue;
        bool gleich = true;
        for (size_t i = 0; i < name.size(); ++i) {
            char a = f.name[i], b = name[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) { gleich = false; break; }
        }
        if (gleich) return f.wert.get();
    }
    return nullptr;
}

bool LiesDbObjekt(const std::vector<uint8_t>& daten, WertPtr& aus, std::string& fehler) {
    Leser r(daten, fbcas::DeobfStart(daten));
    std::string name;
    return Lies(r, name, aus, fehler);
}

std::string AlsText(const Wert& w, int nachkomma) {
    std::string aus;
    Text(w, std::string(), 0, nachkomma, aus);
    return aus;
}

} // namespace fbdb
