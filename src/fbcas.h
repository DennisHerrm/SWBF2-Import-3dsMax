// ============================================================
//  fbcas.h - die unterste Schicht des Frostbite-Containers.
//
//  Portiert aus fb_container.py, das an der echten Installation
//  von Battlefront II bewiesen ist. Die Zahlen und Regeln hier
//  sind KEINE Vermutungen - jede einzelne steht dort mit ihrer
//  Herkunft im Kommentar.
//
//  Diese Datei kennt weder das Max-SDK noch Bundles oder Meshes.
//  Sie kann genau drei Dinge:
//
//    1. Entschleiern: beginnt eine Datei mit 0x01CED100 oder
//       0x03CED100, steht der Inhalt erst ab 0x22C.
//    2. cas.cat lesen: die Zuordnung SHA-1 -> Datei, Versatz,
//       Laenge.
//    3. cas-Bloecke entpacken: roh, zlib, lz4, zstd, und zstd
//       mit Woerterbuch. Dazu der Deltapfad fuer Patches.
//
//  Alles Weitere - initfs, Manifest, Bundles, Index - kommt
//  darauf. Erst wenn diese drei Sachen gegen Python auf 0
//  Abweichungen stehen, geht es weiter.
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fbcas {

// ------------------------------------------------------------
//  Entschleierung
//
//  Frostys NullDeobfuscator: die Signatur davor ist 0x22C Byte
//  lang. Gilt fuer JEDE Datei, die durch den Deobfuscator geht -
//  layout.toc, initfs_win32 UND cas.cat.
// ------------------------------------------------------------
constexpr uint32_t kObfMagicA  = 0x01CED100u;
constexpr uint32_t kObfMagicB  = 0x03CED100u;
constexpr size_t   kObfHeader  = 0x22C;

size_t DeobfStart(const std::vector<uint8_t>& daten);

// ------------------------------------------------------------
//  cas.cat
// ------------------------------------------------------------
struct CatEintrag {
    uint8_t  sha1[20] = {};
    uint32_t versatz = 0;
    uint32_t laenge = 0;
    uint32_t logischerVersatz = 0;
    int32_t  archiv = 0;          // ergibt cas_NN.cas
};

struct CatPatch {
    uint8_t sha1[20] = {};
    uint8_t basisSha1[20] = {};
    uint8_t deltaSha1[20] = {};
};

struct Katalog {
    std::vector<CatEintrag> eintraege;
    std::vector<CatPatch>   patches;
};

// Wo der Katalog anfaengt. -1, wenn die Kennung "NyanNyanNyanNyan"
// nirgends in den ersten 4 KB steht.
long FindeCatKennung(const std::vector<uint8_t>& daten);

bool LiesKatalog(const std::vector<uint8_t>& daten, Katalog& aus, std::string& fehler);

// ------------------------------------------------------------
//  cas-Bloecke
//
//  Ein Block: u32 BE entpackte Groesse, u16 LE Verfahren,
//  u16 BE Pufferlaenge. Die oberen Bits tragen zusaetzliche
//  Angaben - siehe die Umsetzung.
// ------------------------------------------------------------
class BlockLeser {
public:
    // Das Woerterbuch stammt aus initfs_win32 (Dictionaries/ebx.dict)
    // und wird nur fuer zstd-Bloecke mit gesetztem Woerterbuchbit
    // gebraucht. Ohne es scheitern genau diese Bloecke - mit einer
    // Meldung, nicht still.
    void SetzeWoerterbuch(std::vector<uint8_t> woerterbuch);

    // Einen ganzen Strom entpacken (Block fuer Block bis zum Ende).
    bool EntpackeAlles(const std::vector<uint8_t>& daten,
                       std::vector<uint8_t>& aus, std::string& fehler);

    // Deltapfad: der Deltastrom steuert, welche Basisbloecke
    // uebernommen, gemischt, ersetzt oder uebersprungen werden.
    bool EntpackeGepatcht(const std::vector<uint8_t>& basis,
                          const std::vector<uint8_t>& delta,
                          std::vector<uint8_t>& aus, std::string& fehler);

    // Wie oft welches Verfahren vorkam - fuers Protokoll.
    const std::vector<std::pair<int, long>>& Zaehler() const { return zaehler_; }

private:
    std::vector<uint8_t> woerterbuch_;
    std::vector<std::pair<int, long>> zaehler_;
    void Zaehle(int art);

    bool EinBlock(const std::vector<uint8_t>& daten, size_t& p,
                  std::vector<uint8_t>& aus, std::string& fehler);
};

// ------------------------------------------------------------
//  Kleinkram
// ------------------------------------------------------------
bool LiesDatei(const std::string& pfad, std::vector<uint8_t>& aus, std::string& fehler);
std::string Sha1Text(const uint8_t sha1[20]);

} // namespace fbcas
