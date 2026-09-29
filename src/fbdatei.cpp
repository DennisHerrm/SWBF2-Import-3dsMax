// ============================================================
//  fbdatei.cpp - siehe fbdatei.h
// ============================================================
#include "fbdatei.h"

#include <cstdint>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/types.h>
#endif

#include <limits>

namespace fbdatei {

#ifdef _WIN32
std::wstring Breit(const std::string& s) {
    if (s.empty() || s.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n) != n) return std::wstring();
    return w;
}

std::string Utf8(const std::wstring& w) {
    if (w.empty() || w.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                            &s[0], n, nullptr, nullptr) != n) return std::string();
    return s;
}
#else
// Ohne Windows (Werkzeuge unter Linux, Attrappenbau): dieselbe Umrechnung
// von Hand, wchar_t fasst dort einen ganzen Codepunkt.
std::wstring Breit(const std::string& s) {
    std::wstring w;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        size_t n = 1;
        if (c >= 0xF0 && i + 3 < s.size()) { cp = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu); n = 4; }
        else if (c >= 0xE0 && i + 2 < s.size()) { cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu); n = 3; }
        else if (c >= 0xC0 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu); n = 2; }
        w += static_cast<wchar_t>(cp);
        i += n;
    }
    return w;
}

std::string Utf8(const std::wstring& w) {
    std::string s;
    for (wchar_t wc : w) {
        const uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += static_cast<char>(0xE0 | (cp >> 12)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { s += static_cast<char>(0xF0 | (cp >> 18)); s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    return s;
}
#endif

std::FILE* Oeffne(const std::string& p, const char* modus) {
    if (p.empty() || modus == nullptr) return nullptr;
#ifdef _WIN32
    const std::wstring wp = Breit(p);
    if (wp.empty()) return nullptr;
    std::wstring wm;
    for (const char* c = modus; *c != '\0'; ++c) wm += static_cast<wchar_t>(static_cast<unsigned char>(*c));
    return _wfopen(wp.c_str(), wm.c_str());
#else
    return std::fopen(p.c_str(), modus);
#endif
}

bool Springe(std::FILE* f, uint64_t v) {
    if (f == nullptr) return false;
#ifdef _WIN32
    if (v > static_cast<uint64_t>(std::numeric_limits<long long>::max())) return false;
    return _fseeki64(f, static_cast<long long>(v), SEEK_SET) == 0;
#else
    if (v > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) return false;
    return fseeko(f, static_cast<off_t>(v), SEEK_SET) == 0;
#endif
}

bool Groesse(std::FILE* f, uint64_t& aus) {
    if (f == nullptr) return false;
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0) return false;
    const long long n = _ftelli64(f);
    if (n < 0 || _fseeki64(f, 0, SEEK_SET) != 0) return false;
#else
    if (fseeko(f, 0, SEEK_END) != 0) return false;
    const off_t n = ftello(f);
    if (n < 0 || fseeko(f, 0, SEEK_SET) != 0) return false;
#endif
    aus = static_cast<uint64_t>(n);
    return true;
}

bool LiesAlles(const std::string& pfad, std::vector<uint8_t>& aus, std::string& fehler) {
    std::FILE* f = Oeffne(pfad, "rb");
    if (f == nullptr) { fehler = "Datei nicht lesbar: " + pfad; return false; }
    uint64_t n = 0;
    if (!Groesse(f, n)) { std::fclose(f); fehler = "Laenge nicht bestimmbar: " + pfad; return false; }
    if (n > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        std::fclose(f); fehler = "Datei zu gross: " + pfad; return false;
    }
    try {
        aus.resize(static_cast<size_t>(n));
    } catch (...) {
        std::fclose(f); fehler = "zu wenig Speicher fuer " + pfad; return false;
    }
    const size_t gelesen = n ? std::fread(aus.data(), 1, static_cast<size_t>(n), f) : 0;
    std::fclose(f);
    if (gelesen != static_cast<size_t>(n)) { fehler = "Lesefehler: " + pfad; return false; }
    return true;
}

bool LiesBereich(const std::string& pfad, uint64_t versatz, size_t laenge,
                 std::vector<uint8_t>& aus, std::string& fehler) {
    std::FILE* f = Oeffne(pfad, "rb");
    if (f == nullptr) { fehler = "nicht lesbar: " + pfad; return false; }
    if (!Springe(f, versatz)) { std::fclose(f); fehler = "Versatz nicht anfahrbar: " + pfad; return false; }
    try {
        aus.resize(laenge);
    } catch (...) {
        std::fclose(f); fehler = "zu wenig Speicher fuer " + pfad; return false;
    }
    const size_t n = laenge ? std::fread(aus.data(), 1, laenge, f) : 0;
    std::fclose(f);
    if (n != laenge) { fehler = "zu kurz: " + pfad; return false; }
    return true;
}

bool SchreibeAlles(const std::string& pfad, const std::vector<uint8_t>& d, std::string& fehler) {
    std::FILE* f = Oeffne(pfad, "wb");
    if (f == nullptr) { fehler = "kann " + pfad + " nicht schreiben"; return false; }
    const size_t n = d.empty() ? 0 : std::fwrite(d.data(), 1, d.size(), f);
    const bool zu = (std::fclose(f) == 0);
    if (n != d.size() || !zu) { fehler = "Schreibfehler: " + pfad; return false; }
    return true;
}

bool Existiert(const std::string& pfad) {
    std::FILE* f = Oeffne(pfad, "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

} // namespace fbdatei
