// ============================================================
//  fbindex.h - der Index ueber alle Bundles.
//
//  Das letzte Stueck: aus einem NAMEN wird ein SHA-1. Danach ist
//  der Weg vollstaendig und eine Auswahlliste hat Inhalt.
//
//  Bundle-Namen sind nicht gespeichert, nur ihr FNV-1-Hash. Sie
//  werden in dieser Reihenfolge bestimmt:
//    1. aus Frostys Liste bekannter geteilter Bundles
//    2. aus dem EBX-Namen IM Bundle, dessen fnv1("win32/" + Name)
//       den Hash trifft - erst so geschrieben, dann klein
//    3. sonst der Hash als Hexzahl
//
//  Portiert aus build_index in fb_container.py.
// ============================================================
#pragma once

#include "fbbundle.h"
#include "fbgame.h"

#include <atomic>
#include <map>
#include <string>
#include <vector>

namespace fbindex {

struct BundleInfo {
    size_t nummer = 0;
    uint32_t hash = 0;
    std::string name;
    std::string woher;          // "shared", "ebx", "ebx-lower" oder "hash"
    size_t ebx = 0, res = 0, chunks = 0;
};

struct Eintrag {
    std::string name;
    std::string sha1;
    uint32_t originalSize = 0;
    uint32_t resType = 0;
    uint64_t resRid = 0;
    uint8_t  resMeta[16] = {};
    std::vector<size_t> bundles;
};

struct Index {
    std::vector<BundleInfo> bundles;
    std::map<std::string, Eintrag> ebx;
    std::map<std::string, Eintrag> res;
    std::map<std::string, Eintrag> chunks;   // Schluessel ist die GUID als Text
    std::map<std::string, int> namensherkunft;
};

// Frostys Liste der geteilten Bundles fuer SWBF2.
const std::vector<std::string>& GeteilteBundles();

// Fortschritt und Abbruch fuer einen Arbeitsthread. Alles atomar: der
// Arbeitsthread schreibt, der Hauptthread liest (Core Guidelines CP.2).
struct Fortschritt {
    std::atomic<size_t> fertig{0};
    std::atomic<size_t> gesamt{0};
    std::atomic<bool>   abbrechen{false};
};

// `fs` darf nullptr sein (castool). Ist abbrechen gesetzt, endet der Bau
// vor dem naechsten Bundle mit dem Fehler "abgebrochen".
bool BaueIndex(fbgame::Spiel& spiel, Index& aus, std::string& fehler,
               Fortschritt* fs = nullptr);

// ------------------------------------------------------------
//  Den Index ablegen und wieder einlesen.
//
//  Der Index ueber 4.777 Bundles zu bauen dauert - fuer ein Plugin,
//  das bei jedem Import laeuft, zu lange. Deshalb wird er einmal
//  gebaut und danach aus einer Datei geladen.
//
//  Die KOPFNUMMER des Spiels steht mit drin: nach einem Patch
//  aendert sie sich, und der alte Index wird verworfen statt still
//  falsche Versaetze zu liefern.
// ------------------------------------------------------------
bool SpeichereIndex(const Index& idx, const std::string& pfad, int64_t kopfnummer,
                    std::string& fehler);
bool LadeIndex(Index& aus, const std::string& pfad, int64_t kopfnummer,
               std::string& fehler);

// Klartextname eines RES-Typs ("MeshSet", "Texture", "AssetBank", ...).
// Unbekannte Typen kommen als "0x<HEX>" zurueck.
std::string ResTypName(uint32_t typ);

} // namespace fbindex
