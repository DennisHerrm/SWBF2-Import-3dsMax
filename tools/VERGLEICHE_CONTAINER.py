#!/usr/bin/env python3
"""VERGLEICHE_CONTAINER.py - die C++-Containerschicht gegen fbtools halten.

Der Prüfstand ist das Mini-Spiel aus fbtools:

    python tests/synth_test.py --keep C:\\minispiel

Es enthält alle fünf Entpackwege (roh, zlib, lz4, zstd, zstd mit
Wörterbuch), einen Delta-Patch, ein Manifest und ein Bundle - also genau
das, was die echte Installation auch bietet, nur klein genug zum Prüfen.

Verglichen wird zweierlei:
  1. die Katalogausgabe Zeile für Zeile
  2. jeder einzelne Eintrag, byteweise nach dem Entpacken
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def hexen(s):
    """fbtools liefert SHA-1 mal als Bytes, mal schon als Text."""
    return s if isinstance(s, str) else s.hex()


def finde(name, *orte):
    for o in orte:
        p = os.path.join(o, name)
        if os.path.isfile(p):
            return p
    return None


def main(argv):
    try:
        import protokoll
        protokoll.anschalten()
    except Exception:
        pass
    if len(argv) < 2:
        print("VERGLEICHE_CONTAINER.py <minispiel-ordner> [fbtools-ordner]")
        return 2
    wurzel = argv[0] if False else argv[1] if len(argv) > 1 else None
    wurzel = argv[1] if len(argv) > 1 else argv[0]
    fbtools = argv[2] if len(argv) > 2 else None

    hier = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    # START.bat baut die Werkzeuge nach build_tool\, nicht nach build\ -
    # deshalb steht der zuerst in der Liste. Genau daran ist der erste Lauf
    # mit "castool nicht gefunden" gescheitert.
    orte = [os.path.join(hier, "build_tool", "bin", "Release"),
            os.path.join(hier, "build_tool", "bin"),
            os.path.join(hier, "build_tool", "Release"),
            os.path.join(hier, "build", "bin", "Release"),
            os.path.join(hier, "build", "bin"),
            os.path.join(hier, "build")]
    exe = finde("castool.exe", *orte) or finde("castool", *orte)
    if exe is None:
        print("castool nicht gefunden - erst START.bat laufen lassen.")
        return 2

    if fbtools is None:
        for kandidat in (os.path.join(hier, ".."), os.path.join(hier, "..", "..")):
            import glob
            treffer = sorted(glob.glob(os.path.join(kandidat, "fbtools*")), reverse=True)
            for t in treffer:
                if os.path.isfile(os.path.join(t, "fb_container.py")):
                    fbtools = t
                    break
            if fbtools:
                break
    if fbtools is None or not os.path.isfile(os.path.join(fbtools, "fb_container.py")):
        print("fbtools nicht gefunden. Ordner als zweites Argument angeben.")
        return 2
    sys.path.insert(0, fbtools)
    import fb_container as fb

    kat = os.path.join(wurzel, "Data", "win32", "installation", "test", "cas.cat")
    if not os.path.isfile(kat):
        print("Im Mini-Spiel fehlt %s" % kat)
        return 2

    print("castool: %s" % exe)
    print("fbtools: %s" % fbtools)
    print()

    # ---- 1. Katalog Zeile fuer Zeile ------------------------------------
    roh = open(kat, "rb").read()
    start = fb.find_cat_magic(roh)
    eintraege, patches = fb.read_catalog(roh, "native_data/win32/installation/test/cas.cat")
    zeilen = ["KATALOG %s" % kat, "START %d" % start,
              "EINTRAEGE %d" % len(eintraege), "PATCHES %d" % len(patches)]
    for i, e in enumerate(eintraege):
        zeilen.append("E %d %s %d %d %d %d"
                      % (i, hexen(e.sha1), e.offset, e.size, e.logicalOffset, e.archiveIndex))
    for i, p in enumerate(patches):
        zeilen.append("P %d %s %s %s" % (i, hexen(p.sha1), hexen(p.baseSha1), hexen(p.deltaSha1)))
    erwartet = "\n".join(zeilen) + "\n"

    bekommen = subprocess.run([exe, kat], capture_output=True).stdout.decode("utf-8", "replace")
    bekommen = bekommen.replace("\r\n", "\n")
    if bekommen == erwartet:
        print("Katalog: zeichengleich, %d Zeilen" % erwartet.count("\n"))
        fehler = 0
    else:
        fehler = 1
        a, b = erwartet.splitlines(), bekommen.splitlines()
        print("Katalog: ABWEICHUNG (%d gegen %d Zeilen)" % (len(a), len(b)))
        for i in range(min(len(a), len(b))):
            if a[i] != b[i]:
                print("   Zeile %d" % (i + 1))
                print("   Python: %s" % a[i])
                print("   C++   : %s" % b[i])
                break

    # ---- 2. Jeden Eintrag entpacken und byteweise vergleichen ------------
    fs = fb.GameFS(wurzel, log=lambda *a: None)
    dictdatei = os.path.join(os.path.dirname(exe), "ebx.dict")
    woerterbuch = fs.memory_fs.get("Dictionaries/ebx.dict")
    if woerterbuch:
        open(dictdatei, "wb").write(woerterbuch)

    print()
    gleich = ungleich = 0
    for i, e in enumerate(eintraege):
        try:
            soll = fs.get_by_sha1(e.sha1)
        except Exception as ex:
            # Ein reiner Deltastrom laesst sich allein nicht entpacken -
            # er kommt weiter unten im Deltapfad dran.
            print("  Eintrag %2d: allein nicht entpackbar (%s)" % (i, str(ex)[:48]))
            continue
        ziel = os.path.join(os.path.dirname(exe), "eintrag_%d.bin" % i)
        r = subprocess.run([exe, kat, "--entpacke", str(i), ziel, dictdatei],
                           capture_output=True)
        if not os.path.isfile(ziel):
            print("  Eintrag %2d: C++ scheitert: %s" % (i, r.stderr.decode("utf-8", "replace").strip()))
            ungleich += 1
            continue
        ist = open(ziel, "rb").read()
        os.remove(ziel)
        if ist == soll:
            print("  Eintrag %2d: %7d Byte, byteweise gleich" % (i, len(soll)))
            gleich += 1
        else:
            ungleich += 1
            stelle = next((k for k in range(min(len(ist), len(soll))) if ist[k] != soll[k]),
                          min(len(ist), len(soll)))
            print("  Eintrag %2d: ABWEICHUNG - %d gegen %d Byte, erste Stelle %d"
                  % (i, len(ist), len(soll), stelle))
    # ---- 3. DbObject: layout.toc und initfs_win32 -------------------------
    def db_text(w, name="", tiefe=0):
        ein = "  " * tiefe
        if isinstance(w, list):
            aus = ["%sLISTE %s [%d]" % (ein, name, len(w))]
            for s2 in w:
                aus.append(db_text(s2, "", tiefe + 1))
            return "\n".join(aus)
        if isinstance(w, dict):
            aus = ["%sOBJEKT %s {%d}" % (ein, name, len(w))]
            for k, v in w.items():
                aus.append(db_text(v, k, tiefe + 1))
            return "\n".join(aus)
        if isinstance(w, bool):
            return "%sBOOL %s %d" % (ein, name, 1 if w else 0)
        if isinstance(w, str):
            return "%sTEXT %s %s" % (ein, name, w)
        if isinstance(w, int):
            # fbtools unterscheidet int32 und int64 nicht - das C++ auch nicht,
            # solange der Wert passt. Deshalb wird die Zeile hier nachgezogen.
            return "%sZAHL %s %d" % (ein, name, w)
        if isinstance(w, float):
            return "%sGLEIT %s %.6f" % (ein, name, w)
        if isinstance(w, (bytes, bytearray)):
            return "%sBYTES %s %d %s" % (ein, name, len(w), bytes(w).hex())
        return "%s? %s %r" % (ein, name, w)

    print()
    for datei in ("layout.toc", "initfs_win32"):
        pfad = os.path.join(wurzel, "Data", datei)
        if not os.path.isfile(pfad):
            continue
        roh = open(pfad, "rb").read()
        soll = db_text(fb.read_db_object(roh)) + "\n"
        ist = subprocess.run([exe, "--db", pfad], capture_output=True).stdout
        ist = ist.decode("utf-8", "replace").replace("\r\n", "\n")
        # GANZ und LANG auf ZAHL vereinheitlichen, siehe oben.
        ist = ist.replace("GANZ ", "ZAHL ").replace("LANG ", "ZAHL ")
        # Guid und Sha1 kommen aus fbtools schon als Text zurueck.
        ist = ist.replace("GUID ", "TEXT ").replace("SHA1 ", "TEXT ")
        if ist == soll:
            print("  %-16s zeichengleich, %d Zeilen" % (datei, soll.count("\n")))
            gleich += 1
        else:
            ungleich += 1
            a, b = soll.splitlines(), ist.splitlines()
            print("  %-16s ABWEICHUNG (%d gegen %d Zeilen)" % (datei, len(a), len(b)))
            for k in range(min(len(a), len(b))):
                if a[k] != b[k]:
                    print("     Zeile %d" % (k + 1))
                    print("     Python: %s" % a[k][:150])
                    print("     C++   : %s" % b[k][:150])
                    break

    # ---- 4. Der Deltapfad ------------------------------------------------
    for i, pt in enumerate(patches):
        try:
            soll = fs.get_by_sha1(hexen(pt.sha1))
        except Exception as ex:
            print("  Patch %d: Python kommt nicht heran (%s)" % (i, ex))
            ungleich += 1
            continue
        ziel = os.path.join(os.path.dirname(exe), "patch_%d.bin" % i)
        r = subprocess.run([exe, kat, "--gepatcht", hexen(pt.baseSha1),
                            hexen(pt.deltaSha1), ziel, dictdatei], capture_output=True)
        if not os.path.isfile(ziel):
            print("  Patch  %2d: C++ scheitert: %s" % (i, r.stderr.decode("utf-8", "replace").strip()))
            ungleich += 1
            continue
        ist = open(ziel, "rb").read()
        os.remove(ziel)
        if ist == soll:
            print("  Patch  %2d: %7d Byte, byteweise gleich (Deltapfad)" % (i, len(soll)))
            gleich += 1
        else:
            ungleich += 1
            print("  Patch  %2d: ABWEICHUNG - %d gegen %d Byte" % (i, len(ist), len(soll)))

    # ---- 5. Das ganze Spiel: Kataloge, Manifest, initfs -------------------
    zeilen = ["PFADE %d" % len(fs.paths),
              "BASE %d" % fs.base_num, "HEAD %d" % fs.head_num,
              "SUPERBUNDLES %d" % len(fs.superbundles)]
    zeilen += ["SB %s" % sb for sb in fs.superbundles]
    zeilen.append("KATALOGE %d" % len(fs.catalogs))
    zeilen += ["KAT %s %d" % (k["name"], 1 if k["alwaysInstalled"] else 0)
               for k in fs.catalogs]
    zeilen.append("MANIFEST %d %d %d" % (len(fs.manifest_files),
                                         len(fs.manifest_bundles),
                                         len(fs.manifest_chunks)))
    zeilen.append("CATEINTRAEGE %d" % len(fs.cat_entries))
    zeilen.append("CATPATCHES %d" % len(fs.cat_patches))
    wbytes = fs.find_memory_file("ebx.dict")
    zeilen.append("WOERTERBUCH %d" % (len(wbytes) if wbytes else 0))
    erwartet_fs = "\n".join(zeilen) + "\n"

    print()
    bekommen_fs = subprocess.run([exe, "--fs", wurzel], capture_output=True).stdout
    bekommen_fs = bekommen_fs.decode("utf-8", "replace").replace("\r\n", "\n")
    if bekommen_fs == erwartet_fs:
        print("  Spielsicht        zeichengleich, %d Zeilen" % erwartet_fs.count("\n"))
        gleich += 1
    else:
        ungleich += 1
        a, b = erwartet_fs.splitlines(), bekommen_fs.splitlines()
        print("  Spielsicht        ABWEICHUNG (%d gegen %d Zeilen)" % (len(a), len(b)))
        for k in range(min(len(a), len(b))):
            if a[k] != b[k]:
                print("     Zeile %d" % (k + 1))
                print("     Python: %s" % a[k][:150])
                print("     C++   : %s" % b[k][:150])
                break

    # Und jetzt der eigentliche Zweck: SHA-1 rein, Nutzdaten raus -
    # ueber den ganzen Weg (Katalog suchen, cas oeffnen, entpacken).
    alle = [hexen(e.sha1) for e in eintraege] + [hexen(p2.sha1) for p2 in patches]
    for sha in alle:
        try:
            soll = fs.get_by_sha1(sha)
        except Exception:
            continue                     # reiner Deltastrom, siehe oben
        ziel = os.path.join(os.path.dirname(exe), "fs_%s.bin" % sha[:8])
        r = subprocess.run([exe, "--fs", wurzel, "--sha1", sha, ziel],
                           capture_output=True)
        if not os.path.isfile(ziel):
            print("  ueber SHA-1 %s: C++ scheitert: %s"
                  % (sha[:8], r.stderr.decode("utf-8", "replace").strip()[:60]))
            ungleich += 1
            continue
        ist = open(ziel, "rb").read()
        os.remove(ziel)
        if ist == soll:
            print("  ueber SHA-1 %s:  %7d Byte, byteweise gleich" % (sha[:8], len(soll)))
            gleich += 1
        else:
            ungleich += 1
            print("  ueber SHA-1 %s:  ABWEICHUNG - %d gegen %d Byte"
                  % (sha[:8], len(ist), len(soll)))

    # ---- 6. Bundles: der Weg vom NAMEN zu den Nutzdaten -------------------
    zeilen = ["BUNDLES %d" % len(fs.manifest_bundles)]
    for i, (mb, bu) in enumerate(fs.iter_bundles()):
        zeilen.append("B %d %08x ebx=%d res=%d chunks=%d"
                      % (i, mb.hash, len(bu["ebx"]), len(bu["res"]), len(bu["chunks"])))
        for e in bu["ebx"]:
            zeilen.append("  EBX %s %s %d" % (e.name, hexen(e.sha1), e.originalSize))
        for e in bu["res"]:
            zeilen.append("  RES %s %s %d %08x %u"
                          % (e.name, hexen(e.sha1), e.originalSize, e.resType,
                             e.resRid & 0xFFFFFFFF))
        for c in bu["chunks"]:
            zeilen.append("  CHUNK %s %d %d" % (hexen(c.sha1), c.logicalOffset, c.logicalSize))
    erwartet_b = "\n".join(zeilen) + "\n"

    bekommen_b = subprocess.run([exe, "--bundles", wurzel], capture_output=True).stdout
    bekommen_b = bekommen_b.decode("utf-8", "replace").replace("\r\n", "\n")
    if bekommen_b == erwartet_b:
        print("  Bundles           zeichengleich, %d Zeilen" % erwartet_b.count("\n"))
        gleich += 1
    else:
        ungleich += 1
        a, b = erwartet_b.splitlines(), bekommen_b.splitlines()
        print("  Bundles           ABWEICHUNG (%d gegen %d Zeilen)" % (len(a), len(b)))
        for k in range(min(len(a), len(b))):
            if a[k] != b[k]:
                print("     Zeile %d" % (k + 1))
                print("     Python: %s" % a[k][:150])
                print("     C++   : %s" % b[k][:150])
                break

    # ---- 7. Der Index: aus dem NAMEN wird ein SHA-1 -----------------------
    idx = fb.build_index(fs, log=lambda *a: None)
    geteilt = {fb.fnv1(n): n for n in fb.SWBF2_SHARED_BUNDLES}
    herkunft = {}
    for b in idx["bundles"]:
        if b["name"] == b["hash"]:
            woher = "hash"
        elif geteilt.get(int(b["hash"], 16)) == b["name"]:
            woher = "shared"
        elif b["name"] == b["name"].lower() and fb.fnv1(b["name"]) == int(b["hash"], 16) \
                and b["name"] != ("win32/" + b["name"][6:]):
            woher = "ebx-lower"
        else:
            woher = "ebx"
        herkunft[woher] = herkunft.get(woher, 0) + 1

    zeilen = ["INDEX bundles=%d ebx=%d res=%d chunks=%d"
              % (len(idx["bundles"]), len(idx["ebx"]), len(idx["res"]), len(idx["chunks"]))]
    for k in sorted(herkunft):
        zeilen.append("NAMEN %s %d" % (k, herkunft[k]))
    for b in idx["bundles"]:
        zeilen.append("B %d %s %s ebx=%d res=%d chunks=%d"
                      % (b["index"], b["hash"], b["name"], b["ebx"], b["res"], b["chunks"]))
    for k in sorted(idx["ebx"]):
        d = idx["ebx"][k]
        zeilen.append("EBX %s %s %d" % (k, hexen(d["sha1"]), d["originalSize"]))
    for k in sorted(idx["res"]):
        d = idx["res"][k]
        zeilen.append("RES %s %s %d %s %s"
                      % (k, hexen(d["sha1"]), d["originalSize"], d["resType"], d["resRid"]))
    for k in sorted(idx["chunks"]):
        d = idx["chunks"][k]
        zeilen.append("CHUNK %s %s" % (k, hexen(d["sha1"]) if d.get("sha1") else "-"))
    erwartet_i = "\n".join(zeilen) + "\n"

    bekommen_i = subprocess.run([exe, "--index", wurzel], capture_output=True).stdout
    bekommen_i = bekommen_i.decode("utf-8", "replace").replace("\r\n", "\n")
    if bekommen_i == erwartet_i:
        print("  Index             zeichengleich, %d Zeilen" % erwartet_i.count("\n"))
        gleich += 1
    else:
        ungleich += 1
        a, b = erwartet_i.splitlines(), bekommen_i.splitlines()
        print("  Index             ABWEICHUNG (%d gegen %d Zeilen)" % (len(a), len(b)))
        for k in range(min(len(a), len(b))):
            if a[k] != b[k]:
                print("     Zeile %d" % (k + 1))
                print("     Python: %s" % a[k][:150])
                print("     C++   : %s" % b[k][:150])
                break

    if os.path.isfile(dictdatei):
        os.remove(dictdatei)

    print("\n%d Eintraege gleich, %d abweichend" % (gleich, ungleich))
    if fehler == 0 and ungleich == 0:
        print("Die C++-Containerschicht liest genau dasselbe wie fbtools.")
    return 0 if (fehler == 0 and ungleich == 0) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
