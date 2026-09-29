// ============================================================
//  fbmeshset.h - MeshSet lesen (die Geometrie).
//
//  Portiert aus fb_meshset.py. Die drei Sachen, die hier
//  wirklich zaehlen und teuer erarbeitet wurden:
//
//   1. ZWEI GeometryDeclarationDesc je Section. Welche zu den
//      Daten gehoert, steht NIRGENDS - beide werden probiert und
//      die glaubwuerdigere genommen (Positionen endlich,
//      Bone-Gewichte summieren auf 1, UV im ueblichen Bereich).
//   2. Vertexelemente ueber ihren OFFSET lesen, nicht ueber die
//      Summe der Groessen. Die Elemente folgen nicht immer
//      lueckenlos aufeinander.
//   3. ALLE LODs koennen in EINEM Puffer liegen, jeder Block auf
//      16 Byte aufgerundet. Ohne diesen Versatz laufen die
//      Positionen bis 1e37.
//
//  Dazu: Sections mit bonesPerVertex 8 tragen einen ZWEITEN Satz
//  Einfluesse (BoneIndices2/BoneWeights2). Nur den ersten zu
//  lesen ergibt Gewichtssummen bis hinunter zu 0,667.
// ============================================================
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fbmesh {

constexpr int kMaxLodCount   = 6;
constexpr int kMaxElements   = 16;
constexpr int kMaxStreams    = 16;    // SWBF2
constexpr int kMaxCategories = 5;
constexpr int kDeclCount     = 2;     // SWBF2
constexpr int kUnknownSectionBytes = 44;
constexpr int kLodAusrichtung = 16;

constexpr int32_t kRenderFormatR16Uint = 33;
constexpr int32_t kRenderFormatR32Uint = 32;

struct Element {
    uint8_t usage = 0;
    uint8_t format = 0;
    uint8_t offset = 0;
    uint8_t streamIndex = 0;
    std::string usageName;
    std::string formatName;
    int size = 0;
};

struct Stream {
    uint8_t stride = 0;
    uint8_t classification = 0;
};

struct Decl {
    std::vector<Element> elements;    // immer kMaxElements
    std::vector<Stream>  streams;     // immer kMaxStreams
    uint8_t elementCount = 0;
    uint8_t streamCount = 0;
};

struct Section {
    int index = 0;
    int32_t materialId = 0;
    uint32_t primitiveCount = 0;
    uint32_t startIndex = 0;
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
    uint8_t vertexStride = 0;
    uint8_t primitiveType = 0;
    uint8_t bonesPerVertex = 0;
    std::vector<uint16_t> boneList;
    std::string materialName;
    Decl decls[kDeclCount];
};

struct Lod {
    uint32_t meshTypeId = 0;
    uint32_t flags = 0;
    int32_t indexBufferFormat = 0;
    uint32_t indexBufferSize = 0;
    uint32_t vertexBufferSize = 0;
    uint32_t nameHash = 0;
    std::string name, shortName, shaderDebugName;
    std::string chunkId;
    std::vector<Section> sections;
};

struct MeshSet {
    uint32_t nameHash = 0;
    uint32_t meshTypeId = 0;
    uint32_t flags = 0;
    uint16_t sectionCount = 0;
    std::string name, fullname;
    std::vector<Lod> lods;
    std::vector<std::string> warnungen;
    // Kopf eines geskinnten MeshSets (meshTypeId 1): zwei u16 und die Liste
    // dahinter. Frosty nennt sie boneCount/CullBoxCount; die Liste traegt
    // Skelettindizes (je Cull-Box einer).
    uint16_t kopfBoneCount = 0;
    uint16_t kopfWerte[2] = { 0, 0 };   // die zwei u16 hinter dem Kopf, roh
    std::vector<uint16_t> kopfBoneListe;
    // Composite-MeshSet (meshTypeId 2, z. B. AT-AT): je Teil eine Lage
    // (16 Floats, Zeilen right/up/forward/trans mit Fuellwert). Die
    // BoneIndices im Vertex sind dann TEILNUMMERN, die boneList der Section
    // nennt nur, welche Teile in ihr vorkommen (gemessen 1.41.0).
    std::vector<std::array<float, 16>> teilTransformen;
    // Manche MeshSets haben KEINEN Chunk (chunkId ist lauter Nullen). Ihr
    // Vertex- und Indexpuffer steht dann IN der .res selbst, und wo genau,
    // sagen die ersten acht Byte der resMeta aus dem Bundle: u32 Versatz,
    // u32 Laenge. Ohne das fehlen bei Anakin Schoesse, Rock und Aermel.
    std::vector<uint8_t> inlineDaten;
};

// Wirft nichts: bei Misserfolg false und eine Meldung.
// resMeta sind die 16 Byte aus dem Bundle; ohne sie bleiben die Inline-Daten
// leer (fuer MeshSets mit Chunk macht das keinen Unterschied).
bool LiesMeshSet(const std::vector<uint8_t>& daten, MeshSet& aus, std::string& fehler,
                 const uint8_t* resMeta = nullptr);

// Wo im Puffer ein LOD anfaengt. Liegt je LOD ein eigener Chunk vor, ist es 0.
size_t LodVersatz(const MeshSet& ms, size_t lodIndex, size_t pufferLaenge);

// Ein dekodierter Vertex: Name des Verwendungszwecks -> Zahlen.
//
// DOPPELTE Genauigkeit, nicht einfache. Python rechnet 131/255 in double und
// druckt 0.513725; mit float kaeme 0.513726 heraus. Fuer Max wird spaeter
// ohnehin auf float verengt - aber erst beim Schreiben, nicht beim Rechnen.
using Vertex = std::map<std::string, std::vector<double>>;

struct Bewertung {
    double punkte = 0.0;
    double pos = -1.0, gewichte = -1.0, uv = -1.0;   // -1 heisst "nicht vorhanden"
    size_t anzahl = 0;
};

// Beide Deklarationen probieren und die glaubwuerdigere nehmen.
bool LiesVerticesGemessen(const Section& s, const std::vector<uint8_t>& vertexPuffer,
                          std::vector<Vertex>& aus, int& decl, Bewertung& note,
                          size_t maxVertices = 0);

bool LiesVertices(const Section& s, const std::vector<uint8_t>& vertexPuffer,
                  int decl, std::vector<Vertex>& aus, size_t maxVertices = 0);

Bewertung Bewerte(const std::vector<Vertex>& verts, double grenze = 1000.0);

int IndexBreite(const Lod& lod);
bool LiesIndices(const Lod& lod, const std::vector<uint8_t>& puffer, const Section& s,
                 std::vector<uint32_t>& aus, std::string& fehler);

bool IstTiefenSection(const Section& s);
bool IstSchattengeometrie(const Section& s, const std::vector<Vertex>& verts);

// Textfassung fuer die Gegenprobe gegen Python.
std::string AlsText(const MeshSet& ms);

} // namespace fbmesh
