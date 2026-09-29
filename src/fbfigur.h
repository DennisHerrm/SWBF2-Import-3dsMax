// ============================================================
//  fbfigur.h - eine Figur zusammensetzen.
//
//  Hier laufen alle Schichten zusammen: aus einem Bundlenamen
//  werden die MeshSets und das Skelett geholt und daraus ein
//  Modell gebaut - dasselbe, das fbtools als .fbmodel schreibt.
//
//  Die Probe dafuer ist die schaerfste im ganzen Projekt: die
//  erzeugte Datei muss BYTEWEISE der von fbtools gleichen.
// ============================================================
#pragma once

#include "fbgame.h"
#include "fbindex.h"
#include "fbmeshset.h"

#include <string>
#include <vector>

namespace fbfigur {

struct Bone {
    std::string name;
    int32_t eltern = -1;
    int32_t typ = 0;
    double ruhelage[12] = {};
};

struct Material {
    std::string name;
    std::vector<std::string> texturen;   // noch leer, kommt mit Stufe 4
};

// Eine Spalte je Datenart. Leer heisst: dieser Strom fehlt.
struct Mesh {
    std::string name;
    int32_t lod = 0;
    int32_t material = -1;
    uint32_t vertexCount = 0;
    uint32_t dreiecke = 0;
    int32_t sectionIndex = 0;
    uint32_t bonesJeVertex = 0;
    int32_t tiefe = 0;
    // Fuer die Materialzuordnung (fbmaterial) - NICHT in der .fbmodel, die
    // bleibt byteweise gleich mit fbtools.
    std::string meshAsset;           // Name des MeshSets = Name des MeshAssets
    int32_t materialId = -1;         // Section.MaterialId
    std::vector<uint16_t> boneRefs;
    std::vector<uint32_t> indices;
    std::vector<double> pos, normal, tangent, uv0, uv1, farbe, boneWgt, boneWgt2;
    std::vector<uint16_t> boneIdx, boneIdx2;
    bool hat[10] = {};                   // in der Reihenfolge der BIT_-Werte
};

struct Modell {
    std::string quelle;
    std::string skelett;
    double einheit = 1.0;
    std::vector<Bone> bones;
    std::vector<Material> materialien;
    std::vector<Mesh> meshes;
    int anders = 0;                      // Sections mit Deklaration 1
    int schatten = 0;                    // weggelassene Schattengeometrie
    std::vector<std::string> hinweise;
    // 0.99.0: weitere Bundles, aus denen Teile stammen (Fahrzeuge). Die
    // Materialzuordnung liest sie mit, sonst fehlen die Texturen aller Teile
    // ausser dem ersten.
    std::vector<std::string> weitereBundles;
    // 1.19.0: Was in der Szene als Figur vermerkt wird. Leer = quelle (Bundle).
    // Fahrzeuge tragen hier "vehicle: ground/at_te" - ohne das hielt das
    // Animationsfenster den AT-TE fuer die Figur "gunner" und fand nichts.
    std::string figurVermerk;
};

// `skelettSchluessel` ist ein Teilstring eines EBX-Namens, z.B.
// "Characters/Rigs/Humanoids/Walrus_HumanMale".
bool BaueFigur(fbgame::Spiel& spiel, const fbindex::Index& idx,
               const std::string& bundleName, const std::string& skelettSchluessel,
               int lod, bool alleLods, Modell& aus, std::string& fehler);

// Wie BaueFigur, aber fuer EIN MeshSet (0.86.0, fuer Fahrzeuge). Bei Figuren
// ist der MeshSet-Name zugleich der Bundlename; Fahrzeug-MeshSets liegen
// dagegen in LEVEL-Bundles (AT-RT: 28 Bundles mit zusammen 6978 Meshes -
// alles, was auf den Karten steht). Deshalb hier gezielt ein MeshSet.
bool BaueMeshSet(fbgame::Spiel& spiel, const fbindex::Index& idx,
                 const std::string& meshsetName, const std::string& skelettSchluessel,
                 int lod, bool alleLods, Modell& aus, std::string& fehler);

// Die Waffen der Figur (0.42.0). Gefunden per Index: Helden-Ausruestung
// liegt unter gameplay/equipment/, Lichtschwerter tragen den Figurennamen
// (lightsaberanakin). Gemessen: starr (Meshtyp 0, keine Bones). Deshalb
// haengt hier jeder Vertex mit Gewicht 1 an Wep_Root, und die Positionen
// werden mit der Ruhelage von Wep_Root aus dem Waffenraum in den Modellraum
// gebracht - so folgt die Waffe jeder Animation wie im Spiel. Die Meshes
// heissen "weapon_..." (eigener Layer im Plugin). Frontend- und
// Egoperspektive-Fassungen ("frontend", "1p") werden ausgelassen.
bool FuegeWaffenHinzu(fbgame::Spiel& spiel, const fbindex::Index& idx, Modell& m, std::vector<std::string>& protokoll);

// win32/characters/hero/anakin/anakin_01/vur_anakin_01_bpb -> anakin
std::string FigurSchluessel(const std::string& bundleName);

// Die Basispose der Figur (0.43.0) - gegen die "verzerrten Koepfe". Die
// ObjectBlueprints des Figuren-Bundles (Kopf, Haare, Koerper) tragen
// BasePoseTransforms: Bone-Index plus Transform. Ob lokal oder im
// Modellraum, wird GEMESSEN (Median-Abstand der Verschiebungen zur
// Ruhelage bzw. zur Modelllage des Skeletts); geschrieben wird immer die
// LOKALE Lage je Bone, im Spielraum und in Metern.
struct PoseBone { int32_t index = -1; std::string name; double lokal[12] = {}; };
struct Basispose {
    std::vector<PoseBone> bones;
    std::string deutung;                 // "lokal" oder "modell"
    double medianLokal = -1.0, medianModell = -1.0;   // Meter
    std::vector<std::string> protokoll;
};
bool LiesBasispose(fbgame::Spiel& spiel, const fbindex::Index& idx, const Modell& m, Basispose& aus);
bool SchreibeBasispose(const std::string& utf8Pfad, const Basispose& p, std::string& fehler);

// Die .fbmodel-Datei als Bytes. SchreibeFbmodel legt genau diese Bytes ab.
bool BaueFbmodelBytes(const Modell& m, std::vector<uint8_t>& aus, std::string& fehler);
bool SchreibeFbmodel(const std::string& pfad, const Modell& m, std::string& fehler);

} // namespace fbfigur
