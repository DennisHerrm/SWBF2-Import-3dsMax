// ============================================================
//  fbtextur.cpp - siehe fbtextur.h
// ============================================================
#include "fbtextur.h"
#include "fbdatei.h"

#define BCDEC_IMPLEMENTATION
#include "bcdec.h"
#include "miniz.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace fbtextur {

bool LiesKopf(const std::vector<uint8_t>& d, Kopf& k, std::string& fehler) {
    if (d.size() < 132) { fehler = "Texturkopf zu kurz (" + std::to_string(d.size()) + " Byte, noetig 132)"; return false; }
    size_t p = 0;
    auto u32 = [&]() { const uint32_t v = static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p + 1]) << 8) |
                                          (static_cast<uint32_t>(d[p + 2]) << 16) | (static_cast<uint32_t>(d[p + 3]) << 24);
                       p += 4; return v; };
    auto u16 = [&]() { const uint16_t v = static_cast<uint16_t>(d[p] | (d[p + 1] << 8)); p += 2; return v; };
    u32();                       // mipOffsets[0]
    u32();                       // mipOffsets[1]
    k.typ = u32();
    k.format = static_cast<int32_t>(u32());
    k.unbekannt1 = u32();
    k.flags = u16();
    k.breite = u16();
    k.hoehe = u16();
    k.tiefe = u16();
    k.scheiben = u16();
    k.mips = d[p];
    k.ersterMip = d[p + 1];
    p += 2;
    std::memcpy(k.chunk, &d[p], 16);
    p += 16;
    for (int i = 0; i < 15; ++i) k.mipGroesse[i] = u32();
    k.chunkGroesse = u32();
    k.namensHash = u32();
    k.gruppe.assign(reinterpret_cast<const char*>(&d[p]), 16);
    k.gruppe = k.gruppe.substr(0, k.gruppe.find('\0'));
    return true;
}

std::string ChunkGuid(const Kopf& k) {
    const uint8_t* g = k.chunk;
    char b[40];
    std::snprintf(b, sizeof b,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
        g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return b;
}

const char* FormatName(int32_t f) {
    switch (f) {
    case 54: return "BC1_UNORM";
    case 63: return "BC5_UNORM";
    case 66: return "BC7_UNORM";
    case 67: return "BC7_UNORM_SRGB";
    default: return "unbekannt";
    }
}

bool Bekannt(int32_t f) { return f == 54 || f == 63 || f == 66 || f == 67; }

bool DekodiereMip0(const Kopf& k, const std::vector<uint8_t>& chunk,
                   std::vector<uint8_t>& rgba, std::string& fehler) {
    if (!Bekannt(k.format)) { fehler = "Format " + std::to_string(k.format) + " nicht bekannt"; return false; }
    const uint32_t w = k.breite, h = k.hoehe;
    if (w == 0 || h == 0 || w > 16384 || h > 16384) { fehler = "Groesse unplausibel"; return false; }
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    const uint32_t bb = (k.format == 54) ? 8u : 16u;
    const uint64_t noetig = static_cast<uint64_t>(bw) * bh * bb;
    if (chunk.size() < noetig) {
        fehler = "Chunk zu kurz fuer Mip 0 (" + std::to_string(chunk.size()) + " < " + std::to_string(noetig) + ")";
        return false;
    }
    rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    uint8_t block[4 * 4 * 4];
    const uint8_t* q = chunk.data();
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx, q += bb) {
            std::memset(block, 0, sizeof block);
            if (k.format == 54) {
                bcdec_bc1(q, block, 16);
            } else if (k.format == 63) {
                uint8_t rg[4 * 4 * 2];
                bcdec_bc5(q, rg, 8);
                for (int i = 0; i < 16; ++i) {
                    block[i * 4 + 0] = rg[i * 2 + 0];
                    block[i * 4 + 1] = rg[i * 2 + 1];
                    block[i * 4 + 2] = 0;
                    block[i * 4 + 3] = 255;
                }
            } else {
                bcdec_bc7(q, block, 16);
            }
            for (uint32_t y = 0; y < 4; ++y) {
                const uint32_t py = by * 4 + y;
                if (py >= h) break;
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t px = bx * 4 + x;
                    if (px >= w) break;
                    std::memcpy(&rgba[(static_cast<size_t>(py) * w + px) * 4], &block[(y * 4 + x) * 4], 4);
                }
            }
        }
    }
    return true;
}

void BaueNormale(std::vector<uint8_t>& rgba, double& anteil) {
    size_t passt = 0;
    const size_t n = rgba.size() / 4;
    for (size_t i = 0; i < n; ++i) {
        uint8_t* p = &rgba[i * 4];
        const double x = p[0] / 127.5 - 1.0, y = p[1] / 127.5 - 1.0;
        const double z = std::sqrt(std::max(0.0, 1.0 - x * x - y * y));
        const double alt = p[2] / 127.5 - 1.0;
        if (std::fabs(alt - z) < 0.1) ++passt;
        p[2] = static_cast<uint8_t>(std::lround((z * 0.5 + 0.5) * 255.0));
        p[3] = 255;
    }
    anteil = n ? static_cast<double>(passt) / static_cast<double>(n) : 0.0;
}

bool SchreibePng(const std::string& utf8Pfad, const std::vector<uint8_t>& rgba,
                 uint32_t w, uint32_t h, int kanaele, std::string& fehler) {
    if (rgba.size() != static_cast<size_t>(w) * h * 4) { fehler = "Bildgroesse passt nicht"; return false; }
    std::vector<uint8_t> quelle;
    const std::vector<uint8_t>* bild = &rgba;
    if (kanaele == 3) {
        quelle.resize(static_cast<size_t>(w) * h * 3);
        for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; ++i) {
            quelle[i * 3 + 0] = rgba[i * 4 + 0];
            quelle[i * 3 + 1] = rgba[i * 4 + 1];
            quelle[i * 3 + 2] = rgba[i * 4 + 2];
        }
        bild = &quelle;
    }
    size_t laenge = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(bild->data(), static_cast<int>(w), static_cast<int>(h),
                                                           kanaele, &laenge, 1, MZ_FALSE);
    if (png == nullptr) { fehler = "PNG liess sich nicht erzeugen"; return false; }
    const uint8_t* b = static_cast<const uint8_t*>(png);
    const bool ok = fbdatei::SchreibeAlles(utf8Pfad, std::vector<uint8_t>(b, b + laenge), fehler);
    mz_free(png);
    return ok;
}

void Mittel(const std::vector<uint8_t>& rgba, double aus[4]) {
    double s[4] = { 0, 0, 0, 0 };
    const size_t n = rgba.size() / 4;
    for (size_t i = 0; i < n; ++i) for (int c = 0; c < 4; ++c) s[c] += rgba[i * 4 + c];
    for (int c = 0; c < 4; ++c) aus[c] = n ? s[c] / static_cast<double>(n) : 0.0;
}

} // namespace fbtextur
