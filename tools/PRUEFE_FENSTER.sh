#!/bin/sh
# ============================================================
#  Das Figurenfenster mit MinGW gegen ECHTE Windows-Header bauen.
#
#  Die SDK-Attrappe bringt eigene Windows-Typen mit und kann das
#  Fenster (reines Win32) nicht pruefen. Hier wird es deshalb mit
#  MinGW uebersetzt, zusammen mit der ganzen Containerschicht - und
#  als fenster_probe.exe gebunden, die das echte Fenster ohne Max
#  zeigt. Laeuft nur, wenn x86_64-w64-mingw32-g++-posix da ist.
# ============================================================
cd "$(dirname "$0")/.." || exit 1
CXX=x86_64-w64-mingw32-g++-posix
CC=x86_64-w64-mingw32-gcc-posix
RC=x86_64-w64-mingw32-windres
if ! command -v $CXX >/dev/null 2>&1; then echo "MinGW fehlt - Fensterprobe uebersprungen."; exit 0; fi
AUS=${1:-/tmp/fensterbau}
mkdir -p "$AUS"
FLAGS="-std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -Isrc -Ivendor/lz4 -Ivendor/miniz -Ivendor/zstd -Ivendor/bcdec"
FEHLER=0
for q in src/fbcas.cpp src/fbdb.cpp src/fbgame.cpp src/fbbundle.cpp src/fbindex.cpp src/fbmeshset.cpp \
         src/fbebx.cpp src/fbgd.cpp src/fbfigur.cpp src/fbdump.cpp src/fbdatei.cpp src/fbauswahl.cpp \
         src/fbbeipack.cpp src/fbtextur.cpp src/fbmaterial.cpp src/fbgdwerte.cpp src/fbanim.cpp src/fbgesicht.cpp src/fbzuordnung.cpp src/fbfahrzeug.cpp src/fbteile.cpp \
         src/swbf2import_fenster.cpp src/swbf2import_animfenster.cpp tools/fenster_probe.cpp; do
  o="$AUS/$(basename "$q" .cpp).o"
  $CXX $FLAGS -c "$q" -o "$o" 2>"$AUS/fehler.txt" || { echo "FEHLER in $q"; head -20 "$AUS/fehler.txt"; FEHLER=1; }
  grep -q "warning" "$AUS/fehler.txt" && { echo "Warnungen in $q:"; grep "warning" "$AUS/fehler.txt" | head -10; }
done
for q in vendor/lz4/lz4.c vendor/miniz/miniz.c vendor/miniz/miniz_tinfl.c vendor/miniz/miniz_tdef.c \
         vendor/miniz/miniz_zip.c vendor/zstd/zstddeclib.c; do
  $CC -O2 -w -Ivendor/miniz -Ivendor/zstd -c "$q" -o "$AUS/$(basename "$q" .c).o" || FEHLER=1
done
$RC -Isrc src/swbf2import.rc -O coff -o "$AUS/swbf2import_res.o" || FEHLER=1
[ "$FEHLER" = "0" ] || exit 1
$CXX -municode -mwindows -static -static-libgcc -static-libstdc++ -o "$AUS/fenster_probe.exe" "$AUS"/*.o \
     -lole32 -luuid -ldwmapi -luxtheme -lcomctl32 -lshell32 -lgdi32 -luser32 || exit 1
echo "Fenster mit MinGW gebaut: $AUS/fenster_probe.exe"
