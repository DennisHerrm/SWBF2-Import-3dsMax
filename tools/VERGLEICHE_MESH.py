#!/usr/bin/env python3
"""VERGLEICHE_MESH.py - den C++-MeshSet-Leser gegen fbtools halten.

Geprueft wird an echten Spieldaten: den .res-Dateien, die fbtools bei einem
Lauf unter ERGEBNIS\\meshprobleme\\ sichert. Dazu gehoert je ein .chunk mit dem
Vertex- und Indexpuffer.

Verglichen wird zweierlei:
  1. die Textfassung des MeshSets, Zeile fuer Zeile
  2. die dekodierten Vertices: gewaehlte Deklaration, Bewertung, erster und
     letzter Vertex, die ersten Indizes
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


def text_von(ms, M):
    z = ["MESHSET %s" % ms["name"], "FULLNAME %s" % ms["fullname"],
         "HASH %08x TYP %d FLAGS %d SECTIONS %d LODS %d"
         % (ms["nameHash"], ms["meshTypeId"], ms["flags"], ms["sectionCount"], len(ms["lods"]))]
    for w in ms["warnings"]:
        z.append("WARNUNG %s" % w)
    for li, l in enumerate(ms["lods"]):
        z.append("LOD %d typ=%d flags=%d idxfmt=%d idxsize=%d vtxsize=%d hash=%08x sections=%d"
                 % (li, l["meshTypeId"], l["flags"], l["indexBufferFormat"], l["indexBufferSize"],
                    l["vertexBufferSize"], l["nameHash"], len(l["sections"])))
        z.append("  NAME %s | %s | %s" % (l["name"], l["shortName"], l["shaderDebugName"]))
        z.append("  CHUNK %s BITS %d" % (l["chunkId"], M.index_unit_size(l)))
        for s in l["sections"]:
            pt = s["primitiveType"]
            ptid = {v: k for k, v in M.PRIMITIVE_TYPE.items()}.get(pt, pt)
            z.append("  S %d mat=%d name=%s prim=%d start=%d vtxoff=%d vtx=%d stride=%d typ=%s bpv=%d bones=%d tiefe=%d"
                     % (s["index"], s["materialId"], s["materialName"], s["primitiveCount"],
                        s["startIndex"], s["vertexOffset"], s["vertexCount"], s["vertexStride"],
                        ptid, s["bonesPerVertex"], len(s["boneList"]),
                        1 if M.is_depth_section(s) else 0))
            for d in range(len(s["decls"])):
                dd = s["decls"][d]
                z.append("    DECL %d elem=%d stream=%d" % (d, dd["elementCount"], dd["streamCount"]))
                for j, st in enumerate(dd["streams"]):
                    if st.stride == 0:
                        continue
                    z.append("      STREAM %d stride=%d class=%d" % (j, st.stride, st.classification))
                for e in dd["elements"]:
                    if e.usage == 0:
                        continue
                    z.append("      E %s %s off=%d stream=%d size=%d"
                             % (e.usageName, e.formatName, e.offset, e.streamIndex, e.size))
    return "\n".join(z) + "\n"


def vertices_von(ms, chunk, li, M):
    l = ms["lods"][li]
    off = M.lod_versatz(ms, li, len(chunk))
    puffer = chunk[off:off + l["vertexBufferSize"] + l["indexBufferSize"]]
    z = ["VERSATZ %d" % off]
    for s in l["sections"]:
        verts, decl, note = M.decode_vertices_gemessen(s, puffer)
        def w(x):
            return -1.0 if x is None else x
        z.append("SEC %d decl=%d punkte=%.4f pos=%.4f gew=%.4f uv=%.4f n=%d schatten=%d"
                 % (s["index"], decl, note["punkte"], w(note["pos"]), w(note["gewichte"]),
                    w(note["uv"]), note["anzahl"],
                    1 if M.ist_schattengeometrie(s, verts) else 0))
        if verts:
            stellen = [0] if len(verts) == 1 else [0, len(verts) - 1]
            for k in stellen:
                teile = []
                for name in sorted(verts[k]):
                    teile.append("%s=%s" % (name, ",".join("%.6f" % v for v in verts[k][name])))
                z.append("  V %d %s" % (k, " ".join(teile)))
        try:
            idx = M.read_indices(l, puffer, s)
            flach = [v for t in idx for v in t]
            z.append("  IDX %d%s" % (len(flach), "".join(" %d" % v for v in flach[:6])))
        except Exception as ex:
            z.append("  IDX FEHLER %s" % ex)
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
    import fb_meshset as M

    # Wo liegen die gesicherten MeshSets? fbtools legt sie unter
    # ERGEBNIS\meshprobleme ab, aber der Ordner wandert beim Packen ins ZIP und
    # ist nach dem Entpacken mal hier, mal eine Ebene tiefer. Also wird gesucht,
    # statt einen festen Pfad anzunehmen: jede .res mit passender .meta.json
    # zaehlt.
    def suche(wurzel):
        """Nur MeshSets. fbtools sichert auch Animationsbaenke als .res, und die
        haben denselben Dateinamen und dieselbe .meta.json daneben - die
        Metadatei sagt aber, was drinsteht: resType 49B156D4 ist MeshSet."""
        treffer = []
        andere = 0
        for res in glob.glob(os.path.join(wurzel, "**", "*.res"), recursive=True):
            meta_pfad = res + ".meta.json"
            if not os.path.isfile(meta_pfad):
                continue
            try:
                m = json.load(open(meta_pfad, encoding="utf-8"))
            except Exception:
                continue
            if (m.get("resType") or "").upper() != "49B156D4":
                andere += 1
                continue
            treffer.append(res)
        return sorted(treffer), andere

    dateien, andere = [], 0
    if ordner and os.path.isdir(ordner):
        dateien, andere = suche(ordner)
    if not dateien:
        for wurzel in (fbtools, os.path.dirname(os.path.abspath(fbtools))):
            dateien, andere = suche(wurzel)
            if dateien:
                ordner = wurzel
                break
    if not dateien:
        print("Keine gesicherten MeshSets gefunden (gesucht unter %s)." % fbtools)
        if andere:
            print("%d .res-Dateien anderer Art uebergangen (Animationsbaenke und dergleichen)."
                  % andere)
        print("fbtools legt MeshSets beim Lauf unter ERGEBNIS\\meshprobleme ab.")
        return 0
    ordner = os.path.dirname(dateien[0])

    print("meshtool: %s" % exe)
    print("%d MeshSets zu pruefen.\n" % len(dateien))
    gleich = ungleich = 0
    for res in dateien:
        name = os.path.basename(res)[:-4]
        meta = {}
        if os.path.isfile(res + ".meta.json"):
            meta = json.load(open(res + ".meta.json"))
        rohdaten = open(res, "rb").read()
        rm = bytes.fromhex(meta["resMeta"]) if meta.get("resMeta") else None
        try:
            ms = M.parse_meshset(rohdaten, rm)
        except Exception as ex:
            print("  %-34s Python kommt nicht heran: %s" % (name[:34], ex))
            ungleich += 1
            continue

        soll = text_von(ms, M)
        ist = subprocess.run([exe, res], capture_output=True).stdout
        ist = ist.decode("utf-8", "replace").replace("\r\n", "\n")
        if ist == soll:
            print("  %-34s Kopf zeichengleich, %d Zeilen" % (name[:34], soll.count("\n")))
            gleich += 1
        else:
            ungleich += 1
            a, b = soll.splitlines(), ist.splitlines()
            print("  %-34s ABWEICHUNG im Kopf (%d gegen %d Zeilen)" % (name[:34], len(a), len(b)))
            for k in range(min(len(a), len(b))):
                if a[k] != b[k]:
                    print("     Zeile %d" % (k + 1))
                    print("     Python: %s" % a[k][:160])
                    print("     C++   : %s" % b[k][:160])
                    break

        for ch in sorted(glob.glob(os.path.join(ordner, name + "_lod*.chunk"))):
            li = int(ch.rsplit("_lod", 1)[1].split(".")[0])
            chunk = open(ch, "rb").read()
            soll = vertices_von(ms, chunk, li, M)
            ist = subprocess.run([exe, res, "--vertices", ch, str(li)], capture_output=True).stdout
            ist = ist.decode("utf-8", "replace").replace("\r\n", "\n")
            if ist == soll:
                print("  %-34s LOD %d: Vertices zeichengleich, %d Zeilen"
                      % ("", li, soll.count("\n")))
                gleich += 1
            else:
                ungleich += 1
                a, b = soll.splitlines(), ist.splitlines()
                print("  %-34s LOD %d: ABWEICHUNG (%d gegen %d Zeilen)" % ("", li, len(a), len(b)))
                for k in range(min(len(a), len(b))):
                    if a[k] != b[k]:
                        print("     Zeile %d" % (k + 1))
                        print("     Python: %s" % a[k][:160])
                        print("     C++   : %s" % b[k][:160])
                        break

    print("\n%d gleich, %d abweichend" % (gleich, ungleich))
    if ungleich == 0:
        print("Der C++-MeshSet-Leser liest genau dasselbe wie fbtools.")
    return 0 if ungleich == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
