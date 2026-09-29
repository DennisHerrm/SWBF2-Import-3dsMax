#!/usr/bin/env python3
"""VERGLEICHE_EBX.py - den C++-EBX-Leser gegen fbtools halten.

Geprueft wird an echten Spieldaten: den .ebx-Dateien, die fbtools bei einem
Lauf unter ERGEBNIS\\dump\\ sichert (darunter das Skelett Walrus_HumanMale mit
seinen 248 Bones).
"""
import glob
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def finde(name, *orte):
    for o in orte:
        p = os.path.join(o, name)
        if os.path.isfile(p):
            return p
    return None


def text_von(e, E, grenze=0):
    z = ["EBX %s FASSUNG %d OBJEKTE %d" % (e.file_guid, e.version, len(e.objects))]

    def rek(w, name, tiefe):
        ein = "  " * tiefe
        if isinstance(w, dict) and "__type" in w:
            felder = [(k, v) for k, v in w.items() if not k.startswith("__")]
            z.append("%sOBJ %s %s {%d}" % (ein, name, w["__type"], len(felder)))
            for k, v in felder:
                rek(v, k, tiefe + 1)
        elif isinstance(w, dict) and "__ref" in w:
            z.append("%sREF %s %d" % (ein, name, w["__ref"]))
        elif isinstance(w, dict) and "__import" in w:
            z.append("%sIMPORT %s %s %s" % (ein, name, w["__import"], w["__class"]))
        elif isinstance(w, dict) and "__boxed" in w:
            z.append("%sBOXED %s %d" % (ein, name, w["__boxed"]))
        elif isinstance(w, list):
            z.append("%sLISTE %s [%d]" % (ein, name, len(w)))
            for s in w:
                rek(s, "", tiefe + 1)
        elif isinstance(w, bool):
            z.append("%sBOOL %s %d" % (ein, name, 1 if w else 0))
        elif isinstance(w, int):
            z.append("%sGANZ %s %d" % (ein, name, w))
        elif isinstance(w, float):
            z.append("%sGLEIT %s %.6f" % (ein, name, w))
        elif w is None:
            z.append("%sNICHTS %s" % (ein, name))
        else:
            z.append("%sTEXT %s %s" % (ein, name, w))

    for i, o in enumerate(e.objects):
        if grenze and i >= grenze:
            break
        z.append("-- %d %s %s" % (i, o["__type"], o["__guid"] or "-"))
        for k, v in o.items():
            if k.startswith("__"):
                continue
            rek(v, k, 1)
    return "\n".join(z) + "\n"


def main(argv):
    try:
        import protokoll
        protokoll.anschalten()
    except Exception:
        pass

    hier = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    orte = [os.path.join(hier, "build_tool", "bin", "Release"),
            os.path.join(hier, "build_tool", "bin"),
            os.path.join(hier, "build", "bin"), "/tmp/probebau/bin"]
    exe = finde("meshtool.exe", *orte) or finde("meshtool", *orte)
    if exe is None:
        print("meshtool nicht gefunden - erst START.bat laufen lassen.")
        return 2

    ordner = argv[1] if len(argv) > 1 else None
    fbtools = argv[2] if len(argv) > 2 else None
    if fbtools is None:
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "V", os.path.join(os.path.dirname(os.path.abspath(__file__)), "VERGLEICHE.py"))
        V = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(V)
        fbtools, _ = V.finde_fbtools(None)
    if not fbtools:
        print("fbtools nicht gefunden.")
        return 2
    sys.path.insert(0, fbtools)
    import fb_ebx as E

    def suche(wurzel):
        return sorted(glob.glob(os.path.join(wurzel, "**", "*.ebx"), recursive=True))

    dateien = []
    if ordner and os.path.isdir(ordner):
        dateien = suche(ordner)
    if not dateien:
        for wurzel in (fbtools, os.path.dirname(os.path.abspath(fbtools))):
            dateien = suche(wurzel)
            if dateien:
                break
    if not dateien:
        print("Keine gesicherten EBX-Dateien gefunden (gesucht unter %s)." % fbtools)
        print("fbtools legt sie beim Lauf unter ERGEBNIS\\dump ab.")
        return 0

    print("meshtool: %s" % exe)
    print("%d EBX-Dateien zu pruefen.\n" % len(dateien))
    gleich = ungleich = 0
    for pfad in dateien:
        name = os.path.basename(pfad)
        try:
            e = E.EbxFile(open(pfad, "rb").read())
        except Exception as ex:
            print("  %-40s Python kommt nicht heran: %s" % (name[:40], ex))
            ungleich += 1
            continue
        soll = text_von(e, E)
        ist = subprocess.run([exe, "--ebx", pfad], capture_output=True).stdout
        ist = ist.decode("utf-8", "replace").replace("\r\n", "\n")
        if ist == soll:
            print("  %-40s zeichengleich, %d Zeilen" % (name[:40], soll.count("\n")))
            gleich += 1
        else:
            ungleich += 1
            a, b = soll.splitlines(), ist.splitlines()
            print("  %-40s ABWEICHUNG (%d gegen %d Zeilen)" % (name[:40], len(a), len(b)))
            for k in range(min(len(a), len(b))):
                if a[k] != b[k]:
                    print("     Zeile %d" % (k + 1))
                    print("     Python: %s" % a[k][:160])
                    print("     C++   : %s" % b[k][:160])
                    break

        # ---- und das Skelett daraus ----------------------------------
        sk = E.skeleton_info(e)
        if sk:
            z = ["SKELETT %s BONES %d" % (sk["name"], len(sk["names"]))]
            for i, n in enumerate(sk["names"]):
                h = sk["hierarchy"][i] if i < len(sk["hierarchy"]) else -1
                zeile = "B %d %s eltern=%d" % (i, n, h)
                lp = sk.get("LocalPose")
                if lp and i < len(lp):
                    for feld in ("right", "up", "forward", "trans"):
                        zeile += "".join(" %.6f" % v for v in lp[i][feld])
                z.append(zeile)
            soll_sk = "\n".join(z) + "\n"
            ist_sk = subprocess.run([exe, "--skelett", pfad], capture_output=True).stdout
            ist_sk = ist_sk.decode("utf-8", "replace").replace("\r\n", "\n")
            if ist_sk == soll_sk:
                print("  %-40s Skelett zeichengleich, %d Zeilen"
                      % ("", soll_sk.count("\n")))
                gleich += 1
            else:
                ungleich += 1
                a, b = soll_sk.splitlines(), ist_sk.splitlines()
                print("  %-40s Skelett ABWEICHUNG (%d gegen %d Zeilen)" % ("", len(a), len(b)))
                for k in range(min(len(a), len(b))):
                    if a[k] != b[k]:
                        print("     Zeile %d" % (k + 1))
                        print("     Python: %s" % a[k][:160])
                        print("     C++   : %s" % b[k][:160])
                        break

    print("\n%d gleich, %d abweichend" % (gleich, ungleich))
    if ungleich == 0:
        print("Der C++-EBX-Leser liest genau dasselbe wie fbtools.")
    return 0 if ungleich == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
