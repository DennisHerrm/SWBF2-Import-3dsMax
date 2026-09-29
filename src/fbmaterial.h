// ============================================================
//  fbmaterial.h - welche Textur zu welchem Mesh einer Figur gehoert.
//
//  Die Kette, wie sie Frosty liest (FrostyMeshSetEditor.cs) und wie sie
//  an Anakin GEMESSEN wurde:
//    Section.MaterialId -> MeshAsset.Materials[MaterialId] (Zeiger auf
//    eine MeshMaterial-Instanz mit eigener GUID) -> Shader.TextureParameters.
//  Bei SWBF2-Figuren sind diese Listen LEER. Die Texturen stehen in der
//  MeshVariationDatabase des Bundles:
//    Entries[].Mesh -> MeshAsset, Entries[].Materials[].Material -> Import
//    auf die MeshMaterial-Instanz, .TextureParameters[] -> ParameterName +
//    Verweis auf die Textur-EBX.
//  Zugeordnet wird ueber die Instanz-GUID; nur wenn die fehlt, ueber die
//  Reihenfolge (so macht es Frosty). Welcher Weg genommen wurde, steht im
//  Beipackzettel und im Protokoll.
// ============================================================
#pragma once

#include "fbbeipack.h"
#include "fbfigur.h"
#include "fbgame.h"
#include "fbindex.h"

#include <string>
#include <vector>

namespace fbmaterial {

// Je Mesh des Modells (gleiche Reihenfolge wie m.meshes) die Texturen.
// 0.99.0: `weitereBundles` wird zusaetzlich gelesen. Fahrzeuge bestehen aus
// MeshSets, die in VERSCHIEDENEN Level-Bundles liegen; die MeshVariationDatabase
// eines Teils steht nur in dessen eigenem Bundle. Gemessen an DHs Import des
// aalstormtroopertransport: nur die 7 Materialien des ersten Teils wurden
// gefunden ("ueber guid"), die restlichen 30 blieben ohne ("ueber NICHTS").
bool Loese(fbgame::Spiel& spiel, const fbindex::Index& idx, const fbfigur::Modell& m,
           std::vector<fbbeipack::MeshMaterial>& aus, std::vector<std::string>& protokoll,
           std::string& fehler);

// Texturen dekodieren und als PNG in `ordnerUtf8` ablegen. Schon vorhandene
// PNG werden wiederverwendet (nur der Kopf wird gelesen).
bool SchreibeTexturen(fbgame::Spiel& spiel, const fbindex::Index& idx,
                      std::vector<fbbeipack::MeshMaterial>& mm, const std::string& ordnerUtf8,
                      std::vector<std::string>& protokoll, std::string& fehler);

} // namespace fbmaterial
