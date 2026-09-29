// fbdump.cpp - Leser fuer .fbmodel und .fbanim.
//
// Beide Formate sind absichtlich einfach: fester Kopf, Stringtabelle, dann
// Bloecke fester Groesse. Es wird nichts geraten - passt eine Laenge nicht,
// bricht der Leser mit einer Meldung ab, statt Muell zu liefern.
#include "fbdump.h"
#include "fbdatei.h"

#include <cstdint>

#include <cstdio>
#include <cstring>
#include <fstream>

namespace fb {
namespace {

// Ein Leser, der nie ueber das Ende hinausliest. Jeder Fehlgriff setzt
// `bad`, und der Aufrufer prueft das einmal am Ende des Abschnitts.
struct Reader {
    const uint8_t* d = nullptr;
    size_t         n = 0;
    size_t         p = 0;
    bool           bad = false;

    Reader(const std::vector<uint8_t>& v) : d(v.data()), n(v.size()) {}

    bool need(size_t k) {
        if (bad || p + k > n) { bad = true; return false; }
        return true;
    }
    template <class T> T get() {
        T v{};
        if (need(sizeof(T))) { std::memcpy(&v, d + p, sizeof(T)); p += sizeof(T); }
        return v;
    }
    void skip(size_t k) { if (need(k)) p += k; }
    void align4() { while (!bad && (p % 4) != 0) skip(1); }
    void read(void* dst, size_t k) {
        if (need(k)) { std::memcpy(dst, d + p, k); p += k; }
    }
    template <class T> void readVec(std::vector<T>& v, size_t count) {
        if (!need(count * sizeof(T))) return;
        v.resize(count);
        if (count) std::memcpy(v.data(), d + p, count * sizeof(T));
        p += count * sizeof(T);
    }
};

// Die Stringtabelle: Versatz 0 heisst "kein String".
struct Strings {
    const uint8_t* d = nullptr;
    size_t         n = 0;
    std::string get(uint32_t off) const {
        if (!off || off >= n) return std::string();
        size_t e = off;
        while (e < n && d[e]) ++e;
        return std::string(reinterpret_cast<const char*>(d + off), e - off);
    }
};

void readBones(Reader& r, const Strings& s, std::vector<Bone>& out, uint32_t count) {
    out.resize(count);
    for (uint32_t i = 0; i < count && !r.bad; ++i) {
        Bone& b = out[i];
        b.name   = s.get(r.get<uint32_t>());
        b.parent = r.get<int32_t>();
        b.type   = r.get<int32_t>();
        r.skip(4);
        r.read(b.rest, sizeof(b.rest));
    }
}

// %.6f wie in Python. Ohne setlocale bleibt der Punkt der Dezimaltrenner.
void appendFloat(std::string& out, double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    out += buf;
}
void appendInt(std::string& out, long long v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", v);
    out += buf;
}
void appendRow(std::string& out, const std::vector<float>& v, size_t index,
               size_t width, int decimals, char sep) {
    for (size_t k = 0; k < width; ++k) {
        if (k) out += sep;
        appendFloat(out, v[index * width + k], decimals);
    }
}

} // namespace

bool readFile(const char* path, std::vector<uint8_t>& out, std::string& err) {
    // Ueber fbdatei: 64-Bit-Laenge, geprueft (siehe fbdatei.h).
    return fbdatei::LiesAlles(path != nullptr ? path : "", out, err);
}

#ifdef _WIN32
// Der weite Pfad geht ueber _wfopen und nicht ueber std::ifstream: dessen
// Konstruktor mit const wchar_t* ist eine Zugabe von Microsoft und gibt es
// nicht ueberall. Mit FILE* laesst sich derselbe Quelltext auch gegen die
// SDK-Attrappe uebersetzen - und genau daran haengt die Vorabpruefung.
bool readFileW(const wchar_t* path, std::vector<uint8_t>& out, std::string& err) {
    std::FILE* f = _wfopen(path, L"rb");
    if (f == nullptr) { err = "Datei nicht lesbar"; return false; }
    // 64 Bit: ftell liefert long, unter Windows 32 Bit breit.
    uint64_t size = 0;
    if (!fbdatei::Groesse(f, size) || size > static_cast<uint64_t>(SIZE_MAX)) {
        std::fclose(f); err = "Laenge nicht bestimmbar"; return false;
    }
    out.resize(static_cast<size_t>(size));
    const size_t gelesen = size ? std::fread(out.data(), 1, static_cast<size_t>(size), f) : 0;
    std::fclose(f);
    if (gelesen != static_cast<size_t>(size)) { err = "Lesefehler"; return false; }
    return true;
}
#endif

// ---------------------------------------------------------------- Modell --

bool readModel(const std::vector<uint8_t>& data, Model& out, std::string& err) {
    Reader r(data);
    char magic[8] = {};
    r.read(magic, 8);
    if (r.bad || std::memcmp(magic, "FBMODEL1", 8) != 0) {
        err = "keine .fbmodel-Datei"; return false;
    }
    uint32_t version   = r.get<uint32_t>();
    uint32_t headerLen = r.get<uint32_t>();
    uint32_t meshCount = r.get<uint32_t>();
    uint32_t boneCount = r.get<uint32_t>();
    uint32_t matCount  = r.get<uint32_t>();
    uint32_t strBytes  = r.get<uint32_t>();
    out.flags          = r.get<uint32_t>();
    out.unit           = r.get<float>();
    uint32_t srcOff    = r.get<uint32_t>();
    uint32_t skelOff   = r.get<uint32_t>();
    if (version > 2) { err = "Fassung " + std::to_string(version) + ", gelesen wird bis 2"; return false; }
    if (r.bad || headerLen < 64 || headerLen > data.size()) { err = "Kopf unvollstaendig"; return false; }

    r.p = headerLen;
    if (r.p + strBytes > data.size()) { err = "Stringtabelle passt nicht in die Datei"; return false; }
    Strings s{ data.data() + r.p, strBytes };
    r.p += strBytes;

    out.source   = s.get(srcOff);
    out.skeleton = s.get(skelOff);
    readBones(r, s, out.bones, boneCount);

    out.materials.resize(matCount);
    for (uint32_t i = 0; i < matCount && !r.bad; ++i) {
        out.materials[i].name = s.get(r.get<uint32_t>());
        for (int k = 0; k < 4; ++k) {
            std::string t = s.get(r.get<uint32_t>());
            if (!t.empty()) out.materials[i].textures.push_back(t);
        }
    }

    out.meshes.resize(meshCount);
    for (uint32_t i = 0; i < meshCount && !r.bad; ++i) {
        Mesh& m = out.meshes[i];
        m.name           = s.get(r.get<uint32_t>());
        m.lod            = r.get<uint32_t>();
        m.material       = r.get<int32_t>();
        m.vertexCount    = r.get<uint32_t>();
        m.triangleCount  = r.get<uint32_t>();
        uint32_t refCount= r.get<uint32_t>();
        m.streams        = r.get<uint32_t>();
        m.bonesPerVertex = r.get<uint32_t>();
        m.sectionIndex   = r.get<uint32_t>();
        m.depth          = r.get<uint32_t>();
        r.skip(8);
        if (r.bad) break;

        struct Spalte { uint32_t bit; std::vector<float>* f; size_t width; };
        const Spalte spalten[] = {
            { STREAM_POS,     &m.pos,     3 },
            { STREAM_NORMAL,  &m.normal,  3 },
            { STREAM_TANGENT, &m.tangent, 4 },
            { STREAM_UV0,     &m.uv0,     2 },
            { STREAM_UV1,     &m.uv1,     2 },
            { STREAM_COLOR,   &m.color,   4 },
        };
        for (const Spalte& c : spalten)
            if (m.has(c.bit)) r.readVec(*c.f, m.vertexCount * c.width);
        if (m.has(STREAM_BONEIDX))  r.readVec(m.boneIndex,  m.vertexCount * 4);
        if (m.has(STREAM_BONEWGT))  r.readVec(m.weight,     m.vertexCount * 4);
        if (m.has(STREAM_BONEIDX2)) r.readVec(m.boneIndex2, m.vertexCount * 4);
        if (m.has(STREAM_BONEWGT2)) r.readVec(m.weight2,    m.vertexCount * 4);
        r.readVec(m.indices,  static_cast<size_t>(m.triangleCount) * 3);
        r.readVec(m.boneRefs, refCount);
        r.align4();
    }
    if (r.bad) { err = "Datei endet mitten in den Daten"; return false; }
    return true;
}

// ------------------------------------------------------------ Animation --

bool readAnim(const std::vector<uint8_t>& data, Anim& out, std::string& err) {
    Reader r(data);
    char magic[8] = {};
    r.read(magic, 8);
    if (r.bad || std::memcmp(magic, "FBANIM01", 8) != 0) {
        err = "keine .fbanim-Datei"; return false;
    }
    uint32_t version    = r.get<uint32_t>();
    uint32_t headerLen  = r.get<uint32_t>();
    uint32_t chanCount  = r.get<uint32_t>();
    uint32_t keyCount   = r.get<uint32_t>();
    uint32_t boneCount  = r.get<uint32_t>();
    uint32_t strBytes   = r.get<uint32_t>();
    out.duration        = r.get<float>();
    out.endFrame        = r.get<int32_t>();
    uint32_t flags      = r.get<uint32_t>();
    uint32_t nameOff    = r.get<uint32_t>();
    uint32_t codecOff   = r.get<uint32_t>();
    out.fps             = r.get<float>();
    out.trajJointIndex  = r.get<int32_t>();
    if (version != 1) { err = "Fassung " + std::to_string(version) + ", erwartet 1"; return false; }
    if (r.bad || headerLen < 64 || headerLen > data.size()) { err = "Kopf unvollstaendig"; return false; }

    r.p = headerLen;
    if (r.p + strBytes > data.size()) { err = "Stringtabelle passt nicht in die Datei"; return false; }
    Strings s{ data.data() + r.p, strBytes };
    r.p += strBytes;

    out.name     = s.get(nameOff);
    out.codec    = s.get(codecOff);
    out.additive = (flags & 1) != 0;

    r.readVec(out.times, keyCount);
    r.align4();
    readBones(r, s, out.bones, boneCount);

    out.channels.resize(chanCount);
    for (uint32_t i = 0; i < chanCount && !r.bad; ++i) {
        Channel& c = out.channels[i];
        c.name       = s.get(r.get<uint32_t>());
        c.kind       = static_cast<char>(r.get<uint32_t>());
        c.components = r.get<uint32_t>();
        c.constant   = r.get<uint32_t>() != 0;
        if (r.bad) break;
        const size_t saetze = c.constant ? 1u : keyCount;
        r.readVec(c.values, saetze * c.components);
    }
    if (r.bad) { err = "Datei endet mitten in den Daten"; return false; }
    return true;
}

// ------------------------------------------------------------ Textfassung --

std::string dumpText(const Model& m, int decimals) {
    std::string o;
    o.reserve(1u << 20);
    o += "QUELLE " + m.source + "\n";
    o += "SKELETT " + m.skeleton + " bones=";
    appendInt(o, static_cast<long long>(m.bones.size()));
    o += "\n";
    for (size_t i = 0; i < m.bones.size(); ++i) {
        const Bone& b = m.bones[i];
        o += "BONE "; appendInt(o, static_cast<long long>(i));
        o += " " + b.name + " eltern="; appendInt(o, b.parent);
        o += " typ="; appendInt(o, b.type);
        for (int k = 0; k < 12; ++k) { o += ' '; appendFloat(o, b.rest[k], decimals); }
        o += "\n";
    }
    for (size_t i = 0; i < m.materials.size(); ++i) {
        o += "MATERIAL "; appendInt(o, static_cast<long long>(i));
        o += " " + m.materials[i].name + " ";
        for (size_t k = 0; k < m.materials[i].textures.size(); ++k) {
            if (k) o += ',';
            o += m.materials[i].textures[k];
        }
        o += "\n";
    }
    for (size_t mi = 0; mi < m.meshes.size(); ++mi) {
        const Mesh& s = m.meshes[mi];
        o += "MESH "; appendInt(o, static_cast<long long>(mi));
        o += " " + s.name + " lod="; appendInt(o, s.lod);
        o += " material="; appendInt(o, s.material);
        o += " verts=";    appendInt(o, s.vertexCount);
        o += " tris=";     appendInt(o, s.triangleCount);
        o += " bonerefs="; appendInt(o, static_cast<long long>(s.boneRefs.size()));
        o += " tiefe=";    appendInt(o, s.depth);
        o += "\n";
        if (!s.boneRefs.empty()) {
            o += "  BONEREFS";
            for (uint16_t x : s.boneRefs) { o += ' '; appendInt(o, x); }
            o += "\n";
        }
        for (uint32_t vi = 0; vi < s.vertexCount; ++vi) {
            o += "  V "; appendInt(o, vi);
            struct Spalte { const char* name; const std::vector<float>* f; size_t width; };
            const Spalte spalten[] = {
                { "pos",     &s.pos,     3 }, { "normal", &s.normal, 3 },
                { "tangent", &s.tangent, 4 }, { "uv0",    &s.uv0,    2 },
                { "uv1",     &s.uv1,     2 }, { "farbe",  &s.color,  4 },
                { "boneWgt", &s.weight,  4 }, { "boneWgt2", &s.weight2, 4 },
            };
            for (const Spalte& c : spalten) {
                if (c.f->empty()) continue;
                o += ' '; o += c.name; o += '=';
                appendRow(o, *c.f, vi, c.width, decimals, ',');
            }
            struct IdxSpalte { const char* name; const std::vector<uint16_t>* v; };
            const IdxSpalte idxSpalten[] = {
                { "boneIdx", &s.boneIndex }, { "boneIdx2", &s.boneIndex2 },
            };
            for (const IdxSpalte& c : idxSpalten) {
                if (c.v->empty()) continue;
                o += ' '; o += c.name; o += '=';
                for (int k = 0; k < 4; ++k) {
                    if (k) o += ',';
                    appendInt(o, (*c.v)[vi * 4 + k]);
                }
            }
            o += "\n";
        }
        for (uint32_t ti = 0; ti < s.triangleCount; ++ti) {
            o += "  T "; appendInt(o, ti);
            for (int k = 0; k < 3; ++k) { o += ' '; appendInt(o, s.indices[ti * 3 + k]); }
            o += "\n";
        }
    }
    return o;
}

std::string dumpText(const Anim& a, int decimals) {
    std::string o;
    o.reserve(1u << 20);
    std::string codec = a.codec;
    while (!codec.empty() && (codec.back() == ' ' || codec.back() == '\t')) codec.pop_back();
    size_t vorn = codec.find_first_not_of(" \t");
    if (vorn == std::string::npos) codec.clear();
    else codec = codec.substr(vorn);

    o += "CLIP " + a.name + " codec=" + codec + " dauer=";
    appendFloat(o, a.duration, decimals);
    o += " endFrame="; appendInt(o, a.endFrame);
    o += " additive="; appendInt(o, a.additive ? 1 : 0);
    o += " fps=";      appendFloat(o, a.fps, decimals);
    o += " traj=";     appendInt(o, a.trajJointIndex);
    o += "\n";
    o += "KEYS "; appendInt(o, static_cast<long long>(a.times.size()));
    for (float t : a.times) { o += ' '; appendFloat(o, t, decimals); }
    o += "\n";
    for (size_t i = 0; i < a.bones.size(); ++i) {
        const Bone& b = a.bones[i];
        o += "BONE "; appendInt(o, static_cast<long long>(i));
        o += " " + b.name + " eltern="; appendInt(o, b.parent);
        o += " typ="; appendInt(o, b.type);
        for (int k = 0; k < 12; ++k) { o += ' '; appendFloat(o, b.rest[k], decimals); }
        o += "\n";
    }
    // Python gibt die Kanaele nach Namen sortiert aus (sorted()), damit die
    // Reihenfolge nicht von der Schreibreihenfolge abhaengt.
    std::vector<const Channel*> sortiert;
    sortiert.reserve(a.channels.size());
    for (const Channel& c : a.channels) sortiert.push_back(&c);
    for (size_t i = 1; i < sortiert.size(); ++i) {           // Einfuegesortierung
        const Channel* cur = sortiert[i];
        size_t j = i;
        while (j > 0 && sortiert[j - 1]->name > cur->name) { sortiert[j] = sortiert[j - 1]; --j; }
        sortiert[j] = cur;
    }
    for (const Channel* c : sortiert) {
        if (c->constant) {
            o += "KANAL " + c->name + " art="; o += c->kind; o += " konstant ";
            for (uint32_t k = 0; k < c->components; ++k) {
                if (k) o += ',';
                appendFloat(o, k < c->values.size() ? c->values[k] : 0.0f, decimals);
            }
            o += "\n";
        } else {
            const size_t keys = c->components ? c->values.size() / c->components : 0;
            o += "KANAL " + c->name + " art="; o += c->kind; o += " keys=";
            appendInt(o, static_cast<long long>(keys));
            o += "\n";
            for (size_t i = 0; i < keys; ++i) {
                o += "  K "; appendInt(o, static_cast<long long>(i)); o += ' ';
                appendRow(o, c->values, i, c->components, decimals, ',');
                o += "\n";
            }
        }
    }
    return o;
}

} // namespace fb
