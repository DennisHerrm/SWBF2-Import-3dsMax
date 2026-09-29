#!/usr/bin/env python3
"""Baut eine kleine GD-Bank mit bekanntem Inhalt - nach dem Aufbau, den
fb_gd.py beschreibt (Kopf big-endian, Bloecke GD.STRM/REF2/DAT2, REF2 mit
Klassen- und Feldtabelle, DAT2 mit Datensatz ab +44). Darin: Grundtypen,
eine Zeichenkette, Key, Arrays aus Float/Key/Unterstruktur und ein
eingebetteter Basis-Satz ueber __base - alle Versaetze relativ zu sich selbst
und mit gesetztem oberem Markenbyte. So lassen sich fb_gd.py und der
C++-Leser OHNE Spieldateien gegeneinander halten."""
import struct, sys

MARKE = 0x10 << 56


def klasse(name, h, groesse, felder):
    """felder: (name, typhash, groesse, versatz, istArray)"""
    namen = b"\0" + name.encode() + b"\0"
    offs = []
    for f in felder:
        offs.append(len(namen)); namen += f[0].encode() + b"\0"
    str_off = 32 + 32 * len(felder)
    kopf = struct.pack("<iiiIII", 0, 0, max(0, len(felder) - 1), groesse, str_off, len(namen))
    kopf += bytes([0, 0, 0, 0]) + struct.pack("<I", h)
    fe = b""
    for f, no in zip(felder, offs):
        fe += struct.pack("<IIIIHHHhq", f[1], f[2], f[3], no, 1, 1 if f[4] else 0, 4, 0, 0)
    roh = kopf + fe + namen
    while len(roh) % 8:
        roh += b"\0"
    return roh


def main(ziel):
    H = dict(Float=1, Int32=2, UInt16=3, Bool=4, String=5, Key=6, Sub=7, TestAnim=8, BaseAnim=9, UInt32=10)
    kl = [klasse(n, H[n], 4, []) for n in ("Float", "Int32", "UInt16", "Bool", "String", "Key", "UInt32")]
    kl.append(klasse("Sub", H["Sub"], 8, [("a", H["Float"], 4, 0, False), ("b", H["Int32"], 4, 4, False)]))
    kl.append(klasse("TestAnim", H["TestAnim"], 88, [
        ("__name", H["String"], 16, 0, False), ("__key", 0, 8, 16, False), ("__base", 0, 8, 24, False),
        ("NumKeys", H["UInt16"], 2, 32, False), ("Flag", H["Bool"], 1, 34, False), ("Scale", H["Float"], 4, 36, False),
        ("Data", H["Float"], 4, 40, True), ("Keys", H["Key"], 8, 56, True), ("Subs", H["Sub"], 8, 72, True)]))
    kl.append(klasse("BaseAnim", H["BaseAnim"], 24, [
        ("CodecType", H["UInt32"], 4, 0, False), ("EndFrame", H["Int32"], 4, 4, False), ("__name", H["String"], 16, 8, False)]))
    ref_inhalt = struct.pack("<q", len(kl) | MARKE) + struct.pack("<%dq" % len(kl), *range(len(kl)))
    for k in kl:
        ref_inhalt += k
    ref = b"GD.REF2l" + struct.pack("<I", 12 + len(ref_inhalt)) + ref_inhalt
    while len(ref) % 16:
        ref += b"\0"
    ref = ref[:8] + struct.pack("<I", len(ref)) + ref[12:]

    # DAT2: Kopf bis +44, dann der Datensatz (88 Byte), dann die Nutzlast.
    dat = bytearray(b"GD.DAT2l" + b"\0" * 36)
    struct.pack_into("<I", dat, 12, 0xABCD)            # typeHash (words[0])
    struct.pack_into("<I", dat, 28, H["TestAnim"])      # Klassenhash
    rec = 44
    dat += b"\0" * 88
    def rel(desc_pos, ziel_pos):                         # Deskriptor: relativ + 8
        return (ziel_pos - desc_pos - 8) | MARKE
    nutz = len(dat)
    name = b"clip_test\0"; dat += name
    data_pos = len(dat); dat += struct.pack("<3f", 0.5, -1.25, 1.0 / 3.0)
    keys_pos = len(dat); dat += bytes(range(16))
    subs_pos = len(dat); dat += struct.pack("<fi", 2.5, 7) + struct.pack("<fi", -0.125, -9)
    while len(dat) % 8:
        dat += b"\0"
    basis_pos = len(dat); dat += b"\0" * 16 + struct.pack("<I", H["BaseAnim"]) + b"\0" * 12
    dat += b"\0" * 24                                   # der Basis-Datensatz
    struct.pack_into("<Ii", dat, basis_pos + 32, 0x52415720, 225)
    bname_pos = len(dat)
    dat += b"basis_x\0"
    struct.pack_into("<IIq", dat, basis_pos + 32 + 8, 8, 8, rel(basis_pos + 32 + 8, bname_pos))
    struct.pack_into("<IIq", dat, rec + 0, len(name), len(name), rel(rec + 0, nutz))
    dat[rec + 16:rec + 24] = bytes([0xDE, 0xAD, 0xBE, 0xEF, 1, 2, 3, 4])
    struct.pack_into("<q", dat, rec + 24, (basis_pos - (rec + 24)) | MARKE)
    struct.pack_into("<HB", dat, rec + 32, 226, 1)
    struct.pack_into("<f", dat, rec + 36, 0.1)
    struct.pack_into("<IIq", dat, rec + 40, 3, 4, rel(rec + 40, data_pos))
    struct.pack_into("<IIq", dat, rec + 56, 2, 2, rel(rec + 56, keys_pos))
    struct.pack_into("<IIq", dat, rec + 72, 2, 2, rel(rec + 72, subs_pos))
    while len(dat) % 16:
        dat += b"\0"
    struct.pack_into("<I", dat, 8, len(dat))

    kopf_len = 36 + 16 + 8
    data_offset = (kopf_len + 15) // 16 * 16
    strm_pos = data_offset + 4
    rumpf = ref + bytes(dat)
    strm = b"GD.STRMl" + struct.pack("<III", 16 + len(rumpf), 0, 0)[:8]
    datei = bytearray(struct.pack(">9I", 1, data_offset, 1, 1, 1, 0, 0, 0, 0))
    datei += struct.pack(">Q", 0x1122334455667788) + b"\0" * 8 + struct.pack(">Q", 0x1122334455667788)
    datei += b"\0" * (strm_pos - len(datei))
    datei += strm + rumpf
    open(ziel, "wb").write(bytes(datei))
    print("Testbank geschrieben: %s (%d Byte)" % (ziel, len(datei)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "gd_testbank.res")
