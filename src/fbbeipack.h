// ============================================================
//  fbbeipack.h - der Beipackzettel einer Figur: welche Textur zu
//  welchem Mesh gehoert.
//
//  Die .fbmodel bleibt dabei UNVERAENDERT - sie muss byteweise gleich
//  mit fbtools bleiben. Die Materialien stehen deshalb in einer
//  eigenen Datei daneben (<figur>.fbmodel.material.txt), Zeile fuer
//  Zeile, Tab-getrennt, UTF-8:
//
//      SWBF2MATERIAL 1
//      MESH <i> <meshAsset> <materialId> <materialName> <weg>
//      TEX  <i> <parameter> <art> <datei> <textur> <format> <b>x<h>
//
//  <i> ist der Meshindex in der .fbmodel. <art> ist farbe, normal oder
//  sonst. <weg> sagt, woher die Zuordnung kam (guid, reihenfolge,
//  shader oder leer).
//
//  Diese Datei braucht nur fbdatei - das Plugin liest den Zettel,
//  ohne das Spiel zu kennen.
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fbbeipack {

struct Textur {
    std::string parameter;     // BaseColor, Normal, AOSlice, ...
    std::string art;           // farbe | normal | deckkraft | sonst
    std::string datei;         // PNG, voller Pfad (UTF-8)
    std::string name;          // Name der Textur-EBX
    int32_t format = 0;
    uint32_t breite = 0, hoehe = 0;
};

struct MeshMaterial {
    std::string meshAsset;
    int32_t materialId = -1;
    std::string materialName;
    std::string weg;
    std::vector<Textur> texturen;
};

bool Schreibe(const std::string& utf8Pfad, const std::vector<MeshMaterial>& mm, std::string& fehler);
bool Lies(const std::string& utf8Pfad, std::vector<MeshMaterial>& mm, std::string& fehler);

// Welche Art ein Texturparameter ist. Nur Namen, die GEMESSEN wurden
// (Anakin: BaseColor, CS, Normal, NW); alles andere ist "sonst".
std::string ArtVon(const std::string& parameter);

} // namespace fbbeipack
