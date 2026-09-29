// ============================================================
//  fbebx.h - EBX lesen (Objekte mit Namen).
//
//  Portiert aus fb_ebx.py, das FrostySdk/IO/EbxReader.cs folgt
//  (Zweig EbxVersion 4, den SWBF2 benutzt).
//
//  Der entscheidende Punkt: eine EBX-Datei bringt ihre
//  Typbeschreibung SELBST mit. Klassen- und Feldnamen stehen als
//  Zeichenketten im Kopf, wiedergefunden ueber ihren Hash. Die
//  SDK-Typtabelle wird also nicht gebraucht.
//
//  Zwei Fallen, die uns schon einmal Zeit gekostet haben:
//   * Die Art eines Feldes steht in Bit 4 bis 8: (t >> 4) & 0x1F.
//     Nicht die unteren Bits nehmen - das ist die Kategorie.
//   * Der Namenshash ist FNV-1 mit Startwert 5381 und
//     h = (h * 33) ^ c, dieselbe Rechnung wie bei den Bundles,
//     nur als vorzeichenbehaftete 32 Bit gelesen.
// ============================================================
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fbebx {

enum class Art {
    Nichts, Bool, Ganz, Gleit, Text, Guid, Sha1, Objekt, Liste, Zeiger, Import, Boxed
};

struct Wert;
using WertPtr = std::shared_ptr<Wert>;

struct Feld {
    std::string name;
    WertPtr wert;
};

struct Wert {
    Art art = Art::Nichts;
    bool wahr = false;
    int64_t zahl = 0;
    double gleit = 0.0;
    std::string text;              // auch Guid, Sha1, Import
    std::string typ;               // bei Objekt: Klassenname
    std::string guid;              // bei Objekt: die GUID exportierter Instanzen
    int64_t verweis = -1;          // bei Zeiger: Index in objekte
    std::vector<WertPtr> liste;
    std::vector<Feld> felder;

    const Wert* Feldwert(const std::string& name) const;
};

class Datei {
public:
    bool Lies(const std::vector<uint8_t>& daten, std::string& fehler);

    const std::vector<WertPtr>& Objekte() const { return objekte_; }
    const std::string& DateiGuid() const { return dateiGuid_; }
    int Fassung() const { return fassung_; }

    // Alle Objekte eines Typs.
    std::vector<const Wert*> Suche(const std::string& typ) const;

private:
    struct FeldTyp {
        std::string name;
        uint16_t typ = 0;
        std::string typName;
        uint16_t klassenRef = 0;
        uint32_t datenVersatz = 0;
    };
    struct KlassenTyp {
        std::string name;
        int32_t feldIndex = 0;
        uint8_t feldZahl = 0;
        uint8_t ausrichtung = 0;
        uint16_t typ = 0;
        std::string typName;
        uint16_t groesse = 0;
    };

    std::vector<uint8_t> daten_;
    std::vector<FeldTyp> felder_;
    std::vector<KlassenTyp> klassen_;
    std::vector<std::pair<std::string, std::string>> importe_;
    std::vector<std::tuple<uint32_t, uint32_t, int32_t>> arrays_;
    std::vector<WertPtr> objekte_;
    std::string dateiGuid_;
    uint32_t stringsVersatz_ = 0, stringsLaenge_ = 0, datenLaenge_ = 0, arraysVersatz_ = 0;
    int fassung_ = 4;

    std::string TextAn(uint32_t versatz) const;
};

// FNV-1 mit Startwert 5381 (EbxReader.HashString), vorzeichenbehaftet.
int32_t HashText(const std::string& s);

// ------------------------------------------------------------
//  Skelett: das, was der Import aus einem SkeletonAsset braucht.
// ------------------------------------------------------------
struct Lage {
    double right[3] = {0, 0, 0};
    double up[3] = {0, 0, 0};
    double forward[3] = {0, 0, 0};
    double trans[3] = {0, 0, 0};
};

struct Skelett {
    std::string name;
    std::vector<std::string> namen;
    std::vector<int32_t> hierarchie;      // Elternindex je Bone, -1 = Wurzel
    std::vector<Lage> lokal;              // LocalPose
    std::vector<Lage> modell;             // ModelPose
    std::vector<Lage> modellInvers;       // InverseModelPose
    bool gefunden = false;
};

// Sucht ein SkeletonAsset (oder ersatzweise ein Objekt mit BoneNames).
Skelett LiesSkelett(const Datei& e);

// BasePoseTransforms (0.43.0): ein SparseTransformArray in ObjectBlueprints
// (Kopf, Haare, Koerper) - laut SWBF2-Typ-SDK mit den Feldern Indices,
// Transforms und Count. Liefert je Eintrag Bone-Index und Transform.
struct BasisPose {
    std::string objekt;                               // Klassenname des Objekts
    std::vector<std::pair<int32_t, Lage>> eintraege;
    int64_t count = -1;
};
std::vector<BasisPose> LiesBasisPosen(const Datei& e);

// Textfassung fuer die Gegenprobe.
std::string AlsText(const Datei& e, size_t grenze = 0);

} // namespace fbebx
