// ============================================================
//  fbdatei.h - Dateizugriff mit Unicode-Pfaden und 64-Bit-Versatz.
//
//  Zwei Fehler, die bis 0.32.0 in jeder Schicht steckten:
//
//  1. fseek/ftell rechnen mit long, und long ist unter 64-Bit-
//     Windows nur 32 Bit breit (Microsoft, "Common Visual C++ 64-bit
//     Migration Issues"). Ab 2 GiB wird ein Versatz negativ, der
//     Sprung scheitert - und weil niemand den Rueckgabewert pruefte,
//     wurde still ab Dateianfang gelesen.
//  2. Das schmale fopen geht unter Windows ueber die ANSI-Codepage.
//     Ein Pfad mit Zeichen ausserhalb davon laesst sich so nicht
//     oeffnen.
//
//  Deshalb laeuft JEDER Dateizugriff hierueber: Pfade sind UTF-8,
//  unter Windows werden sie nach UTF-16 gewandelt und mit _wfopen
//  geoeffnet, Versaetze sind uint64_t, und jeder Rueckgabewert wird
//  geprueft.
// ============================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace fbdatei {

std::FILE* Oeffne(const std::string& utf8Pfad, const char* modus);

bool Springe(std::FILE* f, uint64_t versatz);
bool Groesse(std::FILE* f, uint64_t& aus);     // steht danach wieder auf 0

bool LiesAlles(const std::string& utf8Pfad, std::vector<uint8_t>& aus, std::string& fehler);
bool LiesBereich(const std::string& utf8Pfad, uint64_t versatz, size_t laenge,
                 std::vector<uint8_t>& aus, std::string& fehler);
bool SchreibeAlles(const std::string& utf8Pfad, const std::vector<uint8_t>& daten,
                   std::string& fehler);
bool Existiert(const std::string& utf8Pfad);

std::wstring Breit(const std::string& utf8);
std::string  Utf8(const std::wstring& breit);


} // namespace fbdatei
