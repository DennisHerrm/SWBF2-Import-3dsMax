#!/usr/bin/env python3
"""VERGLEICHE_FIGUR.py - der ganze Weg am Stueck.

START.bat zieht Anakin direkt aus den Spieldateien und schreibt
figur_direkt.fbmodel. Hier wird die Datei BYTEWEISE gegen die gehalten, die
fbtools aus denselben Dateien geschrieben hat.

Das ist die schaerfste Probe im Projekt: sie deckt jede Schicht ab - von der
Entschleierung ueber Katalog, Entpacker, Manifest, Bundle und Index bis zu
MeshSet, Skelett und dem Schreiber.

Sie MUSS nach der Extraktion laufen, nicht davor. Genau daran ist der erste
Versuch vorbeigegangen: VERGLEICHE.py laeuft in Schritt 2, die Figur entsteht
erst in Schritt 3.
"""
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def main(argv):
    try:
        import protokoll
        protokoll.anschalten()
    except Exception:
        pass

    projekt = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    meins_pfad = argv[1] if len(argv) > 1 else os.path.join(projekt, "figur_direkt.fbmodel")
    fbtools = argv[2] if len(argv) > 2 else None
    if fbtools is None:
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "V", os.path.join(os.path.dirname(os.path.abspath(__file__)), "VERGLEICHE.py"))
        V = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(V)
        fbtools, _ = V.finde_fbtools(None)

    print("\nDer ganze Weg am Stueck (Spieldateien -> .fbmodel):")
    if not os.path.isfile(meins_pfad):
        print("  %s gibt es nicht - wurde die Figur ueberhaupt gezogen?" % meins_pfad)
        return 0
    if not fbtools:
        print("  fbtools nicht gefunden, kein Vergleich moeglich.")
        return 0

    meins = open(meins_pfad, "rb").read()
    kandidaten = [m for m in glob.glob(os.path.join(fbtools, "**", "*.fbmodel"), recursive=True)
                  if "vur_anakin_01_bpb" in os.path.basename(m)
                  and os.sep + "probe" + os.sep not in m]
    if not kandidaten:
        print("  Keine Referenz von fbtools gefunden (vur_anakin_01_bpb.fbmodel).")
        print("  Einmal fbtools laufen lassen, dann steht sie unter figur\\.")
        return 0

    # Die groesste nehmen: die kleine Probefassung stammt aus dem groebsten LOD.
    ref = max(kandidaten, key=os.path.getsize)
    seins = open(ref, "rb").read()
    fassung = struct.unpack_from("<I", seins, 8)[0] if len(seins) > 12 else 0
    if fassung != 2:
        print("  Referenz ist Fassung %d - kein Vergleich moeglich." % fassung)
        return 0
    if meins == seins:
        print("  %d Byte, BYTEWEISE GLEICH mit %s" % (len(meins), os.path.basename(ref)))
        return 0

    stelle = next((k for k in range(min(len(meins), len(seins))) if meins[k] != seins[k]),
                  min(len(meins), len(seins)))
    print("  ABWEICHUNG ab Byte %d (%d gegen %d Byte)" % (stelle, len(meins), len(seins)))
    print("  Referenz: %s" % ref)

    # Ein paar Kennzahlen aus beiden Koepfen, damit die Ursache eingrenzbar ist.
    def kopf(d):
        if len(d) < 64:
            return None
        m, v, kb, nm, nb, nmat, ns = struct.unpack_from("<8sIIIIII", d, 0)
        return dict(meshes=nm, bones=nb, materialien=nmat, strings=ns)
    a, b = kopf(meins), kopf(seins)
    if a and b:
        print("  C++    : %d Meshes, %d Bones, %d Materialien, %d Byte Strings"
              % (a["meshes"], a["bones"], a["materialien"], a["strings"]))
        print("  fbtools: %d Meshes, %d Bones, %d Materialien, %d Byte Strings"
              % (b["meshes"], b["bones"], b["materialien"], b["strings"]))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
