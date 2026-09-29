#!/usr/bin/env python3
"""VERGLEICHE_GD.py - den C++-Leser fuer GD-Baenke gegen fbtools halten.

Geprueft wird an echten Spieldaten: den Animationsbaenken, die fbtools bei
einem Lauf unter ERGEBNIS\\dump_anim\\ sichert (resType 51A3C853, AssetBank).
"""
import glob
import json
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


def text_von(b):
    z = ["BANK typ=%d dataOffset=%d refl=%d unter=%d/%d"
         % (b["packageType"], b["dataOffset"], b["reflType"],
            b["subDataCount"], b["subDataCapacity"]),
         "IDS %d" % len(b["ids"])]
    z += ["ID %s" % i for i in b["ids"]]
    z.append("BLOECKE %d" % len(b["blocks"]))
    for k in b["blocks"]:
        z.append("BL %s pos=%d size=%d le=%d w=%d,%d"
                 % (k.tag, k.pos, k.size, 1 if k.littleEndian else 0, k.words[0], k.words[1]))
    z.append("KLASSEN %d" % len(b["classes"]))
    for h in sorted(b["classes"]):
        k = b["classes"][h]
        z.append("K %08x %s size=%d nativ=%d felder=%d"
                 % (h, k.name, k.size, k.native, len(k.fields)))
        for f in k.fields:
            z.append("  F %s typ=%08x(%s) off=%d es=%d n=%d flags=%d arr=%d ea=%d rle=%d lay=%d"
                     % (f.name, f.typeHash, f.type or "", f.offset, f.elementSize, f.count,
                        f.flags, 1 if f.isArray else 0, f.elementAlign, f.rle, f.layout))
    z.append("EINTRAEGE %d" % len(b["entries"]))
    for e in b["entries"]:
        z.append("E pos=%d size=%d typ=%08x klasse=%08x(%s) satz=%d"
                 % (e["pos"], e["size"], e["typeHash"], e["classHash"],
                    e["className"] or "", e["record"]))
    for w in b["warnings"]:
        z.append("WARNUNG %s" % w)
    return "\n".join(z) + "\n"


def _text(s):
    if all(32 <= ord(c) < 127 for c in s):
        return s
    return "x:" + s.encode("latin-1").hex()


def _kanon(v, art):
    """art: 'i' Ganzzahl, 'b' Wahrheitswert, 'f' Gleitzahl, 'h' Hex, 's' Text."""
    import struct
    if v is None:
        return "n:"
    if art == "f":
        return "f:%08x" % struct.unpack("<I", struct.pack("<f", v))[0]
    if art == "b":
        return "b:%d" % (1 if v else 0)
    if art == "i":
        return "i:%d" % int(v)
    if art == "h":
        return "h:" + v
    return "s:" + _text(v)


def _art(typ, name, skalar_bool):
    import fb_gd as G
    if typ in G.ELEMENT:
        if typ == "Float":
            return "f"
        return "b" if (typ == "Bool" and skalar_bool) else "i"
    if typ in ("Key", "DataRef") or name == "__key":
        return "h"
    if typ == "String":
        return "s"
    return None


def _fnv(data):
    h = 0xcbf29ce484222325
    for c in data:
        h ^= c
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def werte_text(b, data):
    """Dieselbe exakte Vergleichsform wie fbgd::WerteAlsText (C++)."""
    import fb_gd as G
    z = []
    klassen_nach_name = {c.name: c for c in b["classes"].values()}
    for i, e in enumerate(b["entries"]):
        k = b["classes"].get(e["classHash"])
        endian = ">" if not e["littleEndian"] else "<"
        name = None
        if k is not None:
            off = next((f.offset for f in k.fields if f.name == "__name"), None)
            if off is not None:
                name = G._string_feld(data, e["record"] + off, endian)
        z.append("E %d klasse=%s name=%s" % (i, e["className"] or "-", _text(name) if name is not None else "-"))
        if k is None:
            continue
        w = G.lies_datensatz(b, data, e, mit_daten=True, hoechstens=4)

        def felder(kl, werte, kennung, basis):
            for f in kl.fields:
                if f.name not in werte:
                    continue
                v = werte[f.name]
                if isinstance(v, dict) and not basis:
                    groesse = 0
                    if f.type == "Key":
                        groesse = 8
                    elif f.type in G.ELEMENT:
                        groesse = G.ELEMENT[f.type][1]
                    elif f.type in klassen_nach_name and klassen_nach_name[f.type].fields:
                        s = max(uf.offset + uf.elementSize for uf in klassen_nach_name[f.type].fields)
                        groesse = (s + 7) // 8 * 8
                    summe = "-"
                    if groesse and v["start"] + v["anzahl"] * groesse <= len(data):
                        summe = "%016x" % _fnv(data[v["start"]:v["start"] + v["anzahl"] * groesse])
                    zeile = " A %s typ=%s n=%d kap=%d start=%d sum=%s" % (
                        f.name, f.type or "-", v["anzahl"], v["kapazitaet"], v["start"], summe)
                    if v["werte"] is None:
                        zeile += " werte=-"
                    else:
                        for x in v["werte"]:
                            if isinstance(x, dict):
                                uk = klassen_nach_name[f.type]
                                teile = []
                                for uf in uk.fields:
                                    if uf.name in x:
                                        teile.append("%s=%s" % (uf.name, _kanon(x[uf.name], _art(uf.type, uf.name, False))))
                                zeile += " {" + ",".join(teile) + "}"
                            else:
                                zeile += " " + _kanon(x, "h" if f.type == "Key" else _art(f.type, f.name, False))
                    z.append(zeile)
                else:
                    if basis:
                        a = ("f" if f.type == "Float" else "i") if (f.type in G.ELEMENT and not f.isArray) else \
                            ("s" if f.name == "__name" else "h")
                    else:
                        a = _art(f.type, f.name, True)
                    z.append(" %s %s %s" % (kennung, f.name, _kanon(v, a)))

        felder(k, w, "F", False)
        try:
            bas = G.lies_basis(b, data, e)
        except Exception:
            bas = None
        if bas:
            z.append(" B __klasse %s" % bas["__klasse"])
            bk = klassen_nach_name.get(bas["__klasse"])
            if bk is not None:
                felder(bk, bas, "B", True)
    return "\n".join(z) + ("\n" if z else "")


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
    import fb_gd as G

    def suche(wurzel):
        """Animationsbaenke: Dateien, die auf _antstate enden, oder .res mit
        resType 51A3C853 (AssetBank) in der Metadatei."""
        treffer = []
        for p in glob.glob(os.path.join(wurzel, "**", "*"), recursive=True):
            if not os.path.isfile(p) or p.endswith(".json"):
                continue
            meta = p + ".meta.json"
            if os.path.isfile(meta):
                try:
                    m = json.load(open(meta, encoding="utf-8"))
                except Exception:
                    continue
                if (m.get("resType") or "").upper() == "51A3C853":
                    treffer.append(p)
            elif p.endswith("_antstate"):
                treffer.append(p)
        return sorted(treffer)

    dateien = []
    if ordner and os.path.isdir(ordner):
        dateien = suche(ordner)
    if not dateien:
        for wurzel in (fbtools, os.path.dirname(os.path.abspath(fbtools))):
            dateien = suche(wurzel)
            if dateien:
                break
    if not dateien:
        print("Keine gesicherten Animationsbaenke gefunden (gesucht unter %s)." % fbtools)
        print("fbtools legt sie beim Lauf unter ERGEBNIS\\dump_anim ab.")
        return 0

    print("meshtool: %s" % exe)
    print("%d Baenke zu pruefen.\n" % len(dateien))
    gleich = ungleich = 0
    for pfad in dateien:
        name = os.path.basename(pfad)
        try:
            b = G.read_bank(open(pfad, "rb").read(), mit_werten=False)
        except Exception as ex:
            print("  %-40s Python kommt nicht heran: %s" % (name[:40], ex))
            ungleich += 1
            continue
        soll = text_von(b)
        ist = subprocess.run([exe, "--gd", pfad], capture_output=True).stdout
        ist = ist.decode("utf-8", "replace").replace("\r\n", "\n")
        if ist == soll:
            # Zweite Probe: die WERTE der Datensaetze (seit 0.36.0).
            try:
                roh = open(pfad, "rb").read()
                soll_w = werte_text(G.read_bank(roh, mit_werten=False), roh)
            except Exception as ex:
                soll_w = "PYTHON FEHLER %s\n" % ex
            ist_w = subprocess.run([exe, "--gd-werte", pfad], capture_output=True).stdout
            ist_w = ist_w.decode("utf-8", "replace").replace("\r\n", "\n")
            if ist_w == soll_w:
                print("  %-40s zeichengleich, %d Zeilen; Werte gleich, %d Zeilen" % (
                    name[:40], soll.count("\n"), soll_w.count("\n")))
                gleich += 1
            else:
                ungleich += 1
                a, c = soll_w.splitlines(), ist_w.splitlines()
                print("  %-40s Aufbau gleich, WERTE WEICHEN AB (%d gegen %d Zeilen)" % (name[:40], len(a), len(c)))
                for kk in range(min(len(a), len(c))):
                    if a[kk] != c[kk]:
                        print("     Zeile %d" % (kk + 1))
                        print("     Python: %s" % a[kk][:200])
                        print("     C++   : %s" % c[kk][:200])
                        break
        else:
            ungleich += 1
            a, c = soll.splitlines(), ist.splitlines()
            print("  %-40s ABWEICHUNG (%d gegen %d Zeilen)" % (name[:40], len(a), len(c)))
            for k in range(min(len(a), len(c))):
                if a[k] != c[k]:
                    print("     Zeile %d" % (k + 1))
                    print("     Python: %s" % a[k][:160])
                    print("     C++   : %s" % c[k][:160])
                    break

    print("\n%d gleich, %d abweichend" % (gleich, ungleich))
    if ungleich == 0:
        print("Der C++-Leser fuer GD-Baenke liest genau dasselbe wie fbtools.")
    return 0 if ungleich == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
