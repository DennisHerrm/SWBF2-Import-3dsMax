// ============================================================
//  swbf2import_ablage.h - wo das Plugin seine Dateien ablegt.
//
//  Normal: %LOCALAPPDATA%\SWBF2Import (Index, Clip-Cache, Figuren,
//  Texturen, Einstellungen) und "Import swbf2.log" im Downloads-Ordner.
//
//  1.43.0: Testschalter fuer den Gesamttest mit mehreren Max-Instanzen
//  gleichzeitig. Teilen sich zwei Instanzen Ablage und Protokoll, schreiben
//  sie dieselben Texturdateien und mischen ihre Protokollzeilen - der Test
//  erkennt das Importende am Protokoll. Gesetzt werden die Variablen nur vom
//  Testtreiber; ohne sie bleibt alles wie bisher.
//    SWBF2IMPORT_ABLAGE     = Ordner statt %LOCALAPPDATA%\SWBF2Import
//    SWBF2IMPORT_PROTOKOLL  = Datei statt Downloads\Import swbf2.log
// ============================================================
#pragma once

#include <cstdlib>
#include <string>

namespace swbf2ablage {

inline std::wstring Umgebungswert(const wchar_t* name) {
    const wchar_t* w = _wgetenv(name);
    return (w != nullptr) ? std::wstring(w) : std::wstring();
}

inline std::wstring Ordner() {
    const std::wstring test = Umgebungswert(L"SWBF2IMPORT_ABLAGE");
    if (!test.empty()) return test;
    const std::wstring lad = Umgebungswert(L"LOCALAPPDATA");
    return (lad.empty() ? std::wstring(L".") : lad) + L"\\SWBF2Import";
}

inline std::wstring Protokoll() {
    const std::wstring test = Umgebungswert(L"SWBF2IMPORT_PROTOKOLL");
    if (!test.empty()) return test;
    const std::wstring heim = Umgebungswert(L"USERPROFILE");
    return (heim.empty() ? std::wstring(L".") : heim) + L"\\Downloads\\Import swbf2.log";
}

} // namespace swbf2ablage
