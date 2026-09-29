#!/usr/bin/env python3
"""VERGLEICHE_ANIM.py - entpackte Clips aus C++ gegen fbtools.

fbtools legt fuer die Clips seiner drei Referenzbaenke .fbanim-Dateien ab
(ERGEBNIS, animation_<clip>.fbanim). castool --anim schreibt dieselben Clips
aus C++. Verglichen werden die KURVEN (ueber fb_animdump.lies_bin beider
Dateien): Kanalnamen, Art, konstant, Keyzeiten und jeder Wert. Die Bytes
koennen sich unterscheiden (fbtools legt ein Skelett bei), die Kurven nicht.

    python VERGLEICHE_ANIM.py <ERGEBNIS> <fbtools> <spielordner> <index.fbidx> <castool>
"""
import glob
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def main(argv):
    try:
        import protokoll
        protokoll.anschalten()
    except Exception:
        pass
    if len(argv) < 6:
        print(__doc__)
        return 0
    ergebnis, fbt, spiel, cache, castool = argv[1:6]
    sys.path.insert(0, fbt)
    import fb_animdump as A
    dateien = sorted(glob.glob(os.path.join(ergebnis, "**", "animation_*.fbanim"), recursive=True))
    print("\nEntpackte Clips gegen fbtools:")
    if not dateien:
        print("  keine .fbanim von fbtools gefunden (%s)" % ergebnis)
        return 0
    namen = [os.path.basename(p)[len("animation_"):-len(".fbanim")] + ".chan" for p in dateien]
    tmp = tempfile.mkdtemp(prefix="swbf2anim_")
    liste = os.path.join(tmp, "liste.txt")
    with open(liste, "w") as f:
        f.write("\n".join(namen) + "\n")
    lauf = subprocess.run([castool, "--anim", spiel, tmp, "--liste", liste, "--cache", cache],
                          capture_output=True)
    for z in lauf.stdout.decode("utf-8", "replace").splitlines():
        if z.startswith("ANIM Verzeichnis") or z.startswith("ANIM geschrieben"):
            print("  " + z)
    gleich = abweichend = offen = 0
    for p, n in zip(dateien, namen):
        q = os.path.join(tmp, os.path.basename(p))
        if not os.path.isfile(q):
            offen += 1
            continue
        a = A.lies_bin(p)
        b = A.lies_bin(q)
        fehler = None
        if [round(z, 3) for z in a["zeiten"]] != [round(z, 3) for z in b["zeiten"]]:
            fehler = "Keyzeiten %d gegen %d" % (len(a["zeiten"]), len(b["zeiten"]))
        elif list(a["kanaele"].keys()) != list(b["kanaele"].keys()):
            ka, kb = list(a["kanaele"].keys()), list(b["kanaele"].keys())
            i = next((i for i in range(min(len(ka), len(kb))) if ka[i] != kb[i]), min(len(ka), len(kb)))
            fehler = "Kanalnamen: %d gegen %d, erster Unterschied bei %d: %s / %s" % (
                len(ka), len(kb), i, ka[i] if i < len(ka) else "-", kb[i] if i < len(kb) else "-")
        else:
            for name, ka in a["kanaele"].items():
                kb = b["kanaele"][name]
                if ka["art"] != kb["art"] or ka["konstant"] != kb["konstant"]:
                    fehler = "Kanal %s: Art/konstant verschieden" % name
                    break
                wa = ka.get("wert") if ka["konstant"] else ka.get("werte")
                wb = kb.get("wert") if kb["konstant"] else kb.get("werte")
                if wa != wb:
                    fehler = "Kanal %s: Werte verschieden" % name
                    break
        if fehler is None:
            gleich += 1
        else:
            abweichend += 1
            if abweichend <= 5:
                print("  %s: %s" % (n, fehler))
    print("  %d Clips gleich, %d abweichend, %d noch nicht aus C++ (DCT/VBR)" % (gleich, abweichend, offen))
    return 0 if abweichend == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
