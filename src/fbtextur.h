// ============================================================
//  fbtextur.h - Texturen: Kopf lesen, Mip 0 dekodieren, PNG schreiben.
//
//  Kopf nach Frostys Texture.cs (SWBF2-Zweig, 132 Byte). Gemessen an
//  allen 21 Texturen von Anakin: Summe der Mip-Groessen = ChunkSize,
//  Mip 0 = Breite x Hoehe x Blockgroesse, und der geholte Chunk
//  enthaelt ALLE Ebenen (die Groesse im Bundle zaehlt nur die ab
//  ersterMip). Mip 0 steht also am Anfang des Chunks.
//
//  Formate (fbtools, gemessen ueber die Glattheit):
//      54 BC1_UNORM  63 BC5_UNORM  66 BC7_UNORM  67 BC7_UNORM_SRGB
//  Andere Formatnummern werden gemeldet, nicht geraten.
//
//  Dekodiert wird mit bcdec (MIT/Unlicense, vendor/bcdec).
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fbtextur {

struct Kopf {
    uint32_t typ = 0;
    int32_t format = 0;
    uint32_t unbekannt1 = 0;
    uint16_t flags = 0, breite = 0, hoehe = 0, tiefe = 0, scheiben = 0;
    uint8_t mips = 0, ersterMip = 0;
    uint8_t chunk[16] = {};
    uint32_t mipGroesse[15] = {};
    uint32_t chunkGroesse = 0;
    uint32_t namensHash = 0;
    std::string gruppe;
};

bool LiesKopf(const std::vector<uint8_t>& res, Kopf& k, std::string& fehler);
std::string ChunkGuid(const Kopf& k);          // wie im Index: 8-4-4-4-12
const char* FormatName(int32_t format);
bool Bekannt(int32_t format);

// Mip 0 als RGBA8 (Zeile 0 = oben). BC5 liefert R, G und B = 0, A = 255.
bool DekodiereMip0(const Kopf& k, const std::vector<uint8_t>& chunk,
                   std::vector<uint8_t>& rgba, std::string& fehler);

// Tangentenraum-Normale aus R und G: B = sqrt(1 - x^2 - y^2), A = 255.
// Misst dabei, bei wie vielen Pixeln das alte B schon zu Z passte (+-0,1).
void BaueNormale(std::vector<uint8_t>& rgba, double& anteilBpasstZuZ);

// PNG mit 3 (RGB) oder 4 (RGBA) Kanaelen schreiben (miniz, Stufe 1).
bool SchreibePng(const std::string& utf8Pfad, const std::vector<uint8_t>& rgba,
                 uint32_t breite, uint32_t hoehe, int kanaele, std::string& fehler);

// Mittelwerte je Kanal (0..255) - fuers Protokoll.
void Mittel(const std::vector<uint8_t>& rgba, double aus[4]);

} // namespace fbtextur
