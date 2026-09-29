// ============================================================
//  fbgdwerte.h - die WERTE in den Datensaetzen einer GD-Bank.
//
//  Portiert aus fb_gd.py (lies_datensatz, lies_basis, _string_feld) -
//  eigener Code aus fbtools. Die Regeln, alle an echten Baenken gemessen:
//   * Array-Deskriptor: u32 Anzahl, u32 Kapazitaet, i64 Versatz; der
//     Versatz zaehlt RELATIV ZU SEINER EIGENEN POSITION + 8, nur die
//     unteren 56 Bit gelten.
//   * Zeichenkette: derselbe Deskriptor (Laenge, Kapazitaet, Versatz),
//     Laenge 1..4095, bis zum ersten Nullbyte, Latin-1.
//   * __base zeigt (ebenso relativ) auf einen eingebetteten
//     AnimationAsset-Satz: +16 Klassenhash, +32 Datensatz.
//   * Welche Felder gelesen werden, entscheidet der TYPNAME: die neun
//     Grundtypen, Key/DataRef (8 Byte hex), String, Arrays. Andere
//     Felder (Unterstrukturen ohne Array) werden - wie in fbtools -
//     uebergangen.
//
//  WerteAlsText gibt eine exakte Vergleichsform aus (Gleitzahlen als
//  Bitmuster, Arrays mit FNV-1a-Pruefsumme ueber ALLE Bytes), die
//  tools/VERGLEICHE_GD.py aus fbtools genauso erzeugt.
// ============================================================
#pragma once

#include "fbgd.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fbgd {

struct Wert {
    enum class Art { Nichts, Ganz, Gleit, Bool, Hex, Text, Feld };
    Art art = Art::Nichts;
    int64_t ganz = 0;
    float gleit = 0.0f;
    std::string text;                 // Hex oder Text
    bool textDa = false;              // Zeichenkette gelesen (sonst None)
    // Array
    uint32_t anzahl = 0, kapazitaet = 0;
    uint64_t start = 0;
    std::string typ;
    bool werteDa = false;
    std::vector<Wert> werte;                                     // Grundtypen/Keys
    std::vector<std::vector<std::pair<std::string, Wert>>> saetze; // Unterstrukturen
};

using Felder = std::vector<std::pair<std::string, Wert>>;

struct Datensatz {
    Felder felder;
    bool nameDa = false;
    std::string name;                 // __name
    bool basisDa = false;
    std::string basisKlasse;
    Felder basis;
};

// hoechstens: wie viele Array-Elemente mitgelesen werden (0 = alle).
bool LiesDatensatz(const std::vector<uint8_t>& d, const Bank& b, const Eintrag& e,
                   Datensatz& aus, size_t hoechstens);

// Codec aus CodecType (Vierzeichenkuerzel, 0x52415720 = "RAW ").
std::string Codec(int64_t codecType);

// Die exakte Vergleichsform (siehe oben).
std::string WerteAlsText(const std::vector<uint8_t>& d, const Bank& b);

// Ein Feld nach Namen (nullptr, wenn es fehlt).
const Wert* Feld(const Felder& f, const std::string& name);

} // namespace fbgd
