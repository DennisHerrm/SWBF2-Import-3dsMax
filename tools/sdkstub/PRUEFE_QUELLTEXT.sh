#!/bin/sh
# ============================================================
#  Vorabpruefung ohne Windows und ohne Max - vier Teile:
#
#  1. Plugin-Quelltext gegen die SDK-Attrappe, fuer JEDEN Jahrgang
#     (MAX_RELEASE 18000..29000). GetMasterScale ist durchgerutscht,
#     weil nur ein Zweig geprueft wurde.
#  2. Das Figurenfenster mit MinGW gegen ECHTE Windows-Header
#     (tools/PRUEFE_FENSTER.sh). Die Attrappe hat eigene Windows-Typen
#     und kann reines Win32 nicht pruefen.
#  3. Ein echter CMake-Bau der Werkzeuge ("LANGUAGES CXX" ohne C hat
#     einmal .c-Dateien still uebergangen - das faengt nur ein echter Bau).
#  4. Die Werkzeuge mit AddressSanitizer und UBSan, an echten Dateien,
#     wenn welche angegeben sind:
#        SWBF2_PROBE_INDEX=<index.fbidx>  SWBF2_PROBE_FIGUREN=<figuren.txt>
#        SWBF2_PROBE_MODELL=<x.fbmodel>
# ============================================================
cd "$(dirname "$0")/../.." || exit 1
FEHLER=0
for R in 18000 19000 20000 21000 22000 23000 24000 25000 26000 27000 28000 29000
do
  JAHR=$((2016 + (R - 18000) / 1000))
  # -Wshadow -Werror=shadow: ein lokales "ok" verdeckt MAXScripts globales ok
  # (MSVC C4459, 0.45.0) - die Attrappe hat es, hier faellt es auf.
  if g++ -std=c++17 -Wall -Wextra -Wshadow -Werror=shadow -D_WIN32 -DMAX_RELEASE=$R \
        -Isrc -Itools/sdkstub -include tools/sdkstub/win_shim.h \
        -fsyntax-only src/swbf2import_max.cpp src/swbf2import_dll.cpp 2>/tmp/stubfehler.txt
  then
    printf "  Max %d  ok\n" "$JAHR"
    grep -q "warning" /tmp/stubfehler.txt && grep "warning" /tmp/stubfehler.txt | head -5
  else
    printf "  Max %d  FEHLER\n" "$JAHR"
    head -5 /tmp/stubfehler.txt
    FEHLER=1
  fi
done
[ "$FEHLER" = "0" ] || exit 1
# fbdatei OHNE _WIN32: die Attrappe hat keine windows.h.
g++ -std=c++17 -Wall -Wextra -Isrc -c src/fbdatei.cpp -o /tmp/fbdatei_stub.o || exit 1
g++ -std=c++17 -Wall -Wextra -Isrc -c src/fbbeipack.cpp -o /tmp/fbbeipack_stub.o || exit 1
g++ -std=c++17 -Wall -Wextra -D_WIN32 \
    -Isrc -Itools/sdkstub -include tools/sdkstub/win_shim.h \
    src/fbdump.cpp src/swbf2import_max.cpp src/swbf2import_dll.cpp \
    tools/sdkstub/stub_main.cpp /tmp/fbdatei_stub.o /tmp/fbbeipack_stub.o -o /tmp/swbf2_stubtest || exit 1
echo "Alle Jahrgaenge uebersetzt und gebunden (gegen die Attrappe)."

# Die Fassung muss an allen Stellen gleich sein - sonst sucht man Fehler in
# einer alten .dlu (XFBIN 1.7.1: der Kopf stand wochenlang auf alter Nummer).
V_H=$(grep -o 'VERSION_STR  _T("[0-9.]*")' src/swbf2import.h | grep -o '[0-9][0-9.]*')
V_RC=$(grep -o 'VALUE "FileVersion", "[0-9.]*"' src/swbf2import.rc | grep -o '[0-9][0-9.]*$\|[0-9][0-9.]*"$' | tr -d '"')
V_PKG=$(grep -o 'AppVersion="[0-9.]*"' package/SWBF2Import/PackageContents.xml | grep -o '[0-9][0-9.]*')
V_MCR=$(grep -o 'sErwartet = "[0-9.]*"' scripts/EAfrontImport.mcr | grep -o '[0-9][0-9.]*')
V_PROBE=$(grep -o 'b.version = L"[0-9.]*"' tools/fenster_probe.cpp | grep -o '[0-9][0-9.]*')
# 1.08.0: "08" waere im .rc eine Oktalzahl ("digit exceeds base"), deshalb
# steht dort 1,8,0,0. Beim Vergleich fuehrende Nullen entfernen.
V_FV="$(echo "$V_H" | sed 's/\.0*\([0-9]\)/.\1/g' | tr . ,),0"
if [ "$V_H" = "$V_RC" ] && [ "$V_H" = "$V_PKG" ] && [ "$V_H" = "$V_MCR" ] && [ "$V_H" = "$V_PROBE" ] \
   && grep -q "FILEVERSION $V_FV" src/swbf2import.rc; then
  echo "Fassung $V_H an allen fuenf Stellen gleich"
else
  echo "FASSUNG UNEINHEITLICH: Kopf $V_H, rc $V_RC, Paket $V_PKG, Skript $V_MCR, Probe $V_PROBE"; exit 1
fi

# ---- 2. Fenster mit MinGW ------------------------------------------------
sh tools/PRUEFE_FENSTER.sh /tmp/fensterbau || exit 1

# ---- 3. CMake-Bau der Werkzeuge ------------------------------------------
if command -v cmake >/dev/null 2>&1; then
  BAU=$(mktemp -d)
  if cmake -S . -B "$BAU" -DSWBF2_BUILD_PLUGIN=OFF -DSWBF2_BUILD_TOOL=ON >/tmp/cmake_cfg.log 2>&1 \
     && cmake --build "$BAU" -j4 >/tmp/cmake_bau.log 2>&1; then
    echo "CMake-Bau der Werkzeuge: durchgelaufen"
    grep -i "warning" /tmp/cmake_bau.log | grep -v "vendor/" | head -5
  else
    echo "CMAKE-BAU FEHLGESCHLAGEN:"
    tail -20 /tmp/cmake_cfg.log /tmp/cmake_bau.log
    rm -rf "$BAU"
    exit 1
  fi
  rm -rf "$BAU"
else
  echo "Kein cmake da - der echte Bau wurde uebersprungen."
fi

# ---- 4. Sanitizer an echten Dateien ---------------------------------------
SAN=/tmp/swbf2_san
mkdir -p "$SAN"
SF="-std=c++17 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
SF="$SF -D_GLIBCXX_ASSERTIONS -Wall -Wextra -Isrc -Ivendor/lz4 -Ivendor/miniz -Ivendor/zstd -Ivendor/bcdec"
for q in vendor/lz4/lz4.c vendor/miniz/miniz.c vendor/miniz/miniz_tinfl.c vendor/miniz/miniz_tdef.c \
         vendor/miniz/miniz_zip.c vendor/zstd/zstddeclib.c; do
  gcc -O1 -w -Ivendor/miniz -Ivendor/zstd -c "$q" -o "$SAN/$(basename "$q" .c).o" || exit 1
done
KERN="src/fbcas.cpp src/fbdb.cpp src/fbgame.cpp src/fbbundle.cpp src/fbindex.cpp src/fbmeshset.cpp
      src/fbebx.cpp src/fbgd.cpp src/fbfigur.cpp src/fbdump.cpp src/fbdatei.cpp src/fbauswahl.cpp
      src/fbbeipack.cpp src/fbtextur.cpp src/fbmaterial.cpp src/fbgdwerte.cpp src/fbanim.cpp src/fbgesicht.cpp src/fbzuordnung.cpp src/fbfahrzeug.cpp src/fbteile.cpp"
g++ $SF $KERN tools/castool.cpp tools/codeclab.cpp tools/bericht.cpp "$SAN"/*.o -o "$SAN/castool" || exit 1
g++ $SF src/fbdump.cpp src/fbdatei.cpp tools/fbdumptool.cpp -o "$SAN/fbdump" || exit 1
echo "Sanitizer-Bau (ASan + UBSan): durchgelaufen"
g++ $SF $KERN tools/meshtool.cpp "$SAN"/*.o -o "$SAN/meshtool" || exit 1
python3 tools/tests/gd_testbank.py "$SAN/gd_testbank.res" >/dev/null || exit 1
if [ -n "$SWBF2_PROBE_FBTOOLS" ] && [ -f "$SWBF2_PROBE_FBTOOLS/fb_gd.py" ]; then
  python3 - "$SWBF2_PROBE_FBTOOLS" "$SAN" <<'PYEND' || exit 1
import sys, subprocess
sys.path.insert(0, sys.argv[1]); sys.path.insert(0, "tools")
import fb_gd as G, VERGLEICHE_GD as V
bank = sys.argv[2] + "/gd_testbank.res"
d = open(bank, "rb").read()
a = V.text_von(G.read_bank(d, mit_werten=False)) == subprocess.run([sys.argv[2] + "/meshtool", "--gd", bank], capture_output=True).stdout.decode()
soll = V.werte_text(G.read_bank(d, mit_werten=False), d)
w = soll == subprocess.run([sys.argv[2] + "/meshtool", "--gd-werte", bank], capture_output=True).stdout.decode()
print("GD-Testbank gegen fbtools: Aufbau %s, Werte %s (%d Zeilen)" % ("gleich" if a else "ABWEICHUNG", "gleich" if w else "ABWEICHUNG", soll.count("\n")))
sys.exit(0 if a and w else 1)
PYEND
fi
if [ -n "$SWBF2_PROBE_INDEX" ] && [ -f "$SWBF2_PROBE_INDEX" ]; then
  "$SAN/castool" --ablage "$SWBF2_PROBE_INDEX" > "$SAN/ablage.txt" || { echo "castool --ablage FEHLER"; exit 1; }
  grep "^INDEX\|^FIGUREN" "$SAN/ablage.txt"
  if [ -n "$SWBF2_PROBE_FIGUREN" ] && [ -f "$SWBF2_PROBE_FIGUREN" ]; then
    tr -d '\r' < "$SWBF2_PROBE_FIGUREN" | grep "^F " > "$SAN/alt_F.txt"
    grep "^F " "$SAN/ablage.txt" > "$SAN/neu_F.txt"
    if cmp -s "$SAN/alt_F.txt" "$SAN/neu_F.txt"; then
      echo "Figurenliste: $(wc -l < "$SAN/neu_F.txt") Zeilen, zeichengleich mit $(basename "$SWBF2_PROBE_FIGUREN")"
    else
      echo "Figurenliste WEICHT AB:"; diff "$SAN/alt_F.txt" "$SAN/neu_F.txt" | head -10; exit 1
    fi
  fi
fi
if [ -n "$SWBF2_PROBE_MODELL" ] && [ -f "$SWBF2_PROBE_MODELL" ]; then
  "$SAN/fbdump" "$SWBF2_PROBE_MODELL" > "$SAN/modell.txt" || { echo "fbdump FEHLER"; exit 1; }
  echo "fbdump unter Sanitizern: $(wc -l < "$SAN/modell.txt") Zeilen aus $(basename "$SWBF2_PROBE_MODELL"), ohne Befund"
fi
