// ============================================================
//  swbf2import_fenster.h - das Figurenfenster, von Max aus gesehen.
//
//  Dieser Kopf enthaelt ABSICHTLICH keine Windows- und keine
//  SDK-Typen. Er wird sowohl vom Fenster (Win32, ohne SDK) als auch
//  von der Max-Schicht (SDK, gegen die Attrappe pruefbar) gelesen,
//  und die beiden Welten vertragen sich nicht in einer Datei.
// ============================================================
#pragma once

#include <cstdint>
#include <string>

namespace swbf2 {

struct FensterBruecke {
    void* hInstance = nullptr;   // HINSTANCE der .dlu - dort liegt die Dialogvorlage
    void* eltern = nullptr;      // HWND des Max-Hauptfensters

    // Farbe aus Max' Theme fuer eine Windows-Farbnummer (COLOR_BTNFACE ...).
    // 0xFFFFFFFF heisst "unbekannt" - dann gilt die Windows-Farbe.
    uint32_t (*farbe)(int sysIndex) = nullptr;

    // Eine fertige .fbmodel in die Szene bringen. Laeuft im HAUPTTHREAD,
    // ueber denselben Weg wie Datei -> Importieren.
    bool (*importiere)(const std::wstring& fbmodelPfad, std::wstring& bericht) = nullptr;

    // Eine Zeile ins Import-Protokoll (UTF-8, ohne Zeilenende).
    void (*protokoll)(const char* zeile) = nullptr;

    std::wstring startOrdner;    // leer = gemerkten Spielordner nehmen
    std::wstring ablage;         // %LOCALAPPDATA%\SWBF2Import: Index, Figuren, Einstellungen
    std::wstring version;        // "0.33.0"

    // Nur fuer den Probelauf ohne Spiel und ohne Max (tools/fenster_probe.cpp).
    std::wstring demoFiguren;    // castools Figurenliste statt eines Spielordners
    std::wstring probeSuche;     // Suchfeld vorbelegen
};

// Zeigt das Fenster modal. 1 = mindestens eine Figur importiert,
// 0 = ohne Import geschlossen, -1 = Fenster liess sich nicht oeffnen.
int ZeigeFigurenFenster(const FensterBruecke& b);

// Den Spielordner zu einer Datei darin finden (z.B. Data\layout.toc): so
// weit nach oben gehen, bis ein Ordner Data\layout.toc enthaelt. Leer, wenn
// keiner gefunden wird.
std::wstring SpielordnerAusDatei(const std::wstring& datei);

// %LOCALAPPDATA%\SWBF2Import\start.log schreiben (UTF-8, ueberschreibt):
// Zeitpunkt, Pfad der geladenen .dlu und `text`. START.bat haengt die Datei
// ans START.log - so steht dort, ob und wie das Plugin beim Start geladen hat.
void SchreibeStartprotokoll(void* hInstance, const std::wstring& text);

} // namespace swbf2
