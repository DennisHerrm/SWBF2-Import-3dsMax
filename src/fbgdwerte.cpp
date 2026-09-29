// ============================================================
//  fbgdwerte.cpp - siehe fbgdwerte.h
// ============================================================
#include "fbgdwerte.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

namespace fbgd {

namespace {

uint32_t L32(const std::vector<uint8_t>& d, size_t p, bool be) {
    if (p + 4 > d.size()) return 0;
    if (be) return (static_cast<uint32_t>(d[p]) << 24) | (static_cast<uint32_t>(d[p + 1]) << 16) |
                  (static_cast<uint32_t>(d[p + 2]) << 8) | d[p + 3];
    return static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p + 1]) << 8) |
           (static_cast<uint32_t>(d[p + 2]) << 16) | (static_cast<uint32_t>(d[p + 3]) << 24);
}

uint64_t L64(const std::vector<uint8_t>& d, size_t p, bool be) {
    if (p + 8 > d.size()) return 0;
    uint64_t v = 0;
    if (be) { for (int i = 0; i < 8; ++i) v = (v << 8) | d[p + i]; }
    else    { for (int i = 7; i >= 0; --i) v = (v << 8) | d[p + i]; }
    return v;
}

uint64_t Off(uint64_t v) { return v & 0x00FFFFFFFFFFFFFFULL; }

// Groesse der neun Grundtypen (fb_gd.ELEMENT); 0 = kein Grundtyp.
int Groesse(const std::string& t) {
    if (t == "Bool" || t == "Int8" || t == "UInt8") return 1;
    if (t == "Int16" || t == "UInt16") return 2;
    if (t == "Int32" || t == "UInt32" || t == "Float") return 4;
    if (t == "Int64") return 8;
    return 0;
}

Wert LiesGrundtyp(const std::vector<uint8_t>& d, size_t p, const std::string& t, bool be, bool boolAlsBool) {
    Wert w;
    const int g = Groesse(t);
    if (g == 0 || p + static_cast<size_t>(g) > d.size()) return w;
    if (t == "Float") {
        const uint32_t bits = L32(d, p, be);
        std::memcpy(&w.gleit, &bits, 4);
        w.art = Wert::Art::Gleit;
        return w;
    }
    uint64_t roh = 0;
    if (g == 1) roh = d[p];
    else if (g == 2) roh = be ? ((static_cast<uint64_t>(d[p]) << 8) | d[p + 1]) : (d[p] | (static_cast<uint64_t>(d[p + 1]) << 8));
    else if (g == 4) roh = L32(d, p, be);
    else roh = L64(d, p, be);
    int64_t v = 0;
    if (t == "Int8") v = static_cast<int8_t>(roh);
    else if (t == "Int16") v = static_cast<int16_t>(roh);
    else if (t == "Int32") v = static_cast<int32_t>(roh);
    else if (t == "Int64") v = static_cast<int64_t>(roh);
    else v = static_cast<int64_t>(roh);          // Bool, UInt8, UInt16, UInt32
    w.ganz = v;
    w.art = (t == "Bool" && boolAlsBool) ? Wert::Art::Bool : Wert::Art::Ganz;
    return w;
}

std::string Hex(const std::vector<uint8_t>& d, size_t p, size_t n) {
    static const char* z = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n && p + i < d.size(); ++i) { s += z[d[p + i] >> 4]; s += z[d[p + i] & 15]; }
    return s;
}

Wert Zeichenkette(const std::vector<uint8_t>& d, size_t p, bool be) {
    Wert w;
    w.art = Wert::Art::Text;
    if (p + 16 > d.size()) return w;
    const uint32_t laenge = L32(d, p, be);
    const uint64_t start = static_cast<uint64_t>(p) + 8 + Off(L64(d, p + 8, be));
    if (laenge > 0 && laenge < 4096 && start < d.size()) {
        size_t ende = std::min<size_t>(d.size(), static_cast<size_t>(start) + laenge);
        std::string s(d.begin() + static_cast<long>(start), d.begin() + static_cast<long>(ende));
        const size_t null = s.find('\0');
        if (null != std::string::npos) s.resize(null);
        w.text = s;
        w.textDa = true;
    }
    return w;
}

const GdKlasse* KlasseNachName(const Bank& b, const std::string& name) {
    for (const auto& kv : b.klassen) if (kv.second.name == name) return &kv.second;
    return nullptr;
}

} // namespace

const Wert* Feld(const Felder& f, const std::string& name) {
    for (const auto& p : f) if (p.first == name) return &p.second;
    return nullptr;
}

std::string Codec(int64_t v) {
    const uint32_t u = static_cast<uint32_t>(v);
    char c[5] = { static_cast<char>(u >> 24), static_cast<char>(u >> 16), static_cast<char>(u >> 8), static_cast<char>(u), 0 };
    for (int i = 0; i < 4; ++i) {
        if (static_cast<unsigned char>(c[i]) < 32 || static_cast<unsigned char>(c[i]) >= 127) {
            char b[16];
            std::snprintf(b, sizeof b, "0x%08X", u);
            return b;
        }
    }
    return c;
}

bool LiesDatensatz(const std::vector<uint8_t>& d, const Bank& b, const Eintrag& e, Datensatz& aus, size_t hoechstens) {
    aus = Datensatz();
    const auto ki = b.klassen.find(e.klassenHash);
    if (ki == b.klassen.end()) return false;
    const GdKlasse& k = ki->second;
    const bool be = !e.kleinEndig;
    const size_t r = e.datensatz;
    for (const GdFeld& f : k.felder) {
        const size_t p = r + f.versatz;
        const std::string& t = f.typName;
        if (f.istArray) {
            Wert w;
            w.art = Wert::Art::Feld;
            w.typ = t;
            w.anzahl = L32(d, p, be);
            w.kapazitaet = L32(d, p + 4, be);
            w.start = static_cast<uint64_t>(p) + 8 + Off(L64(d, p + 8, be));
            const size_t n = hoechstens ? std::min<size_t>(w.anzahl, hoechstens) : w.anzahl;
            if (t == "Key") {
                if (w.start + static_cast<uint64_t>(w.anzahl) * 8 <= d.size()) {
                    w.werteDa = true;
                    for (size_t i = 0; i < n; ++i) {
                        Wert x; x.art = Wert::Art::Hex; x.text = Hex(d, static_cast<size_t>(w.start) + 8 * i, 8);
                        w.werte.push_back(x);
                    }
                }
            } else if (Groesse(t) > 0) {
                const int g = Groesse(t);
                if (w.start + static_cast<uint64_t>(w.anzahl) * static_cast<uint64_t>(g) <= d.size()) {
                    w.werteDa = true;
                    for (size_t i = 0; i < n; ++i) {
                        w.werte.push_back(LiesGrundtyp(d, static_cast<size_t>(w.start) + i * static_cast<size_t>(g), t, be, false));
                    }
                }
            } else if (w.anzahl) {
                const GdKlasse* u = t.empty() ? nullptr : KlasseNachName(b, t);
                if (u != nullptr && !u->felder.empty()) {
                    uint32_t schritt = 0;
                    for (const GdFeld& uf : u->felder) schritt = std::max(schritt, uf.versatz + uf.elementGroesse);
                    schritt = (schritt + 7) / 8 * 8;
                    w.werteDa = true;
                    for (size_t i = 0; i < n; ++i) {
                        const uint64_t q = w.start + static_cast<uint64_t>(i) * schritt;
                        if (q + schritt > d.size()) break;
                        Felder satz;
                        for (const GdFeld& uf : u->felder) {
                            if (uf.istArray) continue;
                            const size_t pp = static_cast<size_t>(q) + uf.versatz;
                            if (Groesse(uf.typName) > 0) satz.emplace_back(uf.name, LiesGrundtyp(d, pp, uf.typName, be, false));
                            else if (uf.typName == "Key") { Wert x; x.art = Wert::Art::Hex; x.text = Hex(d, pp, 8); satz.emplace_back(uf.name, x); }
                            else if (uf.typName == "String") satz.emplace_back(uf.name, Zeichenkette(d, pp, be));
                        }
                        w.saetze.push_back(std::move(satz));
                    }
                }
            }
            aus.felder.emplace_back(f.name, std::move(w));
        } else if (Groesse(t) > 0) {
            aus.felder.emplace_back(f.name, LiesGrundtyp(d, p, t, be, true));
        } else if (t == "Key" || t == "DataRef" || f.name == "__key") {
            Wert x; x.art = Wert::Art::Hex; x.text = Hex(d, p, 8);
            aus.felder.emplace_back(f.name, x);
        } else if (t == "String") {
            aus.felder.emplace_back(f.name, Zeichenkette(d, p, be));
        }
    }
    // __name
    for (const GdFeld& f : k.felder) {
        if (f.name != "__name") continue;
        const Wert w = Zeichenkette(d, r + f.versatz, be);
        aus.nameDa = w.textDa;
        aus.name = w.text;
        break;
    }
    // __base
    for (const GdFeld& f : k.felder) {
        if (f.name != "__base") continue;
        const size_t p = r + f.versatz;
        const uint64_t ziel = static_cast<uint64_t>(p) + Off(L64(d, p, be));
        if (!(ziel + 32 < d.size())) break;
        const auto bk = b.klassen.find(L32(d, static_cast<size_t>(ziel) + 16, be));
        if (bk == b.klassen.end()) break;
        aus.basisDa = true;
        aus.basisKlasse = bk->second.name;
        const size_t rb = static_cast<size_t>(ziel) + 32;
        for (const GdFeld& bf : bk->second.felder) {
            const size_t q = rb + bf.versatz;
            if (Groesse(bf.typName) > 0 && !bf.istArray) aus.basis.emplace_back(bf.name, LiesGrundtyp(d, q, bf.typName, be, false));
            else if (bf.name == "__name") aus.basis.emplace_back(bf.name, Zeichenkette(d, q, be));
            else { Wert x; x.art = Wert::Art::Hex; x.text = Hex(d, q, 8); aus.basis.emplace_back(bf.name, x); }
        }
        break;
    }
    return true;
}

namespace {

std::string TextWert(const std::string& s) {
    bool druckbar = true;
    for (unsigned char c : s) if (c < 32 || c >= 127) { druckbar = false; break; }
    if (druckbar) return s;
    static const char* z = "0123456789abcdef";
    std::string h = "x:";
    for (unsigned char c : s) { h += z[c >> 4]; h += z[c & 15]; }
    return h;
}

std::string Kanon(const Wert& w) {
    char b[64];
    switch (w.art) {
    case Wert::Art::Ganz: return "i:" + std::to_string(w.ganz);
    case Wert::Art::Bool: return std::string("b:") + (w.ganz ? "1" : "0");
    case Wert::Art::Gleit: { uint32_t bits; std::memcpy(&bits, &w.gleit, 4); std::snprintf(b, sizeof b, "f:%08x", bits); return b; }
    case Wert::Art::Hex: return "h:" + w.text;
    case Wert::Art::Text: return w.textDa ? "s:" + TextWert(w.text) : std::string("n:");
    default: return "n:";
    }
}

uint64_t Fnv(const std::vector<uint8_t>& d, uint64_t start, uint64_t laenge) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint64_t i = 0; i < laenge; ++i) { h ^= d[static_cast<size_t>(start + i)]; h *= 0x100000001b3ULL; }
    return h;
}

void FelderText(const std::vector<uint8_t>& d, const Bank& b, const Felder& f, const char* kennung, std::string& aus) {
    char buf[160];
    for (const auto& p : f) {
        const Wert& w = p.second;
        if (w.art != Wert::Art::Feld) { aus += std::string(" ") + kennung + " " + p.first + " " + Kanon(w) + "\n"; continue; }
        // Elementgroesse fuer die Pruefsumme: Grundtyp, Key (8) oder Schritt der Unterstruktur.
        uint64_t groesse = 0;
        if (w.typ == "Key") groesse = 8;
        else if (Groesse(w.typ) > 0) groesse = static_cast<uint64_t>(Groesse(w.typ));
        else {
            const GdKlasse* u = w.typ.empty() ? nullptr : KlasseNachName(b, w.typ);
            if (u != nullptr && !u->felder.empty()) {
                uint32_t s = 0;
                for (const GdFeld& uf : u->felder) s = std::max(s, uf.versatz + uf.elementGroesse);
                groesse = (s + 7) / 8 * 8;
            }
        }
        std::string summe = "-";
        if (groesse && w.start + static_cast<uint64_t>(w.anzahl) * groesse <= d.size()) {
            std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(Fnv(d, w.start, static_cast<uint64_t>(w.anzahl) * groesse)));
            summe = buf;
        }
        std::snprintf(buf, sizeof buf, " A %s typ=%s n=%u kap=%u start=%llu sum=%s", p.first.c_str(),
                      w.typ.empty() ? "-" : w.typ.c_str(), w.anzahl, w.kapazitaet, static_cast<unsigned long long>(w.start), summe.c_str());
        aus += buf;
        if (!w.werteDa) { aus += " werte=-\n"; continue; }
        for (const Wert& x : w.werte) aus += " " + Kanon(x);
        for (const auto& satz : w.saetze) {
            aus += " {";
            bool erst = true;
            for (const auto& sp : satz) { if (!erst) aus += ","; erst = false; aus += sp.first + "=" + Kanon(sp.second); }
            aus += "}";
        }
        aus += "\n";
    }
}

} // namespace

std::string WerteAlsText(const std::vector<uint8_t>& d, const Bank& b) {
    std::string aus;
    for (size_t i = 0; i < b.eintraege.size(); ++i) {
        const Eintrag& e = b.eintraege[i];
        Datensatz s;
        const bool ok = LiesDatensatz(d, b, e, s, 4);
        aus += "E " + std::to_string(i) + " klasse=" + (e.klassenName.empty() ? std::string("-") : e.klassenName) +
               " name=" + (s.nameDa ? TextWert(s.name) : std::string("-")) + "\n";
        if (!ok) continue;
        FelderText(d, b, s.felder, "F", aus);
        if (s.basisDa) {
            aus += " B __klasse " + s.basisKlasse + "\n";
            FelderText(d, b, s.basis, "B", aus);
        }
    }
    return aus;
}

} // namespace fbgd
