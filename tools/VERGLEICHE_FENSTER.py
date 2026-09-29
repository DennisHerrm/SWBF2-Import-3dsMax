#!/usr/bin/env python3
"""VERGLEICHE_FENSTER.py - der Weg ueber das Figurenfenster.

Das Plugin legt jede Figur, die es aus dem Spiel holt, als .fbmodel unter
%LOCALAPPDATA%\\SWBF2Import\\figuren ab und importiert genau diese Datei.
Hier wird sie BYTEWEISE gegen figur_direkt.fbmodel gehalten, die castool aus
denselben Spieldateien schreibt. Gleich heisst: das Plugin geht im
Max-Prozess denselben Weg wie das Werkzeug, Schicht fuer Schicht.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def main(argv):
    try:
        import protokoll
        protokoll.anschalten()
    except Exception:
        pass
    projekt = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    werkzeug = argv[1] if len(argv) > 1 else os.path.join(projekt, "figur_direkt.fbmodel")
    ablage = os.path.join(os.environ.get("LOCALAPPDATA", ""), "SWBF2Import", "figuren")
    fenster = os.path.join(ablage, "vur_anakin_01_bpb.fbmodel")
    print()
    print("Figurenfenster gegen castool (vur_anakin_01_bpb):")
    if not os.path.isfile(werkzeug):
        print("  figur_direkt.fbmodel fehlt - die Probe braucht das Spiel")
        return 0
    if not os.path.isfile(fenster):
        print("  noch kein Import von vur_anakin_01_bpb ueber das Fenster")
        return 0
    a = open(werkzeug, "rb").read()
    b = open(fenster, "rb").read()
    if a == b:
        print("  %d Byte, BYTEWEISE GLEICH - das Plugin geht denselben Weg wie castool" % len(a))
        return 0
    n = min(len(a), len(b))
    i = next((k for k in range(n) if a[k] != b[k]), n)
    print("  ABWEICHUNG: castool %d Byte, Fenster %d Byte, erste Abweichung bei Byte %d" % (len(a), len(b), i))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
