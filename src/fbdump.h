// fbdump.h - Leser fuer die beiden Dumpformate aus fbtools.
//
// .fbmodel  Skelett, Meshes, Skinning, Materialien   (fb_model.py)
// .fbanim   ein Animationsclip                       (fb_animdump.py)
//
// Der Aufbau steht im Kopfkommentar der beiden Python-Dateien. Hier wird er
// nur gelesen. Beide Formate sind so gebaut, dass ein Lesevorgang genuegt und
// alle Bloecke auf vier Byte ausgerichtet sind.
//
// Diese Datei kennt das Max-SDK NICHT. Sie wird sowohl vom Plugin als auch vom
// Vergleichswerkzeug benutzt; so ist sichergestellt, dass beide dasselbe lesen.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fb {

// ---------------------------------------------------------------- Modell --

enum ModelStream : uint32_t {
    STREAM_POS      = 1,
    STREAM_NORMAL   = 2,
    STREAM_TANGENT  = 4,
    STREAM_UV0      = 8,
    STREAM_UV1      = 16,
    STREAM_COLOR    = 32,
    STREAM_BONEIDX  = 64,
    STREAM_BONEWGT  = 128,
    STREAM_BONEIDX2 = 256,   // zweiter Satz bei acht Einfluessen
    STREAM_BONEWGT2 = 512,
};

struct Bone {
    std::string name;
    int32_t     parent = -1;   // -1 ist die Wurzel
    int32_t     type   = 0;
    float       rest[12] = {}; // right(3) up(3) forward(3) trans(3), lokal
};

struct Material {
    std::string name;
    std::vector<std::string> textures;
};

struct Mesh {
    std::string name;
    uint32_t    lod            = 0;
    int32_t     material       = -1;
    uint32_t    vertexCount    = 0;
    uint32_t    triangleCount  = 0;
    uint32_t    streams        = 0;
    uint32_t    bonesPerVertex = 0;
    uint32_t    sectionIndex   = 0;
    uint32_t    depth          = 0;   // 1 = Tiefen-/Schattengeometrie

    // Flache Felder, je Vertex so viele Werte wie die Spalte breit ist.
    std::vector<float>    pos, normal, tangent, uv0, uv1, color, weight, weight2;
    std::vector<uint16_t> boneIndex;   // vier je Vertex
    std::vector<uint16_t> boneIndex2;  // vier weitere, wenn bonesPerVertex 8 ist
    std::vector<uint32_t> indices;     // drei je Dreieck
    std::vector<uint16_t> boneRefs;    // lokaler Index -> Skelettindex

    bool has(uint32_t bit) const { return (streams & bit) != 0; }
};

struct Model {
    std::string source;       // Bundle oder Figur ("vehicle: ground/at_te" bei Fahrzeugen)
    std::string skeleton;     // Skelettname
    float       unit  = 1.0f; // Meter je Einheit
    uint32_t    flags = 0;
    std::vector<Bone>     bones;
    std::vector<Material> materials;
    std::vector<Mesh>     meshes;
};

// ------------------------------------------------------------ Animation --

struct Channel {
    std::string name;      // Slotname aus dem Rig, z.B. "Spine.q"
    char        kind = 'f';// 'q' Drehung, 't' Verschiebung, 'f' Float
    uint32_t    components = 1;
    bool        constant   = false;
    std::vector<float> values;  // components * (1 bei konstant, sonst keys)
};

struct Anim {
    std::string name;
    std::string codec;
    float       duration = 0.0f;
    int32_t     endFrame = -1;
    bool        additive = false;
    float       fps      = 0.0f;
    int32_t     trajJointIndex = -1;
    std::vector<float>   times;   // Bildnummern
    std::vector<Bone>    bones;
    std::vector<Channel> channels;
};

// ----------------------------------------------------------------- lesen --

bool readModel(const std::vector<uint8_t>& data, Model& out, std::string& err);
bool readAnim (const std::vector<uint8_t>& data, Anim&  out, std::string& err);

bool readFile(const char* path, std::vector<uint8_t>& out, std::string& err);
#ifdef _WIN32
bool readFileW(const wchar_t* path, std::vector<uint8_t>& out, std::string& err);
#endif

// Genau die Textfassung, die fb_model.dump_text und fb_animdump.dump_text
// erzeugen. Zeichengleichheit ist die Gegenprobe fuer den Leser.
std::string dumpText(const Model& m, int decimals = 6);
std::string dumpText(const Anim&  a, int decimals = 6);

} // namespace fb
