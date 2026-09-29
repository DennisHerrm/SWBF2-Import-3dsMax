// ============================================================
//  fbgame.h - die Spieldateien als Ganzes.
//
//  Hier laufen die Bausteine zusammen: Pfade auflösen, layout.toc
//  lesen, das initfs mit dem zstd-Woerterbuch, die Kataloge und
//  das Manifest. Am Ende kann diese Schicht das Entscheidende:
//
//      SHA-1  ->  entpackte Nutzdaten
//
//  Portiert aus GameFS in fb_container.py, das an der echten
//  Installation von Battlefront II bewiesen ist.
//
//  Was hier NOCH NICHT drin ist: Bundles und der Index, also der
//  Weg vom DATEINAMEN zum SHA-1. Das ist die naechste Schicht.
// ============================================================
#pragma once

#include "fbcas.h"
#include "fbdb.h"

#include <map>
#include <string>
#include <vector>

namespace fbgame {

// ------------------------------------------------------------
//  Manifest
// ------------------------------------------------------------
struct ManifestDatei {
    int32_t  dateiRef = 0;      // siehe CasPfadFuerRef
    uint32_t versatz = 0;
    int64_t  laenge = 0;
};

struct ManifestBundle {
    uint32_t hash = 0;
    std::vector<ManifestDatei> dateien;
};

struct ManifestChunk {
    uint8_t guid[16] = {};
    ManifestDatei datei;
};

struct Katalogangabe {
    std::string name;           // z.B. "win32/installation/test"
    bool immerInstalliert = false;
    bool gueltig = false;
};

// ------------------------------------------------------------
class Spiel {
public:
    // `wurzel` ist der Ordner, der Data\ und ggf. Patch\ enthaelt.
    bool Oeffne(const std::string& wurzel, std::string& fehler);

    // ResolvePath aus Frosty: "native_data/..." sucht ab Index 1,
    // wenn es einen Patch gibt; "native_patch/..." nur in Index 0;
    // ohne Praefix ueberall. Leer, wenn nichts da ist.
    std::string Aufloesen(const std::string& name) const;

    // ManifestFileRef: KatalogIndex = (v>>12)-1, ImPatch = v&0x100,
    // CasIndex = (v&0xFF)+1.
    std::string CasPfadFuerRef(int32_t dateiRef) const;

    // Entpackte Nutzdaten zu einer SHA-1, Deltapfad eingeschlossen.
    bool HoleNachSha1(const std::string& sha1Hex,
                      std::vector<uint8_t>& aus, std::string& fehler);

    // Den rohen Bundleblob holen (entpackt, aber noch nicht ausgewertet).
    bool LiesBundleRoh(const ManifestBundle& mb, std::vector<uint8_t>& aus,
                       std::string& fehler);

    bool HoleManifestChunk(const ManifestChunk& mc,
                           std::vector<uint8_t>& aus, std::string& fehler);

    // Nur der Fundort, ohne zu entpacken.
    struct Fundort {
        std::string art;        // "cas" oder "patched" oder leer
        std::string cas;
        uint32_t versatz = 0;
        uint32_t laenge = 0;
        std::string basisCas, deltaCas;
        uint32_t basisVersatz = 0, basisLaenge = 0;
        uint32_t deltaVersatz = 0, deltaLaenge = 0;
    };
    Fundort Finde(const std::string& sha1Hex) const;

    // ---- Auskunft --------------------------------------------
    const std::vector<std::string>& Pfade() const { return pfade_; }
    const std::vector<std::string>& Superbundles() const { return superbundles_; }
    const std::vector<Katalogangabe>& Kataloge() const { return kataloge_; }
    const std::vector<ManifestBundle>& Bundles() const { return manifestBundles_; }
    const std::vector<ManifestChunk>& Chunks() const { return manifestChunks_; }
    const std::vector<ManifestDatei>& Dateien() const { return manifestDateien_; }
    int64_t Basisnummer() const { return basisNummer_; }
    int64_t Kopfnummer() const { return kopfNummer_; }
    size_t KatalogEintraege() const { return catEintraege_.size(); }
    size_t KatalogPatches() const { return catPatches_.size(); }
    // Datei aus dem initfs, Endung genuegt (z.B. "ebx.dict").
    const std::vector<uint8_t>* SpeicherDatei(const std::string& endung) const;
    // Meldungen, die beim Oeffnen angefallen sind (etwa uebersprungene Kataloge).
    const std::vector<std::string>& Hinweise() const { return hinweise_; }

private:
    std::string wurzel_;
    std::vector<std::string> pfade_;
    std::vector<std::string> superbundles_;
    std::vector<Katalogangabe> kataloge_;
    std::vector<ManifestDatei> manifestDateien_;
    std::vector<ManifestBundle> manifestBundles_;
    std::vector<ManifestChunk> manifestChunks_;
    std::map<std::string, std::vector<uint8_t>> speicherFs_;
    // Zu jedem Eintrag gehoert der KATALOG, aus dem er stammt. Ohne den
    // laesst sich die cas-Datei nicht finden: cas_01.cas gibt es in fast
    // jedem der 23 Kataloge, und der erste Treffer ist meist der falsche.
    struct CatFund {
        fbcas::CatEintrag eintrag;
        std::string katalogNativ;     // z.B. "native_data/win32/installation/mp"
    };
    std::map<std::string, CatFund> catEintraege_;
    std::map<std::string, fbcas::CatPatch> catPatches_;
    std::vector<std::string> hinweise_;
    int64_t basisNummer_ = 0, kopfNummer_ = 0;
    fbcas::BlockLeser leser_;

    void QuelleDazu(const std::string& unterordner, bool durchsuchen);
    bool LiesLayouts(std::string& fehler);
    bool LiesKataloge(std::string& fehler);
    bool LiesManifest(const fbdb::Wert& layout, std::string& fehler);
    void LiesInitfs();
    bool LiesRoh(const std::string& nativePfad, uint32_t versatz, uint32_t laenge,
                 std::vector<uint8_t>& aus, std::string& fehler) const;
};

} // namespace fbgame
