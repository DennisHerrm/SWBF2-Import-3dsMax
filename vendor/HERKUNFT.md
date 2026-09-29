# Woher die Entpacker stammen

Der Container von Battlefront II packt seine Blöcke mit drei Verfahren. Wir
brauchen davon **nur das Entpacken**, nie das Packen — deshalb reicht jeweils
der Decoder.

| Ordner | Bibliothek | Fassung | Lizenz | Was daraus benutzt wird |
|---|---|---|---|---|
| `lz4/` | [lz4](https://github.com/lz4/lz4) | dev | BSD 2-Clause | `LZ4_decompress_safe` |
| `miniz/` | [miniz](https://github.com/richgel999/miniz) | 3.0.2 | MIT | `tinfl` / `mz_uncompress` für zlib-Blöcke |
| `zstd/` | [zstd](https://github.com/facebook/zstd) | 1.5.6 | BSD (wahlweise GPLv2) | Einzeldatei-**Decoder** `zstddeclib.c` |

`zstddeclib.c` ist nicht von Hand geschrieben, sondern mit dem offiziellen
Werkzeug aus dem zstd-Baum erzeugt:

    cd zstd/build/single_file_libs
    python3 combine.py -r ../../lib -x legacy/zstd_legacy.h -o zstddeclib.c zstddeclib-in.c

Alle drei Lizenzen sind freizügig und erlauben das Mitliefern im Quelltext und
im Binärpaket. Die Lizenztexte liegen im jeweiligen Ordner und müssen dort
bleiben.

`miniz_export.h` erzeugt miniz sonst über CMake; die Datei definiert nur ein
leeres `MINIZ_EXPORT` und liegt deshalb hier als Zweizeiler bei.
