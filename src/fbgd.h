// ============================================================
//  fbgd.h - GD-Baenke lesen (EAs GenericData).
//
//  Darin stecken die Animationen: je Bank eine Typbeschreibung
//  (REF2) und dahinter je Eintrag ein Datenblock (DAT2).
//
//  Portiert aus fb_gd.py. Die Zahlen sind an echten Baenken
//  nachgemessen, nicht geraten:
//
//   * Kopf 36 Byte, GROSS-ENDIG. Bei dataOffset+4 muss "GD."
//     stehen, sonst ist es keine Bank.
//   * GD.STRM umspannt alles Folgende; die uebrigen Bloecke
//     liegen DARIN, der erste direkt hinter dem 16-Byte-Kopf.
//   * Klassenkopf 32 Byte, Feldeintrag 32 Byte, Feldzahl =
//     (stringTableOffset - 32) / 32. Die naechste Klasse beginnt
//     bei stringTableOffset + stringTableLength, aufgerundet
//     auf acht.
//   * Die Offsetliste VOR den Klassen taugt nichts - sie zaehlt
//     in Schritten von 32, die Klassen liegen 40 und mehr
//     auseinander. Gelesen wird der Reihe nach.
//   * DAT2: p+8 Blockgroesse, p+28 Klassenhash, p+44 Beginn des
//     Datensatzes; die Feldversaetze zaehlen ab dort.
// ============================================================
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fbgd {

struct Block {
    std::string marke;
    size_t pos = 0;
    uint32_t groesse = 0;
    bool kleinEndig = true;
    uint32_t wort0 = 0, wort1 = 0;
};

struct GdFeld {
    std::string name;
    std::string typName;         // aufgeloest ueber den Typhash
    uint32_t typHash = 0;
    uint32_t versatz = 0;
    uint32_t elementGroesse = 0;
    uint16_t anzahl = 0;
    uint16_t flags = 0;
    bool istArray = false;
    uint16_t elementAusrichtung = 0;
    int16_t rle = 0;
    int64_t layout = 0;
};

struct GdKlasse {
    std::string name;
    uint32_t hash = 0;
    uint32_t groesse = 0;
    uint8_t nativ = 0;
    std::vector<GdFeld> felder;
};

struct Eintrag {
    size_t pos = 0;
    uint32_t groesse = 0;
    uint32_t typHash = 0;
    uint32_t klassenHash = 0;
    std::string klassenName;
    size_t datensatz = 0;
    bool kleinEndig = true;
};

struct Bank {
    uint32_t paketTyp = 0, datenVersatz = 0, reflTyp = 0;
    uint32_t unterZahl = 0, unterKapazitaet = 0;
    std::vector<std::string> ids;
    std::vector<Block> bloecke;
    std::map<uint32_t, GdKlasse> klassen;
    std::vector<Eintrag> eintraege;
    std::vector<std::string> typNamen;
    std::vector<std::string> warnungen;
};

bool LiesBank(const std::vector<uint8_t>& daten, Bank& aus, std::string& fehler);

// Nur die unteren 56 Bit sind der Versatz; das obere Byte ist eine Marke.
inline int64_t Versatz(int64_t v) { return v & 0x00FFFFFFFFFFFFFFLL; }

std::string AlsText(const Bank& b);

} // namespace fbgd
