// ============================================================
//  fbbundle.h - ein Bundle lesen.
//
//  Ein Bundle ist die Liste dessen, was zusammengehoert: EBX
//  (Objekte mit Namen), RES (Rohdaten wie MeshSets und Texturen)
//  und Chunks. Zu jedem Eintrag steht die SHA-1 dabei - und
//  damit ist der Weg vom NAMEN zu den Nutzdaten geschlossen:
//
//      Name -> Bundle -> SHA-1 -> cas.cat -> entpackte Daten
//
//  Portiert aus read_bundle in fb_container.py.
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fbbundle {

struct EbxRef {
    std::string name;
    std::string sha1;
    uint32_t originalSize = 0;
};

struct ResRef {
    std::string name;
    std::string sha1;
    uint32_t originalSize = 0;
    uint32_t resType = 0;
    uint8_t  resMeta[16] = {};
    uint64_t resRid = 0;
};

struct ChunkRef {
    uint8_t  guid[16] = {};
    std::string sha1;
    uint32_t logicalOffset = 0;
    uint32_t logicalSize = 0;
};

struct Bundle {
    std::vector<EbxRef>   ebx;
    std::vector<ResRef>   res;
    std::vector<ChunkRef> chunks;
    uint32_t datenVersatz = 0;
    bool grossEndig = true;
};

// Die Kennung ist mit "pecm" verwuerfelt - bei Spielen bis 2017,
// also auch SWBF2. Verschluesselte Bundles kommen hier nicht vor
// und werden benannt, nicht stillschweigend uebergangen.
bool LiesBundle(const std::vector<uint8_t>& daten, Bundle& aus, std::string& fehler);

// FNV-1, 32 Bit - damit ordnet Frosty den Bundle-Hashes Namen zu.
uint32_t Fnv1(const std::string& s);

} // namespace fbbundle
