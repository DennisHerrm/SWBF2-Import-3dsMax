// Wird NUR bei der Vorabpruefung per -include vorgeschaltet.
// Unter MSVC kommt _wfopen aus <stdio.h>; anderswo gibt es die Funktion
// nicht, und ohne sie liesse sich fbdump.cpp nicht gegen die Attrappe
// uebersetzen. Am Quelltext selbst aendert das nichts.
#pragma once
#include <cstdio>
#include <clocale>
#if !defined(_MSC_VER)
extern "C" inline std::FILE* _wfopen(const wchar_t*, const wchar_t*) { return nullptr; }
extern "C" inline wchar_t* _wgetenv(const wchar_t*) { return nullptr; }
#endif
// MSVC-CRT: Datei mit breitem Pfad loeschen (Attrappe).
inline int _wremove(const wchar_t*) { return 0; }

// Profil-Datei lesen (Attrappe). Ab Max 2019 (MAX_RELEASE 21000) bringt das
// SDK (Util/IniUtil.h) eine gleichnamige MaxSDK::Util::GetPrivateProfileString(W)
// ("should replace every occurrence of the standard WIN32 implementation"); bei
// DH war der Aufruf in 0.47.0 unqualifiziert UND in 0.48.0 mit :: mehrdeutig
// (C2668). GCC laesst die dazu noetige using-Deklaration nicht zu - deshalb
// stehen hier ab 21000 zwei Ueberladungen, zwischen denen JEDER Aufruf mit
// einer ganzen Zahl mehrdeutig ist (long gegen unsigned int). Das Plugin-Modul
// ruft die Profil-API seit 0.48.1 nicht mehr, sondern liest die INI selbst.
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 21000)
inline unsigned long GetPrivateProfileStringW(const wchar_t*, const wchar_t*, const wchar_t*, wchar_t*, long, const wchar_t*) { return 0; }
inline unsigned long GetPrivateProfileStringW(const wchar_t*, const wchar_t*, const wchar_t*, wchar_t*, unsigned int, const wchar_t*) { return 0; }
#else
inline unsigned long GetPrivateProfileStringW(const wchar_t*, const wchar_t*, const wchar_t* vorgabe, wchar_t* aus, unsigned long n, const wchar_t*) {
    unsigned long i = 0;
    if (aus == nullptr || n == 0) return 0;
    for (; vorgabe != nullptr && vorgabe[i] != 0 && i + 1 < n; ++i) aus[i] = vorgabe[i];
    aus[i] = 0;
    return i;
}
#endif
