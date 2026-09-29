#!/usr/bin/env python3
"""VERGLEICHE.py - der C++-Leser gegen die Python-Referenz.

Wird von START.bat aufgerufen und braucht normalerweise kein Argument: der
fbtools-Ordner wird selbst gesucht, und darin jede .fbmodel und .fbanim.

Fuer jede Datei laeuft dasselbe zweimal: einmal durch fb_model/fb_animdump in
Python, einmal durch fbdump.exe in C++. Beide schreiben dieselbe Textfassung.
Sind sie zeichengleich, liest C++ genau dasselbe wie Python - das ist die
Gegenprobe, die beim XFBIN-Importer fuenf Dumps auf 0 Abweichungen gebracht hat.

    VERGLEICHE.py                    alles selbst suchen
    VERGLEICHE.py datei.fbmodel      nur diese eine Datei
    VERGLEICHE.py C:\\fbtools-0.45.0  in diesem Ordner suchen
"""
import glob
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

HIER = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def finde_fbdump():
    for teil in ("build_tool/bin/Release", "build_tool/bin", "build_tool/Release",
                 "build/bin/Release", "build/bin", "build/Release", "build", "."):
        for name in ("fbdump.exe", "fbdump"):
            p = os.path.join(HIER, teil, name)
            if os.path.isfile(p):
                return p
    return None


def _version(pfad):
    """Aus "fbtools-0.45.0" die Zahl 45 machen, damit die neueste gewinnt."""
    zahlen = re.findall(r"(\d+)", os.path.basename(pfad.rstrip("\\/")))
    return tuple(int(x) for x in zahlen) or (0,)


def finde_fbtools(vorgabe=None):
    """Den fbtools-Ordner suchen - dort liegen fb_model.py und fb_animdump.py.

    Gesucht wird nach oben: neben dem Importerordner, eine und zwei Ebenen
    darueber, und in Downloads. Entpackte ZIPs haben oft einen Ordner IM Ordner
    ("fbtools-0.45.0\\fbtools-0.45.0"), deshalb wird eine Ebene tiefer
    mitgesucht. Gibt es mehrere, gewinnt die hoechste Fassung.
    """
    if vorgabe:
        for kandidat in (vorgabe, os.path.join(vorgabe, os.path.basename(vorgabe))):
            if os.path.isfile(os.path.join(kandidat, "fb_model.py")):
                return os.path.abspath(kandidat), []

    wurzeln = [HIER]
    p = HIER
    for _ in range(3):
        p = os.path.dirname(p)
        wurzeln.append(p)
    heim = os.path.expanduser("~")
    wurzeln += [os.path.join(heim, "Downloads"), os.path.join(heim, "Desktop"), heim]

    muster, gesehen = [], set()
    for w in wurzeln:
        if not w or w in gesehen:
            continue
        gesehen.add(w)
        muster += [os.path.join(w, "fbtools*"),
                   os.path.join(w, "fbtools*", "fbtools*"),
                   os.path.join(w, "*", "fbtools*")]

    treffer = []
    for m in muster:
        for k in glob.glob(m):
            if os.path.isfile(os.path.join(k, "fb_model.py")):
                treffer.append(os.path.abspath(k))
    treffer = sorted(set(treffer), key=_version, reverse=True)
    return (treffer[0] if treffer else None), sorted(gesehen)


def dumps_suchen(wurzel):
    aus = []
    for muster in ("figur/*.fbmodel", "figur/**/*.fbmodel",
                   "ERGEBNIS/*.fbanim", "ERGEBNIS/**/*.fbanim",
                   "**/*.fbmodel", "**/*.fbanim"):
        aus += glob.glob(os.path.join(wurzel, muster), recursive=True)
    gesehen, eindeutig = set(), []
    for p in aus:
        r = os.path.realpath(p)
        if r not in gesehen:
            gesehen.add(r)
            eindeutig.append(p)
    return eindeutig


def python_text(pfad, fbtools):
    if fbtools not in sys.path:
        sys.path.insert(0, fbtools)
    with open(pfad, "rb") as f:
        kennung = f.read(8)
    if kennung == b"FBMODEL1":
        import fb_model
        return fb_model.dump_text(fb_model.lies_bin(pfad))
    if kennung == b"FBANIM01":
        import fb_animdump
        return fb_animdump.dump_text(fb_animdump.lies_bin(pfad))
    raise ValueError("unbekannte Kennung %r" % kennung)


# ------------------------------------------------------------------ Bones --

def achsen_matrix():
    """Spiel (x, y, z) -> Max (x, -z, y), in Zeilenvektor-Schreibweise."""
    return [[1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, -1.0, 0.0]]


def mal(a, b):
    """Zwei 4x3-Matrizen (drei Zeilen plus Verschiebung) multiplizieren."""
    aus = []
    for r in range(4):
        zeile = a[r]
        x, y, z = zeile[0], zeile[1], zeile[2]
        neu3 = [x * b[0][k] + y * b[1][k] + z * b[2][k] for k in range(3)]
        if r == 3:
            neu3 = [neu3[k] + b[3][k] for k in range(3)]
        aus.append(neu3)
    return aus


def welt_matrizen(modell, massstab=1.0):
    """Weltlage je Bone, wie sie in Max stehen muss: lokal mal Elternteil,
    dann die Achsenumrechnung, dann der Massstab.

    Der Massstab gehoert NUR auf die Verschiebung, nicht auf die
    Achsvektoren - sonst waeren die Bones mitskaliert. Genau so macht es
    auch das Plugin."""
    a4 = achsen_matrix() + [[0.0, 0.0, 0.0]]
    welt = []
    for b in modell["bones"]:
        r = b["ruhelage"]
        lokal = [list(r[0:3]), list(r[3:6]), list(r[6:9]), list(r[9:12])]
        e = b["eltern"]
        welt.append(mal(lokal, welt[e]) if 0 <= e < len(welt) else lokal)
    aus = []
    for w in welt:
        m = mal(w, a4)
        m[3] = [v * massstab for v in m[3]]
        aus.append(m)
    return aus


def pruefe_bones(pfad_model, pfad_max, fbtools, grenze=1e-3):
    """Den Bone-Dump aus Max gegen die Referenz aus der .fbmodel halten."""
    if fbtools not in sys.path:
        sys.path.insert(0, fbtools)
    import fb_model
    modell = fb_model.lies_bin(pfad_model)

    # Der Massstab steht im Kopf des Dumps. Fehlt er, stammt die Datei aus
    # einer Fassung vor 0.19.3 - dann ist ein Vergleich sinnlos, weil in Max
    # skaliert wurde und hier nicht.
    massstab = None
    fassung = None
    with open(pfad_max, encoding="utf-8", errors="replace") as f:
        for zeile in f:
            if zeile.startswith("MASSSTAB "):
                massstab = float(zeile.split()[1].replace(",", "."))
            elif zeile.startswith("FASSUNG "):
                fassung = zeile.split()[1].strip()
            elif zeile.startswith("BONE "):
                break
    if massstab is None:
        return dict(ok=False, schlimmste=0.0,
                    grund="alter Bone-Dump ohne MASSSTAB-Zeile - bitte neu importieren")

    soll = welt_matrizen(modell, massstab)

    # Zwei Sätze Matrizen: BONE ist das, was Max zurückliefert, SETZ das,
    # was das Plugin gesetzt hat. Weichen die beiden voneinander ab, hat
    # Max etwas verändert - und nicht unsere Rechnung.
    gesetzt = {}
    with open(pfad_max, encoding="utf-8", errors="replace") as f:
        for zeile in f:
            if not zeile.startswith("SETZ "):
                continue
            teile = zeile.split()
            zahlen = [float(x.replace(",", ".")) for x in teile[2:14]]
            gesetzt[int(teile[1])] = [zahlen[0:3], zahlen[3:6], zahlen[6:9], zahlen[9:12]]

    ist = []
    with open(pfad_max, encoding="utf-8", errors="replace") as f:
        for zeile in f:
            if not zeile.startswith("BONE "):
                continue
            teile = zeile.split()
            name = teile[2]
            # Zweite Sicherung gegen das Dezimalkomma: 3ds Max stellt die
            # C-Laufzeit auf die Regionseinstellung um, dann schreibt printf
            # "1,000000". Das Plugin erzwingt seit 0.17.0 den Punkt; aeltere
            # Dumps sollen trotzdem lesbar bleiben.
            zahlen = [float(x.replace(",", ".")) for x in teile[4:16]]
            ist.append((name, [zahlen[0:3], zahlen[3:6], zahlen[6:9], zahlen[9:12]]))

    if len(ist) != len(soll):
        return dict(ok=False, grund="Max hat %d Bones, die Datei hat %d"
                    % (len(ist), len(soll)), schlimmste=0.0)

    # Nicht nur die groesste Abweichung melden, sondern auch WIE VIELE
    # Bones betroffen sind und ob es die Drehung (Zeilen 0 bis 2) oder die
    # Lage (Zeile 3) trifft. Eine einzelne Abweichung hat eine andere
    # Ursache als 248 gleichartige.
    schlimmste = 0.0
    wo = ""
    namensfehler = 0
    ueber = []
    dreh_max = 0.0
    lage_max = 0.0
    for i, (name, m) in enumerate(ist):
        if name != modell["bones"][i]["name"]:
            namensfehler += 1
        bone_max = 0.0
        bone_wo = ""
        for r in range(4):
            for k in range(3):
                dif = abs(m[r][k] - soll[i][r][k])
                if r == 3:
                    lage_max = max(lage_max, dif)
                else:
                    dreh_max = max(dreh_max, dif)
                if dif > bone_max:
                    bone_max = dif
                    bone_wo = "Zeile %d Spalte %d" % (r, k)
        if bone_max > grenze:
            ueber.append((bone_max, name, bone_wo))
        if bone_max > schlimmste:
            schlimmste = bone_max
            wo = "%s %s" % (name, bone_wo)
    # Hat Max an den Matrizen gedreht? Das trennt die beiden Ursachen.
    max_aenderung = 0.0
    max_wo = ""
    for i, (name, mm) in enumerate(ist):
        g = gesetzt.get(i)
        if g is None:
            continue
        for r in range(4):
            for k in range(3):
                dif = abs(mm[r][k] - g[r][k])
                if dif > max_aenderung:
                    max_aenderung = dif
                    max_wo = "%s Zeile %d Spalte %d" % (name, r, k)

    ueber.sort(reverse=True)
    return dict(fassung=fassung,
                gesetztVorhanden=bool(gesetzt), maxAenderung=max_aenderung,
                maxWo=max_wo, ok=(schlimmste <= grenze and namensfehler == 0),
                schlimmste=schlimmste, wo=wo, namensfehler=namensfehler,
                anzahl=len(ist), ueber=ueber, drehung=dreh_max, lage=lage_max)


def plugin_fassung():
    """Welche Fassung liegt hier gerade als Quelltext? Aus swbf2import.h."""
    hier = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    kopf = os.path.join(hier, "src", "swbf2import.h")
    try:
        with open(kopf, encoding="utf-8", errors="replace") as f:
            for zeile in f:
                if "SWBF2IMPORT_VERSION_STR" in zeile and "_T(" in zeile:
                    return zeile.split('"')[1]
    except OSError:
        pass
    return None


def bones_vergleichen(fbtools):
    """Jede _max_bones.txt neben einer .fbmodel pruefen."""
    treffer = glob.glob(os.path.join(fbtools, "**", "*.fbmodel_max_bones.txt"),
                        recursive=True)
    # Seit 0.33.0 legt das Figurenfenster jede Figur samt Bone-Dump in der
    # Ablage ab - die gehoeren genauso in die Probe.
    ablage = os.path.join(os.environ.get("LOCALAPPDATA", ""), "SWBF2Import", "figuren")
    if os.path.isdir(ablage):
        treffer += glob.glob(os.path.join(ablage, "*.fbmodel_max_bones.txt"))
    if not treffer:
        return 0, 0
    print("\nBone-Dumps aus 3ds Max:")
    jetzt = plugin_fassung()
    gut = schlecht = 0
    for p in treffer:
        model = p[:-len("_max_bones.txt")]
        if not os.path.isfile(model):
            continue
        try:
            e = pruefe_bones(model, p, fbtools)
        except Exception as ex:
            print("  %-46s %s" % (os.path.basename(p)[:46], ex))
            schlecht += 1
            continue
        alt = e.get("fassung") and jetzt and e["fassung"] != jetzt
        if e["ok"]:
            print("  %-46s %d Bones, groesste Abweichung %.6f"
                  % (os.path.basename(p)[:46], e["anzahl"], e["schlimmste"]))
            gut += 1
        else:
            schlecht += 1
            if "grund" in e:
                print("  %-46s %s" % (os.path.basename(p)[:46], e["grund"]))
            else:
                print("  %-46s ABWEICHUNG %.6f bei %s%s"
                      % (os.path.basename(p)[:46], e["schlimmste"], e.get("wo", ""),
                         "" if not e["namensfehler"]
                         else ", %d Namen passen nicht" % e["namensfehler"]))
                ueber = e.get("ueber") or []
                print("     %d von %d Bones ueber der Grenze; groesste Abweichung"
                      " in der Drehung %.6f, in der Lage %.6f"
                      % (len(ueber), e.get("anzahl", 0), e.get("drehung", 0.0),
                         e.get("lage", 0.0)))
                for wert, name, stelle in ueber[:5]:
                    print("       %-28s %.6f  (%s)" % (name[:28], wert, stelle))
                if alt:
                    print("     ACHTUNG: dieser Dump stammt aus Fassung %s, gebaut"
                          " wird gerade %s." % (e["fassung"], jetzt))
                    print("     START.bat vergleicht in Schritt 2, installiert aber"
                          " erst in Schritt 4 - also einmal importieren und START.bat"
                          " noch einmal laufen lassen.")
                if e.get("gesetztVorhanden"):
                    if e.get("maxAenderung", 0.0) > 1e-6:
                        print("     Max hat die gesetzte Matrix VERAENDERT: groesste"
                              " Aenderung %.6f bei %s"
                              % (e["maxAenderung"], e.get("maxWo", "")))
                    else:
                        print("     Max liefert exakt zurueck, was gesetzt wurde -"
                              " die Abweichung steckt in unserer Rechnung.")
    return gut, schlecht


def main(argv):
    try:
        import protokoll
        protokoll.anschalten()
    except Exception:
        pass
    exe = finde_fbdump()
    if not exe:
        print("fbdump nicht gefunden - der Bau ist wohl schiefgegangen.")
        return 2

    dateien = [a for a in argv if os.path.isfile(a)]
    ordner = next((a for a in argv if os.path.isdir(a)), None)
    fbtools, gesucht = finde_fbtools(ordner)
    if not fbtools:
        print("fbtools nicht gefunden. Gesucht wurde in:")
        for w in gesucht:
            print("   %s" % w)
        print("\nEntweder den fbtools-Ordner neben diesen hier legen,")
        print("oder ihn als Argument angeben:")
        print("   START.bat  ist dafuer nicht gedacht - dann von Hand:")
        print("   python tools\\VERGLEICHE.py \"C:\\Pfad\\zu\\fbtools-0.45.0\"")
        return 2

    print("fbdump : %s" % exe)
    print("fbtools: %s" % fbtools)
    if not dateien:
        dateien = dumps_suchen(fbtools)
    if not dateien:
        print("\nIn fbtools liegen noch keine Dumps.")
        print("Erst dort START.bat laufen lassen - das schreibt")
        print("   figur\\<bundle>.fbmodel        und")
        print("   ERGEBNIS\\animation_*.fbanim")
        return 2

    print("%d Dateien zu pruefen.\n" % len(dateien))
    gleich, ungleich = 0, 0
    for p in dateien:
        try:
            erwartet = python_text(p, fbtools)
        except Exception as ex:
            print("  %-46s Python: %s" % (os.path.basename(p)[:46], ex))
            ungleich += 1
            continue
        r = subprocess.run([exe, p], capture_output=True)
        roh = r.stdout.decode("utf-8", "replace")
        # Die Textfassung ist als reines \n festgelegt. Schreibt fbdump unter
        # Windows im Textmodus, kommt ueberall \r\n an - das waere ein
        # Unterschied in jeder Zeile, obwohl kein einziger Wert abweicht.
        bekommen = roh.replace("\r\n", "\n")
        if bekommen == erwartet and roh != erwartet:
            print("  %-46s zeichengleich, %d Zeilen (nur Zeilenenden unterschiedlich)"
                  % (os.path.basename(p)[:46], erwartet.count("\n")))
            gleich += 1
            continue
        if bekommen == erwartet:
            print("  %-46s zeichengleich, %d Zeilen"
                  % (os.path.basename(p)[:46], erwartet.count("\n")))
            gleich += 1
        else:
            ungleich += 1
            e, b = erwartet.splitlines(), bekommen.splitlines()
            print("  %-46s ABWEICHUNG (%d gegen %d Zeilen)"
                  % (os.path.basename(p)[:46], len(e), len(b)))
            gefunden = False
            for i in range(min(len(e), len(b))):
                if e[i] != b[i]:
                    print("     Zeile %d" % (i + 1))
                    print("     Python: %s" % e[i][:150])
                    print("     C++   : %s" % b[i][:150])
                    gefunden = True
                    break
            if not gefunden:
                print("     Keine Zeile weicht ab - der Unterschied steckt in den"
                      " unsichtbaren Zeichen (Zeilenenden).")
    print("\n%d zeichengleich, %d abweichend" % (gleich, ungleich))
    if ungleich == 0:
        print("Der C++-Leser liest genau dasselbe wie Python.")

    # ---- Schreibprobe: .fbmodel einlesen und wieder ausgeben ------------
    # Kommt Byte fuer Byte dieselbe Datei heraus, stimmt das Format. Das ist
    # die Vorstufe zum Zusammenbau direkt aus dem Spiel: derselbe Schreiber
    # baut spaeter die Datei aus MeshSets und Skelett.
    projekt = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    mesh_exe = None
    for ort in (os.path.join(projekt, "build_tool", "bin", "Release"),
                os.path.join(projekt, "build_tool", "bin"),
                os.path.join(projekt, "build", "bin")):
        for n in ("meshtool.exe", "meshtool"):
            if os.path.isfile(os.path.join(ort, n)):
                mesh_exe = os.path.join(ort, n)
                break
        if mesh_exe:
            break
    if mesh_exe:
        modelle = sorted(glob.glob(os.path.join(fbtools, "**", "*.fbmodel"), recursive=True))
        if modelle:
            print("\nSchreibprobe (.fbmodel einlesen und wieder ausgeben):")
            import tempfile
            for m in modelle:
                roh = open(m, "rb").read()
                fassung = struct.unpack_from("<I", roh, 8)[0] if len(roh) > 12 else 0
                if fassung != 2:
                    print("  %-46s Fassung %d - wird auf 2 gehoben, kein Vergleich"
                          % (os.path.basename(m)[:46], fassung))
                    continue
                ziel = os.path.join(tempfile.gettempdir(), "swbf2_schreibprobe.fbmodel")
                subprocess.run([mesh_exe, "--umschreiben", m, ziel], capture_output=True)
                if not os.path.isfile(ziel):
                    print("  %-46s C++ hat nichts geschrieben" % os.path.basename(m)[:46])
                    ungleich += 1
                    continue
                neu = open(ziel, "rb").read()
                os.remove(ziel)
                if neu == roh:
                    print("  %-46s %8d Byte, byteweise gleich"
                          % (os.path.basename(m)[:46], len(roh)))
                else:
                    ungleich += 1
                    stelle = next((k for k in range(min(len(neu), len(roh))) if neu[k] != roh[k]),
                                  min(len(neu), len(roh)))
                    print("  %-46s ABWEICHUNG ab Byte %d (%d gegen %d Byte)"
                          % (os.path.basename(m)[:46], stelle, len(neu), len(roh)))

    bg, bs = bones_vergleichen(fbtools)
    if bg or bs:
        print("\n%d Bone-Dumps passen, %d weichen ab" % (bg, bs))
        ungleich += bs
    return 0 if ungleich == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
