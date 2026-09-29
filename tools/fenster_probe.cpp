// ============================================================
//  fenster_probe.cpp - das Figurenfenster OHNE Max und OHNE Spiel.
//
//  Zeigt das echte Fenster (dieselbe swbf2import_fenster.cpp, dieselbe
//  Dialogvorlage) mit einer Figurenliste aus castool. Damit laesst sich
//  Anordnung, Filter und Zeichnung pruefen, bevor Max im Spiel ist -
//  unter Windows direkt, unter Linux mit MinGW und Wine
//  (tools/PRUEFE_FENSTER.sh).
//
//      fenster_probe.exe <figuren.txt> [suchtext] [hell]
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "swbf2import_fenster.h"

#include <cstdio>
#include <string>

namespace {

// Farben wie im dunklen Theme von Max - NUR fuer den Probelauf. Im Plugin
// kommen sie aus Max selbst (GetCustSysColor).
uint32_t DunklesTheme(int idx) {
    switch (idx) {
    case COLOR_BTNFACE:       return RGB(68, 68, 68);
    case COLOR_BTNTEXT:       return RGB(220, 220, 220);
    case COLOR_WINDOW:        return RGB(43, 43, 43);
    case COLOR_WINDOWTEXT:    return RGB(230, 230, 230);
    case COLOR_HIGHLIGHT:     return RGB(64, 118, 183);
    case COLOR_HIGHLIGHTTEXT: return RGB(255, 255, 255);
    default:                  return 0xFFFFFFFFu;
    }
}

void Protokoll(const char* zeile) {
    std::printf("%s\n", zeile);
    std::fflush(stdout);
}

bool KeinImport(const std::wstring&, std::wstring& bericht) {
    bericht = L"probe run - nothing is imported";
    return false;
}

} // namespace

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    swbf2::FensterBruecke b;
    b.hInstance = h;
    b.farbe = &DunklesTheme;
    b.protokoll = &Protokoll;
    b.importiere = &KeinImport;
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    b.ablage = std::wstring(tmp) + L"SWBF2ImportProbe";
    b.version = L"1.40.0";
    if (argv != nullptr) {
        if (argc > 1) b.demoFiguren = argv[1];
        if (argc > 2) b.probeSuche = argv[2];
        if (argc > 3 && lstrcmpiW(argv[3], L"hell") == 0) b.farbe = nullptr;
        LocalFree(argv);
    }
    const int r = swbf2::ZeigeFigurenFenster(b);
    std::printf("Ergebnis %d\n", r);
    return r < 0 ? 1 : 0;
}
