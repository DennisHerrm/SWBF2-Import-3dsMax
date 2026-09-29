// ============================================================
//  fbbeipack.cpp - Beipackzettel schreiben und lesen.
// ============================================================
#include "fbbeipack.h"
#include "fbdatei.h"

#include <cstdlib>
#include <sstream>

namespace fbbeipack {

namespace {

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::vector<std::string> Teile(const std::string& z) {
    std::vector<std::string> t;
    std::string w;
    for (char c : z) {
        if (c == '\t') { t.push_back(w); w.clear(); } else if (c != '\r') { w += c; }
    }
    t.push_back(w);
    return t;
}

} // namespace

std::string ArtVon(const std::string& parameter) {
    std::string p = Klein(parameter);
    // 1.23.0: Fuehrende Unterstriche abschneiden. Der AT-TE hat seine Texturen
    // unter "_BaseColor" und "_Normal" - mit Unterstrich. Die Zuordnung kannte
    // nur "basecolor"/"normal", also galten sechs von sieben Materialien als
    // "sonst" und kamen in Max ohne Textur an, obwohl das Protokoll sie
    // richtig gefunden hatte.
    while (!p.empty() && (p[0] == '_' || p[0] == ' ')) p.erase(0, 1);
    if (p == "basecolor" || p == "cs" || p == "color" || p == "diffuse") return "farbe";
    // "NS" am Kopf: R und G um 127 wie jede Normale, B mit eigenem Signal
    // (Mittel 101) - gemessen in 0.35.0, deshalb ebenfalls Normale.
    if (p == "normal" || p == "nw" || p == "nm" || p == "ns") return "normal";
    return "sonst";
}

bool Schreibe(const std::string& utf8Pfad, const std::vector<MeshMaterial>& mm, std::string& fehler) {
    std::string s = "SWBF2MATERIAL 1\n";
    for (size_t i = 0; i < mm.size(); ++i) {
        const MeshMaterial& m = mm[i];
        s += "MESH\t" + std::to_string(i) + "\t" + m.meshAsset + "\t" + std::to_string(m.materialId) + "\t" +
             m.materialName + "\t" + m.weg + "\n";
        for (const Textur& t : m.texturen) {
            s += "TEX\t" + std::to_string(i) + "\t" + t.parameter + "\t" + t.art + "\t" + t.datei + "\t" + t.name +
                 "\t" + std::to_string(t.format) + "\t" + std::to_string(t.breite) + "x" + std::to_string(t.hoehe) + "\n";
        }
    }
    return fbdatei::SchreibeAlles(utf8Pfad, std::vector<uint8_t>(s.begin(), s.end()), fehler);
}

bool Lies(const std::string& utf8Pfad, std::vector<MeshMaterial>& mm, std::string& fehler) {
    mm.clear();
    std::vector<uint8_t> roh;
    if (!fbdatei::LiesAlles(utf8Pfad, roh, fehler)) return false;
    const std::string s(roh.begin(), roh.end());
    std::istringstream ein(s);
    std::string zeile;
    if (!std::getline(ein, zeile) || zeile.rfind("SWBF2MATERIAL 1", 0) != 0) {
        fehler = "kein Beipackzettel (Kopfzeile fehlt)";
        return false;
    }
    while (std::getline(ein, zeile)) {
        const std::vector<std::string> t = Teile(zeile);
        if (t.size() >= 6 && t[0] == "MESH") {
            const size_t i = static_cast<size_t>(std::strtoul(t[1].c_str(), nullptr, 10));
            if (i > 100000) continue;
            if (mm.size() <= i) mm.resize(i + 1);
            mm[i].meshAsset = t[2];
            mm[i].materialId = static_cast<int32_t>(std::strtol(t[3].c_str(), nullptr, 10));
            mm[i].materialName = t[4];
            mm[i].weg = t[5];
        } else if (t.size() >= 8 && t[0] == "TEX") {
            const size_t i = static_cast<size_t>(std::strtoul(t[1].c_str(), nullptr, 10));
            if (i > 100000) continue;
            if (mm.size() <= i) mm.resize(i + 1);
            Textur x;
            x.parameter = t[2];
            x.art = t[3];
            x.datei = t[4];
            x.name = t[5];
            x.format = static_cast<int32_t>(std::strtol(t[6].c_str(), nullptr, 10));
            const size_t xpos = t[7].find('x');
            if (xpos != std::string::npos) {
                x.breite = static_cast<uint32_t>(std::strtoul(t[7].c_str(), nullptr, 10));
                x.hoehe = static_cast<uint32_t>(std::strtoul(t[7].c_str() + xpos + 1, nullptr, 10));
            }
            mm[i].texturen.push_back(x);
        }
    }
    return true;
}

} // namespace fbbeipack
