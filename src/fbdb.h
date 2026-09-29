// ============================================================
//  fbdb.h - DbObject, das Baumformat von Frostbite.
//
//  Darin stehen layout.toc, initfs_win32 und die Chunkangaben
//  des Manifests. Ohne diesen Leser gibt es keinen Weg vom
//  Dateinamen zum SHA-1 - und damit keine Auswahlliste.
//
//  Vorlage ist _db_read aus fb_container.py, das wiederum
//  FrostySdk/IO/DbReader.cs folgt.
//
//  Aufbau: ein Byte Typ, dabei sagt Bit 7 "ohne Namen".
//  Listen und Objekte tragen ihre Laenge als LEB128 davor und
//  werden bis genau dorthin gelesen - nicht bis zum ersten
//  Nullbyte. Das ist wichtig: ein verschachteltes Objekt darf
//  seinen Bereich nicht ueberlaufen.
// ============================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fbdb {

enum class Art {
    Ende,       // 0  - Ende einer Liste oder eines Objekts
    Liste,      // 1
    Objekt,     // 2
    Bool,       // 6
    Text,       // 7
    Ganz,       // 8  - int32
    Lang,       // 9  - int64
    Gleit,      // 11 - float
    Doppel,     // 12 - double
    Guid,       // 15 - 16 Byte
    Sha1,       // 16 - 20 Byte
    Bytes,      // 19
};

struct Wert;
using WertPtr = std::shared_ptr<Wert>;

struct Feld {
    std::string name;
    WertPtr wert;
};

struct Wert {
    Art art = Art::Ende;
    bool wahr = false;
    int64_t zahl = 0;
    double gleit = 0.0;
    std::string text;
    std::vector<uint8_t> bytes;      // auch fuer Guid und Sha1
    std::vector<WertPtr> liste;
    std::vector<Feld> felder;        // Objekt, Reihenfolge bleibt erhalten

    // Feld eines Objekts holen - der Name wird OHNE Ruecksicht auf
    // Gross- und Kleinschreibung verglichen, so wie get_ci in Python.
    const Wert* Feldwert(const std::string& name) const;
};

// Eine ganze Datei lesen. Die Entschleierung wird dabei selbst
// erkannt (0x01CED100 / 0x03CED100 -> Inhalt ab 0x22C).
bool LiesDbObjekt(const std::vector<uint8_t>& daten, WertPtr& aus, std::string& fehler);

// Zeile fuer Zeile, zum Vergleichen mit der Python-Referenz.
std::string AlsText(const Wert& w, int nachkomma = 6);

} // namespace fbdb
