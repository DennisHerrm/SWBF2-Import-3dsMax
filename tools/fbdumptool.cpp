// fbdumptool.cpp - liest .fbmodel und .fbanim und schreibt die Textfassung.
//
// Das ist die Gegenprobe fuer den C++-Leser: seine Ausgabe muss zeichengleich
// sein mit der aus Python (fb_model.dump_text / fb_animdump.dump_text). Dasselbe
// Muster wie beim XFBIN-Importer, wo alle fuenf Dumps auf 0 Abweichungen
// gebracht wurden.
//
//   fbdump <datei> [ausgabe.txt]
//
// Die Dateiendung entscheidet nicht - es zaehlt die Kennung am Dateianfang.
#include "fbdump.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    // stdout steht unter Windows im Textmodus: jedes \n wird beim Schreiben zu
    // \r\n. Die Textfassung aus Python hat aber reine \n. Ohne diese Zeile
    // unterscheiden sich ALLE Zeilen um genau ein Zeichen, und der Vergleich
    // meldet Abweichungen, wo keine sind.
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 2) {
        std::fprintf(stderr,
                     "fbdump - Textfassung eines fbtools-Dumps\n"
                     "  fbdump <datei.fbmodel|datei.fbanim> [ausgabe.txt]\n");
        return 2;
    }
    std::vector<uint8_t> daten;
    std::string fehler;
    if (!fb::readFile(argv[1], daten, fehler)) {
        std::fprintf(stderr, "%s\n", fehler.c_str());
        return 1;
    }
    if (daten.size() < 8) {
        std::fprintf(stderr, "Datei zu kurz\n");
        return 1;
    }

    // Die Zeit mitmessen: sie beantwortet die Frage, ob sich Threads lohnen,
    // ohne dass jemand raten muss.
    const auto t0 = std::chrono::steady_clock::now();
    std::string text;
    if (std::string(reinterpret_cast<const char*>(daten.data()), 8) == "FBMODEL1") {
        fb::Model m;
        if (!fb::readModel(daten, m, fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        text = fb::dumpText(m);
        size_t verts = 0;
        for (const fb::Mesh& s2 : m.meshes) verts += s2.vertexCount;
        std::fprintf(stderr, "%zu Meshes, %zu Vertices, %zu Bones, %zu Materialien\n",
                     m.meshes.size(), verts, m.bones.size(), m.materials.size());
    } else if (std::string(reinterpret_cast<const char*>(daten.data()), 8) == "FBANIM01") {
        fb::Anim a;
        if (!fb::readAnim(daten, a, fehler)) {
            std::fprintf(stderr, "%s\n", fehler.c_str());
            return 1;
        }
        text = fb::dumpText(a);
        std::fprintf(stderr, "%zu Kanaele, %zu Keys, %zu Bones\n",
                     a.channels.size(), a.times.size(), a.bones.size());
    } else {
        std::fprintf(stderr, "unbekannte Kennung - weder FBMODEL1 noch FBANIM01\n");
        return 1;
    }

    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "gelesen und aufbereitet in %.1f ms (%zu Byte)\n",
                 ms, daten.size());

    if (argc >= 3) {
        // "wb", damit unter Windows kein \r dazukommt - sonst ist der
        // Zeichenvergleich mit der Python-Ausgabe wertlos.
        std::FILE* f = std::fopen(argv[2], "wb");
        if (!f) {
            std::fprintf(stderr, "kann %s nicht schreiben\n", argv[2]);
            return 1;
        }
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    } else {
        std::fwrite(text.data(), 1, text.size(), stdout);
    }
    return 0;
}
