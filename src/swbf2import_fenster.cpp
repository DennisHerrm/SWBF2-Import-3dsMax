// ============================================================
//  swbf2import_fenster.cpp - das Figurenfenster (Win32, modal).
//
//  Aufbau nach der SDK-Regel (MaxSDK::ThreadingDebuggingTools): ein
//  Importer zeigt im HAUPTTHREAD einen modalen Dialog mit Fortschritt
//  und arbeitet in EINEM Arbeitsthread. Der Arbeitsthread ruft kein
//  einziges SDK-Stueck auf - er oeffnet das Spiel, laedt oder baut den
//  Index und setzt die Figur zusammen. Die Szene fasst nur der
//  Hauptthread an (ueber FensterBruecke::importiere).
//
//  Diese Datei kennt KEIN Max-SDK. Farben, Import und Protokoll kommen
//  ueber die Bruecke herein. Dadurch uebersetzt sie mit MinGW gegen
//  echte Windows-Header und laeuft im Probelauf ohne Max
//  (tools/fenster_probe.cpp).
//
//  Aussehen: alle Farben aus Max' Theme (GetCustSysColor ueber die
//  Bruecke), Knoepfe und Liste selbst gezeichnet, damit sie im dunklen
//  Theme nicht als helle Windows-Kaesten herausstechen. Kontraste
//  werden gemessen (WCAG 2.2: Text mindestens 4,5:1) und ins Protokoll
//  geschrieben.
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl.h>

#include "swbf2import_fenster.h"
#include "swbf2import_ablage.h"
#include "swbf2import_res.h"

#include "fbauswahl.h"
#include "fbfahrzeug.h"
#include "fbdatei.h"
#include "fbfigur.h"
#include "fbgesicht.h"
#include "fbgame.h"
#include "fbindex.h"
#include "fbbeipack.h"
#include "fbmaterial.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace swbf2 {
namespace {

constexpr UINT_PTR kTimer = 1;
constexpr UINT kTakt = 80;                 // ms zwischen zwei Blicken auf den Arbeitsthread
constexpr int kKategorien = 6;                                   // 0.97.0: Vehicles dazu
const wchar_t* const kReiter[kKategorien] = { L"Heroes", L"Light side", L"Dark side", L"Other", L"Vehicles", L"All" };

const char* const kSkelett = fbauswahl::kGemeinsamesSkelett;   // 1.43.0: Suche in fbauswahl
const wchar_t* const kSkelettKurz = L"Walrus_HumanMale";

// ------------------------------------------------------------
//  Text
// ------------------------------------------------------------
std::wstring Breit(const std::string& s) { return fbdatei::Breit(s); }
std::string Utf8(const std::wstring& w) { return fbdatei::Utf8(w); }

std::string KleinA(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// "win32/characters/hero/anakin/anakin_01/vur_anakin_01_bpb" -> "vur_anakin_01_bpb"
std::wstring Blatt(const std::string& name) {
    const size_t p = name.find_last_of('/');
    return Breit(p == std::string::npos ? name : name.substr(p + 1));
}

// ... -> "hero/anakin/anakin_01"
std::wstring Ordnerteil(const std::string& name) {
    std::string s = name;
    if (s.rfind("win32/", 0) == 0) s = s.substr(6);
    if (s.rfind("characters/", 0) == 0) s = s.substr(11);
    const size_t p = s.find_last_of('/');
    return Breit(p == std::string::npos ? std::string() : s.substr(0, p));
}

// Eine Nachkommastelle mit PUNKT - fuers Protokoll, unabhaengig davon, auf
// welche Region Max die C-Laufzeit gestellt hat (CODING.md Abschnitt 8).
std::string ZehntelPunkt(double x) {
    const long long z = std::llround(x * 10.0);
    return std::to_string(z / 10) + "." + std::to_string(std::llabs(z % 10));
}

// Fuer die Oberflaeche dagegen die Region des Anwenders (swprintf folgt ihr).
std::wstring ZehntelOrt(double x) {
    wchar_t b[32] = {};
    std::swprintf(b, 32, L"%.1f", x);
    return b;
}

// ------------------------------------------------------------
//  Farben und Kontrast
// ------------------------------------------------------------
COLORREF Mische(COLORREF a, COLORREF b, double t) {
    auto kanal = [t](int x, int y) {
        const double v = x + (y - x) * t;
        return static_cast<BYTE>(v <= 0.0 ? 0.0 : (v >= 255.0 ? 255.0 : v + 0.5));
    };
    return RGB(kanal(GetRValue(a), GetRValue(b)), kanal(GetGValue(a), GetGValue(b)),
               kanal(GetBValue(a), GetBValue(b)));
}

double Linear(int v) {
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double Leuchtdichte(COLORREF c) {
    return 0.2126 * Linear(GetRValue(c)) + 0.7152 * Linear(GetGValue(c)) + 0.0722 * Linear(GetBValue(c));
}

// Kontrastverhaeltnis nach WCAG 2.2, 1:1 bis 21:1.
double Kontrast(COLORREF a, COLORREF b) {
    double x = Leuchtdichte(a), y = Leuchtdichte(b);
    if (x < y) std::swap(x, y);
    return (x + 0.05) / (y + 0.05);
}

// Gedaempfte Schrift: so wenig Schrift wie noetig, aber mindestens `mindest`
// Kontrast. Ein fester Mischwert haette im dunklen Theme 3,8:1 ergeben -
// gemessen, nicht geschaetzt, und unter der Grenze von 4,5:1.
COLORREF Gedaempft(COLORREF grund, COLORREF schrift, double mindest) {
    for (int i = 50; i <= 100; i += 2) {
        const COLORREF c = Mische(grund, schrift, i / 100.0);
        if (Kontrast(c, grund) >= mindest) return c;
    }
    return schrift;
}

std::string Verhaeltnis(double k) { return ZehntelPunkt(k) + ":1"; }

struct Palette {
    COLORREF grund = 0, text = 0, dim = 0, feld = 0, feldText = 0, feldDim = 0;
    COLORREF auswahl = 0, auswahlText = 0, auswahlDim = 0, knopf = 0, fehler = 0;
    HBRUSH pinselGrund = nullptr, pinselFeld = nullptr;
    bool dunkel = false;

    void Baue(uint32_t (*farbe)(int)) {
        auto hole = [farbe](int idx) -> COLORREF {
            if (farbe != nullptr) {
                const uint32_t c = farbe(idx);
                if (c != 0xFFFFFFFFu) return static_cast<COLORREF>(c);
            }
            return GetSysColor(idx);
        };
        grund = hole(COLOR_BTNFACE);
        text = hole(COLOR_BTNTEXT);
        feld = hole(COLOR_WINDOW);
        feldText = hole(COLOR_WINDOWTEXT);
        auswahl = hole(COLOR_HIGHLIGHT);
        auswahlText = hole(COLOR_HIGHLIGHTTEXT);
        // Reicht der Kontrast der Auswahlschrift nicht (WCAG: 4,5:1), nimmt
        // das Fenster Schwarz oder Weiss - je nachdem, was mehr Kontrast hat.
        // Gemessen unter Wine mit Windows' Standardblau: Weiss nur 3,1:1.
        if (Kontrast(auswahlText, auswahl) < 4.5) {
            auswahlText = Kontrast(RGB(0, 0, 0), auswahl) > Kontrast(RGB(255, 255, 255), auswahl)
                              ? RGB(0, 0, 0) : RGB(255, 255, 255);
        }
        dunkel = Leuchtdichte(grund) < 0.18;
        dim = Gedaempft(grund, text, 4.5);
        feldDim = Gedaempft(feld, feldText, 4.5);
        auswahlDim = Gedaempft(auswahl, auswahlText, 4.5);
        knopf = Mische(grund, text, dunkel ? 0.13 : 0.07);
        fehler = dunkel ? RGB(0xF2, 0x8B, 0x7C) : RGB(0xB0, 0x28, 0x1E);
        Frei();
        pinselGrund = CreateSolidBrush(grund);
        pinselFeld = CreateSolidBrush(feld);
    }
    void Frei() {
        if (pinselGrund != nullptr) DeleteObject(pinselGrund);
        if (pinselFeld != nullptr) DeleteObject(pinselFeld);
        pinselGrund = pinselFeld = nullptr;
    }
};

// ------------------------------------------------------------
//  Der Arbeitsthread
//
//  Genau EIN Besitzer, der den Thread immer wieder einsammelt:
//  - ein std::thread, der noch laeuft, darf nicht zerstoert werden
//    (sonst std::terminate - und Max ist weg), also join im Destruktor;
//  - keine Ausnahme verlaesst die Threadfunktion (sonst ebenfalls
//    std::terminate), also catch (...) mit Fehlertext als Ergebnis;
//  - nie detach (Core Guidelines CP.26);
//  - geteilt wird nur Atomares, das Ergebnis EINMAL unter einer Sperre.
// ------------------------------------------------------------
struct Ergebnis {
    bool ok = false;
    std::string fehler;
    std::string hinweis;
    double sekunden = 0.0;
    // Laden
    std::unique_ptr<fbgame::Spiel> spiel;
    std::unique_ptr<fbindex::Index> index;
    std::vector<fbauswahl::Figur> figuren;
    bool ausAblage = false;
    std::string ablageGrund;
    // Figur
    std::wstring figurPfad;
    std::string figurZeile;
    std::vector<std::string> hinweise;
    std::vector<std::string> rigKandidaten;
    std::vector<std::string> materialProtokoll;
    size_t bones = 0, meshes = 0, vertices = 0;
};

enum class Aufgabe { Keine, Laden, Figur };
enum Phase : int { kFrei = 0, kKataloge, kIndexLaden, kIndexBauen, kFigurenListe, kFigurBauen, kTexturen };

class Arbeit {
public:
    Arbeit() = default;
    Arbeit(const Arbeit&) = delete;
    Arbeit& operator=(const Arbeit&) = delete;
    ~Arbeit() { Stopp(); }

    template <class F>
    void Starte(Aufgabe a, F aufgabe) {
        Stopp();
        was_ = a;
        fs_.fertig = 0;
        fs_.gesamt = 0;
        fs_.abbrechen = false;
        phase_ = kFrei;
        {
            std::lock_guard<std::mutex> sperre(m_);
            erg_ = Ergebnis();
        }
        laeuft_ = true;
        try {
            faden_ = std::thread([this, aufgabe]() {
                Ergebnis e;
                const auto t0 = std::chrono::steady_clock::now();
                try {
                    aufgabe(e, fs_, phase_);
                } catch (const std::exception& x) {
                    e.ok = false;
                    e.fehler = x.what();
                } catch (...) {
                    e.ok = false;
                    e.fehler = "unbekannter Fehler im Arbeitsthread";
                }
                e.sekunden = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                {
                    std::lock_guard<std::mutex> sperre(m_);
                    erg_ = std::move(e);
                }
                laeuft_ = false;
            });
        } catch (...) {
            // std::thread kann selbst werfen (keine Ressourcen mehr).
            std::lock_guard<std::mutex> sperre(m_);
            erg_.ok = false;
            erg_.fehler = "Arbeitsthread liess sich nicht starten";
            laeuft_ = false;
        }
    }
    void Abbrechen() { fs_.abbrechen = true; }
    void Stopp() {
        fs_.abbrechen = true;
        if (faden_.joinable()) faden_.join();
    }
    Ergebnis Hole() {
        if (faden_.joinable()) faden_.join();
        std::lock_guard<std::mutex> sperre(m_);
        return std::move(erg_);
    }
    bool Laeuft() const { return laeuft_.load(); }
    Aufgabe Was() const { return was_; }
    int Phase() const { return phase_.load(); }
    size_t Fertig() const { return fs_.fertig.load(); }
    size_t Gesamt() const { return fs_.gesamt.load(); }

private:
    std::thread faden_;
    std::atomic<bool> laeuft_{false};
    std::atomic<int> phase_{0};
    fbindex::Fortschritt fs_;
    std::mutex m_;
    Ergebnis erg_;
    Aufgabe was_ = Aufgabe::Keine;
};

// ------------------------------------------------------------
//  Zustand des Fensters
// ------------------------------------------------------------
enum : unsigned { kL = 1, kO = 2, kR = 4, kU = 8 };   // Anker: links, oben, rechts, unten

struct Anker {
    int id;
    RECT start;
    unsigned flags;
};

struct Fenster {
    FensterBruecke b;
    HWND hDlg = nullptr;
    Palette pal;
    HFONT fontNormal = nullptr;          // gehoert dem Dialog
    HFONT fontFett = nullptr;            // eigener, wird freigegeben
    int zeilenHoehe = 16;
    std::wstring ini;
    std::wstring spielordner;
    std::unique_ptr<fbgame::Spiel> spiel;
    std::unique_ptr<fbindex::Index> index;
    std::vector<fbauswahl::Figur> figuren;
    std::string geladenOrdner, geladenAblage;    // 1.43.0: woraus spiel/index stammen (Sitzungsvorrat)
    std::vector<size_t> sichtbar;
    size_t anzahl[kKategorien] = {};
    int kategorie = 0;
    bool erstePerson = false;
    bool probelauf = false;
    bool importiert = false;
    bool abbruchVerlangt = false;
    Arbeit arbeit;
    std::wstring status;
    bool statusFehler = false;
    std::vector<Anker> anker;
    SIZE startClient{ 0, 0 };
    SIZE startFenster{ 0, 0 };
};

Fenster* Zustand(HWND hDlg) {
    return reinterpret_cast<Fenster*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
}

void Log(const Fenster& f, const std::string& zeile) {
    if (f.b.protokoll != nullptr) f.b.protokoll(zeile.c_str());
}

// ------------------------------------------------------------
//  Einstellungen (INI in der Ablage)
// ------------------------------------------------------------
std::wstring LiesIni(const Fenster& f, const wchar_t* schluessel, const wchar_t* vorgabe = L"") {
    std::vector<wchar_t> puffer(4096, L'\0');
    GetPrivateProfileStringW(L"Fenster", schluessel, vorgabe, puffer.data(),
                             static_cast<DWORD>(puffer.size()), f.ini.c_str());
    return std::wstring(puffer.data());
}

void SchreibeIni(const Fenster& f, const wchar_t* schluessel, const std::wstring& wert) {
    if (GetFileAttributesW(f.ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Neue Datei als UTF-16 mit BOM anlegen - sonst schreibt Windows sie
        // in der ANSI-Codepage, und ein Pfad mit Sonderzeichen ginge verloren.
        HANDLE h = CreateFileW(f.ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const BYTE bom[2] = { 0xFF, 0xFE };
            DWORD geschrieben = 0;
            WriteFile(h, bom, 2, &geschrieben, nullptr);
            CloseHandle(h);
        }
    }
    WritePrivateProfileStringW(L"Fenster", schluessel, wert.c_str(), f.ini.c_str());
}

// ------------------------------------------------------------
//  Spielordner
// ------------------------------------------------------------
bool IstSpielordner(const std::wstring& o) {
    if (o.empty()) return false;
    const DWORD a = GetFileAttributesW((o + L"\\Data\\layout.toc").c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Wer den Data-Ordner selbst waehlt, meint den Ordner darueber.
std::wstring Bereinige(std::wstring o) {
    while (!o.empty() && (o.back() == L'\\' || o.back() == L'/')) o.pop_back();
    if (!IstSpielordner(o)) {
        const size_t p = o.find_last_of(L"\\/");
        if (p != std::wstring::npos && _wcsicmp(o.c_str() + p + 1, L"Data") == 0 &&
            IstSpielordner(o.substr(0, p))) {
            o = o.substr(0, p);
        }
    }
    return o;
}

std::wstring Umgebung(const wchar_t* name) {
    const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return std::wstring();
    std::wstring s(n, L'\0');
    const DWORD r = GetEnvironmentVariableW(name, &s[0], n);
    s.resize(r < n ? r : 0);
    return s;
}

std::wstring SucheSpiel() {
    const std::wstring basis[] = { Umgebung(L"ProgramFiles"), Umgebung(L"ProgramFiles(x86)"),
                                   L"C:\\Program Files", L"C:\\Program Files (x86)",
                                   L"D:\\Program Files", L"D:\\Program Files (x86)" };
    const wchar_t* const rest[] = { L"\\EA Games\\STAR WARS Battlefront II",
                                    L"\\Origin Games\\STAR WARS Battlefront II",
                                    L"\\Steam\\steamapps\\common\\STAR WARS Battlefront II" };
    for (const std::wstring& b : basis) {
        if (b.empty()) continue;
        for (const wchar_t* r : rest) {
            const std::wstring k = b + r;
            if (IstSpielordner(k)) return k;
        }
    }
    return std::wstring();
}

bool WaehleOrdner(HWND besitzer, std::wstring& ordner) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                reinterpret_cast<void**>(&dlg))) || dlg == nullptr) {
        return false;
    }
    DWORD opt = 0;
    if (SUCCEEDED(dlg->GetOptions(&opt))) dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Choose the STAR WARS Battlefront II folder");
    if (!ordner.empty()) {
        IShellItem* start = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(ordner.c_str(), nullptr, IID_IShellItem,
                                                  reinterpret_cast<void**>(&start))) && start != nullptr) {
            dlg->SetFolder(start);
            start->Release();
        }
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(besitzer))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item != nullptr) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p != nullptr) {
                ordner = p;
                CoTaskMemFree(p);
                ok = true;
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

// ------------------------------------------------------------
//  Masse und Anordnung
// ------------------------------------------------------------
int ZeilenHoehe(HWND hDlg) {
    HFONT hf = reinterpret_cast<HFONT>(SendMessageW(hDlg, WM_GETFONT, 0, 0));
    HDC dc = GetDC(hDlg);
    HGDIOBJ alt = SelectObject(dc, hf != nullptr ? static_cast<HGDIOBJ>(hf) : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, alt);
    ReleaseDC(hDlg, dc);
    return std::max(12, static_cast<int>(tm.tmHeight + tm.tmExternalLeading));
}

int EintragsHoehe(HWND hDlg) {
    const int z = ZeilenHoehe(hDlg);
    return 2 * z + std::max(4, z / 3) + 2;
}

void MerkeAnker(Fenster& f) {
    struct { int id; unsigned fl; } const liste[] = {
        { IDC_ORDNER_LABEL, kL | kO }, { IDC_ORDNER, kL | kO | kR }, { IDC_DURCHSUCHEN, kO | kR },
        { IDC_FORTSCHRITT, kL | kO | kR },
        { IDC_TAB0, kL | kO }, { IDC_TAB1, kL | kO }, { IDC_TAB2, kL | kO }, { IDC_TAB3, kL | kO },
        { IDC_TAB4, kL | kO }, { IDC_TAB5, kL | kO }, { IDC_ERSTEPERSON, kO | kR },
        { IDC_SUCHE_LABEL, kL | kO }, { IDC_SUCHE, kL | kO | kR }, { IDC_ANZAHL, kO | kR },
        { IDC_LISTE, kL | kO | kR | kU }, { IDC_DETAIL, kL | kR | kU }, { IDC_FUSS, kL | kR | kU },
        { IDOK, kR | kU }, { IDCANCEL, kR | kU },
    };
    RECT c{};
    GetClientRect(f.hDlg, &c);
    f.startClient = { c.right - c.left, c.bottom - c.top };
    RECT w{};
    GetWindowRect(f.hDlg, &w);
    f.startFenster = { w.right - w.left, w.bottom - w.top };
    for (const auto& e : liste) {
        HWND h = GetDlgItem(f.hDlg, e.id);
        if (h == nullptr) continue;
        RECT r{};
        GetWindowRect(h, &r);
        MapWindowPoints(nullptr, f.hDlg, reinterpret_cast<POINT*>(&r), 2);
        f.anker.push_back({ e.id, r, e.fl });
    }
}

void Ordne(Fenster& f, int cx, int cy) {
    if (f.anker.empty()) return;
    const int dx = cx - f.startClient.cx;
    const int dy = cy - f.startClient.cy;
    HDWP h = BeginDeferWindowPos(static_cast<int>(f.anker.size()));
    for (const Anker& a : f.anker) {
        RECT n = a.start;
        if (a.flags & kR) {
            if (a.flags & kL) n.right += dx; else { n.left += dx; n.right += dx; }
        }
        if (a.flags & kU) {
            if (a.flags & kO) n.bottom += dy; else { n.top += dy; n.bottom += dy; }
        }
        HWND c = GetDlgItem(f.hDlg, a.id);
        if (h != nullptr && c != nullptr) {
            h = DeferWindowPos(h, c, nullptr, n.left, n.top, n.right - n.left, n.bottom - n.top,
                               SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    if (h != nullptr) EndDeferWindowPos(h);
    InvalidateRect(f.hDlg, nullptr, TRUE);
}

// ------------------------------------------------------------
//  Zeichnen
// ------------------------------------------------------------
void ZeichneKnopf(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    FillRect(dc, &r, f.pal.pinselGrund);
    const int id = static_cast<int>(d.CtlID);
    const bool gedrueckt = (d.itemState & ODS_SELECTED) != 0;
    const bool aus = (d.itemState & ODS_DISABLED) != 0;
    const bool fokus = (d.itemState & ODS_FOCUS) != 0 && (d.itemState & ODS_NOFOCUSRECT) == 0;
    const bool reiter = id >= IDC_TAB0 && id < IDC_TAB0 + kKategorien;
    const bool gewaehlt = (reiter && id - IDC_TAB0 == f.kategorie) || (id == IDC_ERSTEPERSON && f.erstePerson);
    const bool primaer = (id == IDOK) && !aus;
    const bool betont = gewaehlt || primaer;

    COLORREF flaeche = betont ? f.pal.auswahl : f.pal.knopf;
    if (gedrueckt) flaeche = Mische(flaeche, f.pal.text, 0.18);
    if (aus && gewaehlt) flaeche = Mische(f.pal.auswahl, f.pal.grund, 0.55);
    const COLORREF schrift = aus ? f.pal.dim : (betont ? f.pal.auswahlText : f.pal.text);
    COLORREF linie = Mische(flaeche, f.pal.text, betont ? 0.25 : 0.20);
    if (fokus) linie = betont ? f.pal.auswahlText : f.pal.auswahl;

    const int rund = std::max(4, static_cast<int>(r.bottom - r.top) / 4);   // RECT ist LONG, nicht int
    HBRUSH pinsel = CreateSolidBrush(flaeche);
    HPEN stift = CreatePen(PS_SOLID, 1, linie);
    HGDIOBJ altP = SelectObject(dc, pinsel);
    HGDIOBJ altS = SelectObject(dc, stift);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rund, rund);
    SelectObject(dc, altP);
    SelectObject(dc, altS);
    DeleteObject(pinsel);
    DeleteObject(stift);

    wchar_t text[160] = {};
    GetWindowTextW(d.hwndItem, text, 160);
    HGDIOBJ altF = SelectObject(dc, f.fontNormal);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, schrift);
    RECT t = r;
    InflateRect(&t, -4, 0);
    DrawTextW(dc, text, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, altF);
}

void ZeichneFortschritt(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    FillRect(dc, &r, f.pal.pinselGrund);
    const bool laeuft = f.arbeit.Laeuft();
    const int balken = std::max(3, static_cast<int>(r.bottom - r.top) / 6);
    RECT t = r;
    if (laeuft) t.bottom -= balken + 2;
    HGDIOBJ altF = SelectObject(dc, f.fontNormal);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, f.statusFehler ? f.pal.fehler : f.pal.text);
    DrawTextW(dc, f.status.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, altF);
    if (!laeuft) return;

    const RECT spur = { r.left, r.bottom - balken, r.right, r.bottom };
    HBRUSH pinselSpur = CreateSolidBrush(f.pal.knopf);
    FillRect(dc, &spur, pinselSpur);
    DeleteObject(pinselSpur);
    const int breite = static_cast<int>(spur.right - spur.left);
    RECT fuell = spur;
    const size_t gesamt = f.arbeit.Gesamt();
    const size_t fertig = std::min(f.arbeit.Fertig(), gesamt);
    if (f.arbeit.Phase() == kIndexBauen && gesamt > 0) {
        fuell.right = spur.left + static_cast<int>(static_cast<double>(breite) * static_cast<double>(fertig) /
                                                   static_cast<double>(gesamt));
    } else {
        // Ohne bekannte Gesamtmenge: ein wandernder Block. Ein stehender
        // Balken wuerde "haengt" sagen (Nielsen: nie stehen bleiben).
        const int block = std::max(20, breite / 5);
        const int weg = breite + block;
        const int pos = static_cast<int>((GetTickCount() / 4) % static_cast<DWORD>(weg)) - block;
        fuell.left = std::max(spur.left, spur.left + pos);
        fuell.right = std::min(spur.right, spur.left + pos + block);
    }
    if (fuell.right > fuell.left) {
        HBRUSH pinselFuell = CreateSolidBrush(f.pal.auswahl);
        FillRect(dc, &fuell, pinselFuell);
        DeleteObject(pinselFuell);
    }
}

void ZeichneEintrag(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    const bool sel = (d.itemState & ODS_SELECTED) != 0;
    HBRUSH hg = CreateSolidBrush(sel ? f.pal.auswahl : f.pal.feld);
    FillRect(dc, &r, hg);
    DeleteObject(hg);
    if (d.itemID == static_cast<UINT>(-1) || d.itemID >= f.sichtbar.size()) return;

    const fbauswahl::Figur& fi = f.figuren[f.sichtbar[d.itemID]];
    const int rand = std::max(4, f.zeilenHoehe / 3);
    const RECT z1 = { r.left + rand, r.top + rand / 2, r.right - rand, r.top + rand / 2 + f.zeilenHoehe };
    const RECT z2 = { z1.left, z1.bottom, z1.right, z1.bottom + f.zeilenHoehe };
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ altF = SelectObject(dc, f.fontNormal);

    // Rechts in Zeile 1: die Zahlen, gedaempft.
    const std::wstring meta = std::to_wstring(fi.meshsets) + L" MeshSets  \u00B7  " +
                              std::to_wstring(fi.texturen) + L" textures";
    RECT mess = z1;
    DrawTextW(dc, meta.c_str(), -1, &mess, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    const int mbreite = static_cast<int>(mess.right - mess.left);
    RECT mz = { z1.right - mbreite, z1.top, z1.right, z1.bottom };
    SetTextColor(dc, sel ? f.pal.auswahlDim : f.pal.feldDim);
    DrawTextW(dc, meta.c_str(), -1, &mz, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    // Links in Zeile 1: der Name, halbfett.
    RECT nz = z1;
    nz.right = mz.left - 2 * rand;
    SelectObject(dc, f.fontFett != nullptr ? f.fontFett : f.fontNormal);
    SetTextColor(dc, sel ? f.pal.auswahlText : f.pal.feldText);
    const std::wstring blatt = Blatt(fi.name);
    DrawTextW(dc, blatt.c_str(), -1, &nz, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Zeile 2: wo die Figur im Spiel liegt.
    SelectObject(dc, f.fontNormal);
    std::wstring unter = Ordnerteil(fi.name);
    const std::string klasse = fbauswahl::Klassenname(fi.name);          // 0.49.0: Standardklassen lesbar
    if (!klasse.empty()) unter = fbdatei::Breit(klasse) + L"  \u00B7  " + unter;
    if (fi.erstePerson) unter += L"  \u00B7  first person";
    RECT uz = z2;
    SetTextColor(dc, sel ? f.pal.auswahlDim : f.pal.feldDim);
    DrawTextW(dc, unter.c_str(), -1, &uz, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, altF);

    if (!sel) {
        HPEN p = CreatePen(PS_SOLID, 1, Mische(f.pal.feld, f.pal.feldText, 0.08));
        HGDIOBJ a = SelectObject(dc, p);
        MoveToEx(dc, r.left + rand, r.bottom - 1, nullptr);
        LineTo(dc, r.right - rand, r.bottom - 1);
        SelectObject(dc, a);
        DeleteObject(p);
    }
    if ((d.itemState & ODS_FOCUS) != 0 && (d.itemState & ODS_NOFOCUSRECT) == 0) {
        RECT fr = r;
        DrawFocusRect(dc, &fr);
    }
}

// Such- und Listenfeld haben keinen Windows-Rand (im dunklen Theme ein
// heller Strich), sondern einen feinen in Max' Farben, ein Pixel ausserhalb.
void ZeichneRahmen(const Fenster& f, HDC dc) {
    HPEN stift = CreatePen(PS_SOLID, 1, Mische(f.pal.grund, f.pal.text, 0.28));
    HGDIOBJ altS = SelectObject(dc, stift);
    HGDIOBJ altP = SelectObject(dc, GetStockObject(NULL_BRUSH));
    for (int id : { IDC_SUCHE, IDC_LISTE }) {
        HWND h = GetDlgItem(f.hDlg, id);
        if (h == nullptr) continue;
        RECT r{};
        GetWindowRect(h, &r);
        MapWindowPoints(nullptr, f.hDlg, reinterpret_cast<POINT*>(&r), 2);
        Rectangle(dc, r.left - 1, r.top - 1, r.right + 1, r.bottom + 1);
    }
    SelectObject(dc, altP);
    SelectObject(dc, altS);
    DeleteObject(stift);
}

// ------------------------------------------------------------
//  Liste, Knoepfe, Anzeige
// ------------------------------------------------------------
int Auswahl(const Fenster& f) {
    const LRESULT s = SendDlgItemMessageW(f.hDlg, IDC_LISTE, LB_GETCURSEL, 0, 0);
    if (s == LB_ERR || s < 0 || static_cast<size_t>(s) >= f.sichtbar.size()) return -1;
    return static_cast<int>(s);
}

void Bedienbarkeit(Fenster& f) {
    const bool laeuft = f.arbeit.Laeuft();
    const bool liste = !f.figuren.empty() && !laeuft;
    EnableWindow(GetDlgItem(f.hDlg, IDC_DURCHSUCHEN), !laeuft && !f.probelauf);
    for (int k = 0; k < kKategorien; ++k) EnableWindow(GetDlgItem(f.hDlg, IDC_TAB0 + k), liste);
    EnableWindow(GetDlgItem(f.hDlg, IDC_ERSTEPERSON), liste);
    EnableWindow(GetDlgItem(f.hDlg, IDC_SUCHE), liste);
    EnableWindow(GetDlgItem(f.hDlg, IDC_LISTE), liste);
    EnableWindow(GetDlgItem(f.hDlg, IDOK), liste && Auswahl(f) >= 0 && f.spiel != nullptr && f.index != nullptr);
    SetDlgItemTextW(f.hDlg, IDCANCEL, laeuft ? L"Cancel" : L"Close");
    InvalidateRect(GetDlgItem(f.hDlg, IDOK), nullptr, FALSE);
    InvalidateRect(GetDlgItem(f.hDlg, IDCANCEL), nullptr, FALSE);
}

void ZeigeDetail(Fenster& f) {
    const int sel = Auswahl(f);
    std::wstring t;
    if (sel >= 0) t = Breit(f.figuren[f.sichtbar[static_cast<size_t>(sel)]].name);
    else if (!f.sichtbar.empty()) t = L"Double-click a character or select it and press Import.";
    else if (!f.figuren.empty()) t = L"Nothing matches - try another tab or a shorter search.";
    SetDlgItemTextW(f.hDlg, IDC_DETAIL, t.c_str());
}

std::vector<std::string> Suchwoerter(HWND hDlg) {
    wchar_t puffer[512] = {};
    GetDlgItemTextW(hDlg, IDC_SUCHE, puffer, 512);
    const std::string s = KleinA(Utf8(puffer));
    std::vector<std::string> woerter;
    std::string wort;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!wort.empty()) { woerter.push_back(wort); wort.clear(); }
        } else {
            wort += c;
        }
    }
    if (!wort.empty()) woerter.push_back(wort);
    return woerter;
}

void FuelleListe(Fenster& f) {
    HWND lb = GetDlgItem(f.hDlg, IDC_LISTE);
    std::string vorher;
    const int alt = Auswahl(f);
    if (alt >= 0) vorher = f.figuren[f.sichtbar[static_cast<size_t>(alt)]].name;

    const std::vector<std::string> woerter = Suchwoerter(f.hDlg);
    f.sichtbar.clear();
    std::fill(std::begin(f.anzahl), std::end(f.anzahl), size_t(0));
    for (size_t i = 0; i < f.figuren.size(); ++i) {
        const fbauswahl::Figur& fi = f.figuren[i];
        if (fi.erstePerson && !f.erstePerson) continue;
        ++f.anzahl[static_cast<int>(fi.art)];
        ++f.anzahl[kKategorien - 1];
        if (f.kategorie < kKategorien - 1 && static_cast<int>(fi.art) != f.kategorie) continue;
        if (!woerter.empty()) {
            const std::string k = KleinA(fi.name);
            bool alle = true;
            for (const std::string& w : woerter) {
                if (k.find(w) == std::string::npos) { alle = false; break; }
            }
            if (!alle) continue;
        }
        f.sichtbar.push_back(i);
    }
    for (int k = 0; k < kKategorien; ++k) {
        std::wstring t = kReiter[k];
        if (!f.figuren.empty()) t += L"  " + std::to_wstring(f.anzahl[k]);
        SetDlgItemTextW(f.hDlg, IDC_TAB0 + k, t.c_str());
        InvalidateRect(GetDlgItem(f.hDlg, IDC_TAB0 + k), nullptr, FALSE);
    }
    InvalidateRect(GetDlgItem(f.hDlg, IDC_ERSTEPERSON), nullptr, FALSE);

    SendMessageW(lb, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lb, LB_SETCOUNT, static_cast<WPARAM>(f.sichtbar.size()), 0);
    int neu = -1;
    if (!vorher.empty()) {
        for (size_t j = 0; j < f.sichtbar.size(); ++j) {
            if (f.figuren[f.sichtbar[j]].name == vorher) { neu = static_cast<int>(j); break; }
        }
    }
    SendMessageW(lb, LB_SETCURSEL, static_cast<WPARAM>(neu), 0);
    SendMessageW(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, nullptr, TRUE);

    std::wstring z;
    if (!f.figuren.empty()) {
        z = std::to_wstring(f.sichtbar.size()) + L" of " + std::to_wstring(f.anzahl[f.kategorie]);
    }
    SetDlgItemTextW(f.hDlg, IDC_ANZAHL, z.c_str());
    ZeigeDetail(f);
    Bedienbarkeit(f);
}

void ZeigeStatus(Fenster& f) {
    HWND h = GetDlgItem(f.hDlg, IDC_FORTSCHRITT);
    InvalidateRect(h, nullptr, FALSE);
}

std::wstring PhasenText(const Fenster& f) {
    if (f.abbruchVerlangt) return L"Cancelling\u2026";
    switch (f.arbeit.Phase()) {
    case kKataloge:     return L"Reading the game catalogs\u2026";
    case kIndexLaden:   return L"Loading the index from the cache\u2026";
    case kIndexBauen:   return L"Building the index (only the first time): " + std::to_wstring(f.arbeit.Fertig()) +
                               L" of " + std::to_wstring(f.arbeit.Gesamt()) + L" bundles";
    case kFigurenListe: return L"Collecting characters\u2026";
    case kFigurBauen:   return L"Reading the character from the game files\u2026";
    case kTexturen:     return L"Decoding textures (first time per texture)\u2026";
    default:            return L"Working\u2026";
    }
}

// ------------------------------------------------------------
//  Die beiden Aufgaben
// ------------------------------------------------------------
// 1.43.0: SITZUNGSVORRAT. Das Fenster las Spielkataloge und Index (97 MB)
// bei JEDEM Oeffnen neu - gemessen 8,4 bis 11,1 s, jedes Mal, auch wenn
// DH nur die naechste Figur importieren will. Jetzt bleiben Spiel, Index und
// Figurenliste nach dem Schliessen im Speicher (wie die Animationsdaten
// seit 0.38.0) und werden beim naechsten Oeffnen mit demselben Spielordner
// uebernommen. Nur der Hauptthread fasst den Vorrat an (Oeffnen/Schliessen);
// er lebt bis Max endet und wird absichtlich nicht freigegeben.
struct Vorrat {
    std::string ordner, ablage;
    std::unique_ptr<fbgame::Spiel> spiel;
    std::unique_ptr<fbindex::Index> index;
    std::vector<fbauswahl::Figur> figuren;
};
Vorrat* g_vorrat = nullptr;

void StarteLaden(Fenster& f) {
    {
        const std::string o = Utf8(f.spielordner), a = Utf8(f.b.ablage + L"\\index.fbidx");
        if (!f.probelauf && g_vorrat != nullptr && g_vorrat->spiel != nullptr && g_vorrat->index != nullptr &&
            g_vorrat->ordner == o && g_vorrat->ablage == a) {
            f.spiel = std::move(g_vorrat->spiel);
            f.index = std::move(g_vorrat->index);
            f.figuren = std::move(g_vorrat->figuren);
            f.geladenOrdner = o;
            f.geladenAblage = a;
            f.sichtbar.clear();
            f.abbruchVerlangt = false;
            f.status = std::to_wstring(f.index->bundles.size()) + L" bundles  \u00B7  " +
                       std::to_wstring(f.figuren.size()) + L" characters  \u00B7  index already in memory";
            f.statusFehler = false;
            Log(f, "FENSTER Spielordner " + o);
            Log(f, "INDEX aus dem Speicher dieser Max-Sitzung (nicht neu gelesen)");
            Log(f, "FIGUREN " + std::to_string(f.figuren.size()) + " davon HELDEN " + std::to_string(fbauswahl::ZaehleHelden(f.figuren)));
            FuelleListe(f);
            Bedienbarkeit(f);
            ZeigeStatus(f);
            return;
        }
    }
    f.spiel.reset();
    f.index.reset();
    f.figuren.clear();
    f.sichtbar.clear();
    f.abbruchVerlangt = false;
    const std::string ordner = Utf8(f.spielordner);
    const std::string ablage = Utf8(f.b.ablage + L"\\index.fbidx");
    const std::string demo = Utf8(f.b.demoFiguren);
    f.geladenOrdner = ordner;
    f.geladenAblage = ablage;
    f.status = L"Starting\u2026";
    f.statusFehler = false;
    Log(f, f.probelauf ? "FENSTER Probelauf mit " + demo : "FENSTER Spielordner " + ordner);

    f.arbeit.Starte(Aufgabe::Laden, [ordner, ablage, demo](Ergebnis& e, fbindex::Fortschritt& fs,
                                                           std::atomic<int>& phase) {
        if (!demo.empty()) {
            phase = kFigurenListe;
            if (!fbauswahl::LiesFigurenliste(demo, e.figuren, e.fehler)) return;
            e.ok = true;
            return;
        }
        phase = kKataloge;
        auto spiel = std::make_unique<fbgame::Spiel>();
        if (!spiel->Oeffne(ordner, e.fehler)) return;
        if (fs.abbrechen.load()) { e.fehler = "abgebrochen"; return; }

        phase = kIndexLaden;
        auto index = std::make_unique<fbindex::Index>();
        std::string grund;
        if (fbindex::LadeIndex(*index, ablage, spiel->Kopfnummer(), grund)) {
            e.ausAblage = true;
        } else {
            e.ablageGrund = grund;
            *index = fbindex::Index();
            phase = kIndexBauen;
            if (!fbindex::BaueIndex(*spiel, *index, e.fehler, &fs)) return;
            e.fehler.clear();
            std::string g2;
            if (!fbindex::SpeichereIndex(*index, ablage, spiel->Kopfnummer(), g2)) {
                e.hinweis = "Index nicht abgelegt: " + g2;
            }
        }
        phase = kFigurenListe;
        e.figuren = fbauswahl::Figuren(*index);
        // 0.97.0: Fahrzeuge anhaengen - mit ihrem eigenen Skelett und ihren
        // MeshSets, damit der Import sie wie eine Figur bauen kann.
        for (const fbfahrzeug::Fahrzeug& v : fbfahrzeug::Fahrzeuge(*index)) {
            if (v.meshsetNamen.empty()) continue;
            fbauswahl::Figur fi;
            fi.name = "vehicle: " + v.art + "/" + v.name;
            fi.meshsets = v.meshsets;
            // 1.03.0: Texturen des Fahrzeugordners zaehlen - im Fenster stand
            // bei jedem Fahrzeug "0 textures".
            {
                // 1.19.0: Auch Texturen ausserhalb des Fahrzeugordners zaehlen -
                // beim AT-TE stand "0 textures", obwohl er welche hat; seine
                // Teile liegen in mehreren Ordnern. Gezaehlt wird nach dem
                // dichten Fahrzeugnamen (at_te -> "atte").
                const std::string o = v.ordner + "/";
                std::string dicht;
                for (char c : v.name) if (c != '-' && c != '_' && c != ' ') dicht += c;
                for (const auto& t : index->res) {
                    if (t.second.resType != 0x6BDE20BAu) continue;
                    std::string tn = t.first;
                    for (char& c : tn) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                    bool drin = tn.compare(0, o.size(), o) == 0;
                    if (!drin && dicht.size() >= 4) {
                        // Nur ganze Wortteile vergleichen: "atte" steckt sonst
                        // auch in "sc-atte-rgun" (t_scattergun_cs).
                        std::vector<std::string> teile;
                        std::string cur;
                        for (char c : tn) {
                            if (c == '/' || c == '_' || c == '-' || c == '.') { if (!cur.empty()) teile.push_back(cur); cur.clear(); }
                            else cur += c;
                        }
                        if (!cur.empty()) teile.push_back(cur);
                        for (size_t a2 = 0; a2 < teile.size() && !drin; ++a2) {
                            std::string zus;
                            for (size_t b2 = a2; b2 < teile.size() && b2 < a2 + 3 && !drin; ++b2) {
                                zus += teile[b2];
                                if (zus == dicht) drin = true;
                            }
                        }
                    }
                    if (drin) ++fi.texturen;
                }
            }
            fi.art = fbauswahl::Art::Fahrzeug;
            fi.skelett = v.skelett;
            fi.meshsetNamen = v.meshsetNamen;
            e.figuren.push_back(std::move(fi));
        }
        e.spiel = std::move(spiel);
        e.index = std::move(index);
        e.ok = true;
    });
    SetTimer(f.hDlg, kTimer, kTakt, nullptr);
    FuelleListe(f);
    ZeigeStatus(f);
}

void StarteFigur(Fenster& f) {
    const int sel = Auswahl(f);
    if (sel < 0 || f.spiel == nullptr || f.index == nullptr || f.arbeit.Laeuft()) return;
    const fbauswahl::Figur& fi = f.figuren[f.sichtbar[static_cast<size_t>(sel)]];
    std::wstring blatt = Blatt(fi.name);
    for (wchar_t& c : blatt) {
        if (c < 32 || std::wcschr(L"<>:\"/\\|?*", c) != nullptr) c = L'_';
    }
    const std::wstring ziel = f.b.ablage + L"\\figuren\\" + blatt + L".fbmodel";
    const std::string zielU = Utf8(ziel);
    const std::string texOrdner = Utf8(f.b.ablage + L"\\texturen");
    const std::string name = fi.name;
    fbgame::Spiel* spiel = f.spiel.get();
    const fbindex::Index* index = f.index.get();
    f.abbruchVerlangt = false;
    f.status = L"Reading " + blatt + L"\u2026";
    f.statusFehler = false;
    Log(f, "FENSTER Figur " + name);

    // Waehrend dieser Aufgabe fasst der Hauptthread spiel und index nicht an:
    // alles, was sie braucht, ist gesperrt (Bedienbarkeit).
    const std::string ablageU = Utf8(f.b.ablage);
    // 0.97.0: Fahrzeuge kommen nicht aus einem Bundle, sondern aus ihren
    // MeshSets (siehe fbfahrzeug) - die Auswahl traegt sie bei sich.
    const int sel0 = Auswahl(f);
    const fbauswahl::Figur gewaehlt = sel0 >= 0 ? f.figuren[f.sichtbar[static_cast<size_t>(sel0)]] : fbauswahl::Figur();
    const bool istFahrzeug = gewaehlt.art == fbauswahl::Art::Fahrzeug;
    f.arbeit.Starte(Aufgabe::Figur, [spiel, index, name, zielU, ziel, texOrdner, ablageU, gewaehlt, istFahrzeug](Ergebnis& e, fbindex::Fortschritt&,
                                                                                std::atomic<int>& phase) {
        phase = kFigurBauen;
        fbfigur::Modell m;
        std::string skHerkunft;
        std::string skelett;
        if (istFahrzeug) {
            skelett = gewaehlt.skelett;
            const std::string kurz = gewaehlt.name.substr(gewaehlt.name.find_last_of('/') + 1);
            const std::string echt = fbfahrzeug::SucheSkelett(*spiel, *index, kurz);
            if (!echt.empty()) skelett = echt;
            skHerkunft = "Fahrzeug";
            // 1.43.0: Piloten sind Figuren (Walrus), keine Fahrzeuge mit eigenem Skelett.
            const bool piloten = gewaehlt.name.rfind("vehicle: pilots/", 0) == 0;
            if (piloten && skelett.empty()) { skelett = kSkelett; skHerkunft = "Pilot (gemeinsames Skelett)"; }
            bool nurKern = false;
            for (const std::string& x : gewaehlt.meshsetNamen) if (fbfahrzeug::IstKernTeil(x, kurz)) { nurKern = true; break; }
            const bool mitStatisch = !fbfahrzeug::HatHauptmesh(gewaehlt.meshsetNamen, kurz);
            // 1.38.0: Gibt es MEHRERE vollstaendige Fassungen, nur eine nehmen.
            // Beim AT-AT kamen "at-at_static_donotuse_mesh" UND
            // "vehicle_ground_at-at_sp_mesh" mit - zwei komplette Laeufer
            // uebereinander (12 Meshes, 231 783 Vertices). Die Spielfassung
            // (ohne "donotuse") hat Vorrang.
            bool spielfassungDa = false;
            if (mitStatisch)
                for (const std::string& x : gewaehlt.meshsetNamen) {
                    if (!fbfahrzeug::IstEigenesTeil(x, kurz)) continue;
                    if (nurKern && !fbfahrzeug::IstKernTeil(x, kurz)) continue;
                    if (!fbfahrzeug::IstFahrzeugteil(x, true, piloten)) continue;
                    if (KleinA(x).find("donotuse") == std::string::npos) { spielfassungDa = true; break; }
                }
            size_t gebaut = 0;
            // 1.34.0: Alle Anwaerter protokollieren. Beim AT-AT kamen nur
            // statische Fassungen an ("keine Gewichte in der Datei,
            // uebersprungen") - so ist zu sehen, welche Teile es ueberhaupt
            // gibt und welche der Filter verwirft.
            for (const std::string& kand : gewaehlt.meshsetNamen) {
                std::string warum;
                if (!fbfahrzeug::IstEigenesTeil(kand, kurz)) warum = "nicht vom Fahrzeug";
                else if (nurKern && !fbfahrzeug::IstKernTeil(kand, kurz)) warum = "andere Fassung";
                else if (!fbfahrzeug::IstFahrzeugteil(kand, mitStatisch, piloten)) warum = "Wrack/Besatzung/statisch";
                m.hinweise.push_back("FAHRZEUGTEIL " + kand + (warum.empty() ? "  -> gebaut" : "  -> " + warum));
            }
            for (const std::string& meshset : gewaehlt.meshsetNamen) {
                if (!fbfahrzeug::IstEigenesTeil(meshset, kurz)) continue;
                if (nurKern && !fbfahrzeug::IstKernTeil(meshset, kurz)) continue;
                if (!fbfahrzeug::IstFahrzeugteil(meshset, mitStatisch, piloten)) continue;
                if (spielfassungDa && KleinA(meshset).find("donotuse") != std::string::npos) continue;   // 1.38.0
                fbfigur::Modell teil;
                std::string f2;
                if (!fbfigur::BaueMeshSet(*spiel, *index, meshset, skelett, 0, false, teil, f2)) { m.hinweise.push_back(meshset + ": " + f2); continue; }
                ++gebaut;
                // 1.41.0: Die Hinweise JEDES Teils mitnehmen (TEILE-Zeilen der
                // Composite-Zuordnung). Frueher ueberschrieb "m = teil" die schon
                // gesammelten FAHRZEUGTEIL-Zeilen, und spaetere Teile gingen verloren.
                for (const std::string& h : teil.hinweise) m.hinweise.push_back(h);
                if (m.bones.empty() && !teil.bones.empty()) { const std::vector<fbfigur::Mesh> me = m.meshes; const std::vector<fbfigur::Material> ma = m.materialien; const std::vector<std::string> hw = m.hinweise; m = teil; m.meshes = me; m.materialien = ma; m.hinweise = hw; for (fbfigur::Mesh& x : teil.meshes) m.meshes.push_back(x); for (fbfigur::Material& x : teil.materialien) m.materialien.push_back(x); continue; }
                const size_t vorher = m.materialien.size();
                for (fbfigur::Mesh& me2 : teil.meshes) { if (me2.materialId >= 0) me2.materialId += static_cast<int32_t>(vorher); m.meshes.push_back(std::move(me2)); }
                for (fbfigur::Material& ma2 : teil.materialien) m.materialien.push_back(std::move(ma2));
                if (!teil.quelle.empty() && teil.quelle != m.quelle &&                     // 0.99.0
                    std::find(m.weitereBundles.begin(), m.weitereBundles.end(), teil.quelle) == m.weitereBundles.end())
                    m.weitereBundles.push_back(teil.quelle);
            }
            if (gebaut == 0) { e.fehler = "kein Teil des Fahrzeugs konnte gebaut werden"; return; }
            // 0.98.0: m.quelle bleibt das Bundle des ERSTEN gebauten Teils -
            // die Materialzuordnung sucht darueber die MeshVariationDatabase
            // (frueher stand hier "vehicle: ground/at-st", und sie scheiterte
            // mit "Bundle nicht gefunden").
            m.hinweise.push_back("FAHRZEUG " + gewaehlt.name + " aus " + std::to_string(gebaut) + " MeshSets, Bundle " + m.quelle);
            // 1.10.0: Das Fahrzeug kennzeichnen. Ohne diesen Vermerk haelt das
            // Animationsfenster den AT-TE fuer die Figur "left" (aus den
            // Knotennamen geraten) und findet nichts.
            m.hinweise.push_back("FAHRZEUGNAME " + gewaehlt.name.substr(gewaehlt.name.find_last_of('/') + 1));
            m.figurVermerk = gewaehlt.name;                              // 1.19.0: "vehicle: ground/at_te"
        } else {
            skelett = fbauswahl::SkelettFuer(*index, name, skHerkunft);
            if (!fbfigur::BaueFigur(*spiel, *index, name, skelett, 0, false, m, e.fehler)) return;
        }
        m.hinweise.push_back("SKELETT " + skelett + " (" + skHerkunft + ")");
        // Die Waffen der Figur (0.42.0): starr an Wep_Root, eigener Layer im Plugin.
        if (!istFahrzeug) {
            std::vector<std::string> waffen;
            fbfigur::FuegeWaffenHinzu(*spiel, *index, m, waffen);
            for (const std::string& z : waffen) m.hinweise.push_back(z);
        }
        size_t v = 0, t = 0;
        for (const fbfigur::Mesh& me : m.meshes) { v += me.vertexCount; t += me.dreiecke; }
        e.bones = m.bones.size();
        e.meshes = m.meshes.size();
        e.vertices = v;
        e.figurZeile = "FIGUR " + m.quelle + " meshes=" + std::to_string(m.meshes.size()) +
                       " vertices=" + std::to_string(v) + " dreiecke=" + std::to_string(t) +
                       " bones=" + std::to_string(m.bones.size()) +
                       " materialien=" + std::to_string(m.materialien.size()) +
                       " schatten=" + std::to_string(m.schatten) + " skelett=" + m.skelett;
        e.hinweise = m.hinweise;
        e.rigKandidaten = fbauswahl::RigKandidaten(*index, name);
        // 1.29.0: Die .fbmodel wird VOR der Materialzuordnung geschrieben - der
        // Fahrzeugvermerk muss also hier schon drinstehen, die Zuordnung
        // braucht danach wieder das Bundle. Deshalb kurz tauschen.
        // Vorher stand in der Datei das Bundle: Max erkannte kein Fahrzeug,
        // nahm uv0 statt uv1, die Ruhelage vom Menschen und das
        // Animationsfenster zeigte "(?)".
        const std::string bundleQuelle = m.quelle;
        if (!m.figurVermerk.empty()) m.quelle = m.figurVermerk;
        const bool geschrieben = fbfigur::SchreibeFbmodel(zielU, m, e.fehler);
        m.quelle = bundleQuelle;
        if (!geschrieben) return;
        // Basispose gegen die verzerrten Koepfe (0.43.0): als Beipackzettel
        // neben die .fbmodel; der Import legt sie nach dem Skin auf die Bones.
        if (!istFahrzeug) {
            // 0.44.0: die Gesichtspose aus dem BindPoseAsset der FacePoseLibrary
            // (VisualUnlock -> AntRef -> Bank). Das Clipverzeichnis kommt aus der
            // Ablage (clips.fbclips), sonst wird es einmal gebaut und abgelegt.
            fbfigur::Basispose bp;
            std::string pf, grund;
            fbanim::Quelle q;
            const std::string clipDatei = ablageU + "\\clips.fbclips";
            if (!q.Lade(*spiel, clipDatei, spiel->Kopfnummer(), grund)) {
                if (q.Baue(*spiel, *index, pf)) q.Speichere(clipDatei, spiel->Kopfnummer(), pf);
            }
            bool ok = fbgesicht::LiesGesichtspose(*spiel, *index, q, m, bp);
            if (!ok) {
                fbfigur::Basispose bp2;                          // Rueckfall: BasePoseTransforms
                ok = fbfigur::LiesBasispose(*spiel, *index, m, bp2);
                for (const std::string& z : bp.protokoll) e.hinweise.push_back(z);
                bp = bp2;
            }
            if (ok) fbfigur::SchreibeBasispose(zielU + ".pose.txt", bp, pf);
            else std::remove((zielU + ".pose.txt").c_str());
            for (const std::string& z : bp.protokoll) e.hinweise.push_back(z);
        }
        // Stufe 4: Texturen. Zuordnung ueber die MeshVariationDatabase, die
        // Bilder als PNG in der Ablage (beim zweiten Mal nur noch gelesen),
        // der Beipackzettel neben die .fbmodel. Die .fbmodel selbst bleibt
        // byteweise gleich mit fbtools.
        phase = kTexturen;
        std::vector<fbbeipack::MeshMaterial> mm;
        std::string mf;
        if (fbmaterial::Loese(*spiel, *index, m, mm, e.materialProtokoll, mf)) {
            fbmaterial::SchreibeTexturen(*spiel, *index, mm, texOrdner, e.materialProtokoll, mf);
            if (!fbbeipack::Schreibe(zielU + ".material.txt", mm, mf)) e.materialProtokoll.push_back("BEIPACK nicht geschrieben: " + mf);
        } else {
            e.materialProtokoll.push_back("MATERIAL Zuordnung gescheitert: " + mf);
            // 1.22.0: Den alten Beipackzettel wegraeumen. Genau daran lag es,
            // dass der AT-TE Soldatentexturen trug: die Zuordnung scheiterte,
            // die .material.txt eines frueheren Imports blieb liegen, und Max
            // las daraus M_Gloves_06 und t_l_assault_preq_01_body_cs.
            std::remove((zielU + ".material.txt").c_str());
        }
        e.figurPfad = ziel;
        e.ok = true;
    });
    SetTimer(f.hDlg, kTimer, kTakt, nullptr);
    Bedienbarkeit(f);
    ZeigeStatus(f);
}

void Fertig(Fenster& f) {
    const Aufgabe was = f.arbeit.Was();
    Ergebnis e = f.arbeit.Hole();
    const bool abgebrochen = (e.fehler == "abgebrochen");
    if (was == Aufgabe::Laden) {
        if (!e.ok) {
            f.status = abgebrochen ? std::wstring(L"Cancelled.") : L"Could not read the game: " + Breit(e.fehler);
            f.statusFehler = !abgebrochen;
            Log(f, "FENSTER Laden " + std::string(abgebrochen ? "abgebrochen" : "FEHLER: " + e.fehler));
        } else {
            f.spiel = std::move(e.spiel);
            f.index = std::move(e.index);
            f.figuren = std::move(e.figuren);
            const size_t helden = fbauswahl::ZaehleHelden(f.figuren);
            if (f.probelauf || f.index == nullptr) {
                f.status = std::to_wstring(f.figuren.size()) + L" characters from a list  \u00B7  probe run, no game";
            } else {
                f.status = std::to_wstring(f.index->bundles.size()) + L" bundles  \u00B7  " +
                           std::to_wstring(f.figuren.size()) + L" characters  \u00B7  index " +
                           (e.ausAblage ? L"from cache" : L"built and cached") + L" in " +
                           ZehntelOrt(e.sekunden) + L" s";
                Log(f, "INDEX " + std::string(e.ausAblage ? "aus der Ablage" : "neu gebaut") +
                       (e.ablageGrund.empty() ? std::string() : " (" + e.ablageGrund + ")"));
                Log(f, "INDEX bundles=" + std::to_string(f.index->bundles.size()) +
                       " ebx=" + std::to_string(f.index->ebx.size()) + " res=" + std::to_string(f.index->res.size()) +
                       " chunks=" + std::to_string(f.index->chunks.size()) + " (" + ZehntelPunkt(e.sekunden) + " s)");
            }
            f.statusFehler = false;
            Log(f, "FIGUREN " + std::to_string(f.figuren.size()) + " davon HELDEN " + std::to_string(helden));
            if (!e.hinweis.empty()) Log(f, "FENSTER Hinweis: " + e.hinweis);
        }
        FuelleListe(f);
        if (f.probelauf && !f.b.probeSuche.empty() && !f.sichtbar.empty()) {
            SendDlgItemMessageW(f.hDlg, IDC_LISTE, LB_SETCURSEL, 0, 0);
            ZeigeDetail(f);
            Bedienbarkeit(f);
        }
        SetFocus(GetDlgItem(f.hDlg, IDC_SUCHE));
    } else if (was == Aufgabe::Figur) {
        if (!e.ok) {
            f.status = L"Could not read the character: " + Breit(e.fehler);
            f.statusFehler = true;
            Log(f, "FENSTER Figur FEHLER: " + e.fehler);
        } else {
            Log(f, e.figurZeile);
            for (const std::string& h : e.hinweise) Log(f, "FIGUR hinweis " + h);
            if (e.rigKandidaten.empty()) Log(f, "FIGUR Skelett-Kandidaten im Bundle: keine");
            for (const std::string& k : e.rigKandidaten) Log(f, "FIGUR Skelett-Kandidat " + k);
            for (const std::string& z : e.materialProtokoll) Log(f, z);
            Log(f, "FIGUR geschrieben " + Utf8(e.figurPfad) + " (" + ZehntelPunkt(e.sekunden) + " s)");
            f.status = L"Building the scene\u2026";
            f.statusFehler = false;
            ZeigeStatus(f);
            UpdateWindow(GetDlgItem(f.hDlg, IDC_FORTSCHRITT));
            std::wstring bericht;
            const bool ok = f.b.importiere != nullptr && f.b.importiere(e.figurPfad, bericht);
            const size_t p = e.figurPfad.find_last_of(L"\\/");
            std::wstring blatt = (p == std::wstring::npos) ? e.figurPfad : e.figurPfad.substr(p + 1);
            if (blatt.size() > 8 && blatt.compare(blatt.size() - 8, 8, L".fbmodel") == 0) blatt.resize(blatt.size() - 8);
            if (ok) {
                f.importiert = true;
                f.status = L"Imported " + blatt + L"  \u00B7  " + std::to_wstring(e.bones) + L" bones  \u00B7  " +
                           std::to_wstring(e.meshes) + L" meshes  \u00B7  " + std::to_wstring(e.vertices) + L" vertices";
            } else {
                const size_t z = bericht.find(L'\n');
                f.status = L"Import failed: " + (bericht.empty() ? std::wstring(L"see Import swbf2.log") : bericht.substr(0, z));
                f.statusFehler = true;
            }
        }
        FuelleListe(f);
        SetFocus(GetDlgItem(f.hDlg, IDC_LISTE));
    }
    f.abbruchVerlangt = false;
    Bedienbarkeit(f);
    ZeigeStatus(f);
}

void Durchsuchen(Fenster& f) {
    std::wstring o = f.spielordner.empty() ? Umgebung(L"ProgramFiles") : f.spielordner;
    if (!WaehleOrdner(f.hDlg, o)) return;
    o = Bereinige(o);
    if (!IstSpielordner(o)) {
        const std::wstring m = L"This folder does not contain Data\\layout.toc:\n\n" + o +
                               L"\n\nPlease choose the STAR WARS Battlefront II installation folder.";
        MessageBoxW(f.hDlg, m.c_str(), L"SWBF2 Import", MB_ICONWARNING | MB_OK);
        return;
    }
    f.spielordner = o;
    SetDlgItemTextW(f.hDlg, IDC_ORDNER, o.c_str());
    SchreibeIni(f, L"Spielordner", o);
    StarteLaden(f);
}

void Schliessen(Fenster& f) {
    if (f.arbeit.Laeuft()) {
        f.abbruchVerlangt = true;
        f.status = L"Stopping\u2026";
        ZeigeStatus(f);
        UpdateWindow(GetDlgItem(f.hDlg, IDC_FORTSCHRITT));
    }
    f.arbeit.Stopp();
    KillTimer(f.hDlg, kTimer);
    SchreibeIni(f, L"Kategorie", std::to_wstring(f.kategorie));
    SchreibeIni(f, L"ErstePerson", f.erstePerson ? L"1" : L"0");
    EndDialog(f.hDlg, f.importiert ? 1 : 0);
}

void Einrichten(Fenster& f) {
    f.pal.Baue(f.b.farbe);
    f.fontNormal = reinterpret_cast<HFONT>(SendMessageW(f.hDlg, WM_GETFONT, 0, 0));
    LOGFONTW lf{};
    if (f.fontNormal != nullptr && GetObjectW(f.fontNormal, sizeof(lf), &lf) == sizeof(lf)) {
        lf.lfWeight = FW_SEMIBOLD;
        f.fontFett = CreateFontIndirectW(&lf);
    }
    f.zeilenHoehe = ZeilenHoehe(f.hDlg);
    SendDlgItemMessageW(f.hDlg, IDC_LISTE, LB_SETITEMHEIGHT, 0, EintragsHoehe(f.hDlg));

    // Dunkle Titelleiste, wenn Max dunkel ist. 20 ist DWMWA_USE_IMMERSIVE_DARK_MODE,
    // aeltere Windows-10-Staende kennen nur 19. Scheitert beides, bleibt sie hell.
    if (f.pal.dunkel) {
        const BOOL an = TRUE;
        if (FAILED(DwmSetWindowAttribute(f.hDlg, 20, &an, sizeof(an)))) {
            DwmSetWindowAttribute(f.hDlg, 19, &an, sizeof(an));
        }
    }
    SetWindowTextW(f.hDlg, (L"SWBF2 Import " + f.b.version).c_str());
    {
        const int innen = std::max(3, f.zeilenHoehe / 4);
        SendDlgItemMessageW(f.hDlg, IDC_SUCHE, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(innen, innen));
    }
    SendDlgItemMessageW(f.hDlg, IDC_SUCHE, EM_SETCUEBANNER, TRUE,
                        reinterpret_cast<LPARAM>(L"Filter by name, e.g.  anakin  or  skin"));
    std::wstring fuss = L"SWBF2 Import " + f.b.version + L"  \u00B7  skeleton: " + kSkelettKurz;
    if (f.probelauf) fuss += L"  \u00B7  probe run";
    SetDlgItemTextW(f.hDlg, IDC_FUSS, fuss.c_str());
    MerkeAnker(f);

    f.kategorie = std::clamp(_wtoi(LiesIni(f, L"Kategorie", L"0").c_str()), 0, kKategorien - 1);
    f.erstePerson = (LiesIni(f, L"ErstePerson", L"0") == L"1");
    f.spielordner = Bereinige(LiesIni(f, L"Spielordner"));
    if (!f.b.startOrdner.empty()) {
        const std::wstring vorgabe = Bereinige(f.b.startOrdner);
        if (IstSpielordner(vorgabe)) {
            f.spielordner = vorgabe;
            SchreibeIni(f, L"Spielordner", vorgabe);
        }
    }
    if (!IstSpielordner(f.spielordner)) f.spielordner = SucheSpiel();
    SetDlgItemTextW(f.hDlg, IDC_ORDNER,
                    f.probelauf ? L"(probe run - no game folder)"
                                : (f.spielordner.empty() ? L"(not found - choose it with Browse)" : f.spielordner.c_str()));

    Log(f, "FENSTER Kontrast (WCAG 2.2, Soll 4.5:1): Text " + Verhaeltnis(Kontrast(f.pal.text, f.pal.grund)) +
           ", gedaempft " + Verhaeltnis(Kontrast(f.pal.dim, f.pal.grund)) +
           ", Liste " + Verhaeltnis(Kontrast(f.pal.feldText, f.pal.feld)) +
           ", Liste gedaempft " + Verhaeltnis(Kontrast(f.pal.feldDim, f.pal.feld)) +
           ", Auswahl " + Verhaeltnis(Kontrast(f.pal.auswahlText, f.pal.auswahl)) +
           (f.pal.dunkel ? ", Theme dunkel" : ", Theme hell"));

    if (!f.b.probeSuche.empty()) SetDlgItemTextW(f.hDlg, IDC_SUCHE, f.b.probeSuche.c_str());
    if (f.probelauf || IstSpielordner(f.spielordner)) {
        StarteLaden(f);
    } else {
        f.status = L"Choose the game folder (the one that contains Data\\layout.toc).";
        FuelleListe(f);
    }
    SetFocus(GetDlgItem(f.hDlg, (f.probelauf || !f.spielordner.empty()) ? IDC_SUCHE : IDC_DURCHSUCHEN));
}

// ------------------------------------------------------------
//  Nachrichten
// ------------------------------------------------------------
INT_PTR Verarbeite(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INITDIALOG) {
        Fenster* neu = reinterpret_cast<Fenster*>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(neu));
        neu->hDlg = hDlg;
        Einrichten(*neu);
        return FALSE;      // Fokus ist selbst gesetzt
    }
    if (msg == WM_MEASUREITEM) {
        // Kommt schon VOR WM_INITDIALOG - deshalb ohne Fenster-Zustand.
        MEASUREITEMSTRUCT* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mi != nullptr && mi->CtlID == IDC_LISTE) {
            mi->itemHeight = static_cast<UINT>(EintragsHoehe(hDlg));
            return TRUE;
        }
        return FALSE;
    }
    Fenster* f = Zustand(hDlg);
    if (f == nullptr) return FALSE;

    switch (msg) {
    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(f->pal.pinselGrund);
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        const bool leise = (id == IDC_ORDNER_LABEL || id == IDC_SUCHE_LABEL || id == IDC_DETAIL ||
                            id == IDC_FUSS || id == IDC_ANZAHL);
        SetBkColor(dc, f->pal.grund);
        SetTextColor(dc, leise ? f->pal.dim : f->pal.text);
        return reinterpret_cast<INT_PTR>(f->pal.pinselGrund);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, f->pal.feld);
        SetTextColor(dc, f->pal.feldText);
        return reinterpret_cast<INT_PTR>(f->pal.pinselFeld);
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (d == nullptr) return FALSE;
        if (d->CtlType == ODT_BUTTON) ZeichneKnopf(*f, *d);
        else if (d->CtlType == ODT_LISTBOX) ZeichneEintrag(*f, *d);
        else if (d->CtlType == ODT_STATIC) ZeichneFortschritt(*f, *d);
        return TRUE;
    }
    case WM_TIMER:
        if (wParam == kTimer) {
            if (f->arbeit.Laeuft()) {
                f->status = PhasenText(*f);
                ZeigeStatus(*f);
            } else {
                KillTimer(hDlg, kTimer);
                Fertig(*f);
            }
        }
        return TRUE;
    case WM_SIZE:
        Ordne(*f, LOWORD(lParam), HIWORD(lParam));
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hDlg, &ps);
        ZeichneRahmen(*f, dc);
        EndPaint(hDlg, &ps);
        return TRUE;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lParam);
        if (mm != nullptr && f->startFenster.cx > 0) {
            mm->ptMinTrackSize.x = f->startFenster.cx;
            mm->ptMinTrackSize.y = f->startFenster.cy;
        }
        return TRUE;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id >= IDC_TAB0 && id < IDC_TAB0 + kKategorien) {
            if (code == BN_CLICKED) {
                f->kategorie = id - IDC_TAB0;
                FuelleListe(*f);
            }
            return TRUE;
        }
        switch (id) {
        case IDC_DURCHSUCHEN:
            if (code == BN_CLICKED) Durchsuchen(*f);
            return TRUE;
        case IDC_ERSTEPERSON:
            if (code == BN_CLICKED) {
                f->erstePerson = !f->erstePerson;
                FuelleListe(*f);
            }
            return TRUE;
        case IDC_SUCHE:
            if (code == EN_CHANGE) FuelleListe(*f);
            return TRUE;
        case IDC_LISTE:
            if (code == LBN_SELCHANGE) { ZeigeDetail(*f); Bedienbarkeit(*f); }
            else if (code == LBN_DBLCLK && IsWindowEnabled(GetDlgItem(hDlg, IDOK))) StarteFigur(*f);
            return TRUE;
        case IDOK:
            // Eingabetaste im Suchfeld: erst den ersten Treffer waehlen, dann importieren.
            if (Auswahl(*f) < 0 && !f->sichtbar.empty() && !f->arbeit.Laeuft()) {
                SendDlgItemMessageW(hDlg, IDC_LISTE, LB_SETCURSEL, 0, 0);
                ZeigeDetail(*f);
                Bedienbarkeit(*f);
                SetFocus(GetDlgItem(hDlg, IDC_LISTE));
            } else if (IsWindowEnabled(GetDlgItem(hDlg, IDOK))) {
                StarteFigur(*f);
            }
            return TRUE;
        case IDCANCEL:
            if (f->arbeit.Laeuft()) {
                f->abbruchVerlangt = true;
                f->arbeit.Abbrechen();
                f->status = PhasenText(*f);
                ZeigeStatus(*f);
            } else {
                Schliessen(*f);
            }
            return TRUE;
        default:
            break;
        }
        break;
    }
    case WM_CLOSE:
        Schliessen(*f);
        return TRUE;
    case WM_DESTROY:
        KillTimer(hDlg, kTimer);
        f->pal.Frei();
        if (f->fontFett != nullptr) { DeleteObject(f->fontFett); f->fontFett = nullptr; }
        return FALSE;
    default:
        break;
    }
    return FALSE;
}

// Keine Ausnahme darf in Windows' Nachrichtenschleife gelangen - und damit
// in Max (SDK-Regel: keine ungefangenen Ausnahmen aus einem Plugin).
INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    try {
        return Verarbeite(hDlg, msg, wParam, lParam);
    } catch (const std::exception& x) {
        if (Fenster* f = Zustand(hDlg)) {
            Log(*f, std::string("FENSTER Ausnahme: ") + x.what());
            f->status = L"Internal error: " + Breit(x.what());
            f->statusFehler = true;
            ZeigeStatus(*f);
        }
    } catch (...) {
        if (Fenster* f = Zustand(hDlg)) {
            Log(*f, "FENSTER unbekannte Ausnahme");
            f->status = L"Internal error.";
            f->statusFehler = true;
            ZeigeStatus(*f);
        }
    }
    return FALSE;
}

} // namespace

int ZeigeFigurenFenster(const FensterBruecke& b) {
    // Max hat COM im Hauptthread schon eingerichtet (dann kommt S_FALSE).
    // Beide Erfolgsfaelle muessen mit CoUninitialize ausgeglichen werden.
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::unique_ptr<Fenster> f;
    try {
        f = std::make_unique<Fenster>();
    } catch (...) {
        if (co == S_OK || co == S_FALSE) CoUninitialize();
        return -1;
    }
    f->b = b;
    f->probelauf = !b.demoFiguren.empty();
    CreateDirectoryW(b.ablage.c_str(), nullptr);
    CreateDirectoryW((b.ablage + L"\\figuren").c_str(), nullptr);
    CreateDirectoryW((b.ablage + L"\\texturen").c_str(), nullptr);
    f->ini = b.ablage + L"\\einstellungen.ini";

    const INT_PTR r = DialogBoxParamW(static_cast<HINSTANCE>(b.hInstance), MAKEINTRESOURCEW(IDD_FIGUREN),
                                      static_cast<HWND>(b.eltern), &DlgProc, reinterpret_cast<LPARAM>(f.get()));
    if (r == -1 && b.protokoll != nullptr) {
        b.protokoll(("FENSTER DialogBoxParam fehlgeschlagen, GetLastError=" + std::to_string(GetLastError())).c_str());
    }
    f->arbeit.Stopp();
    // 1.43.0: Spiel und Index fuer das naechste Oeffnen aufheben (siehe Vorrat)
    if (!f->probelauf && f->spiel != nullptr && f->index != nullptr) {
        try {
            if (g_vorrat == nullptr) g_vorrat = new Vorrat();
            g_vorrat->ordner = f->geladenOrdner;
            g_vorrat->ablage = f->geladenAblage;
            g_vorrat->spiel = std::move(f->spiel);
            g_vorrat->index = std::move(f->index);
            g_vorrat->figuren = std::move(f->figuren);
        } catch (...) {
            // kein Speicher: dann liest das naechste Oeffnen eben neu
        }
    }
    if (co == S_OK || co == S_FALSE) CoUninitialize();
    return r == -1 ? -1 : (r == 1 ? 1 : 0);
}

std::wstring SpielordnerAusDatei(const std::wstring& datei) {
    std::wstring d = datei;
    size_t p = d.find_last_of(L"\\/");
    if (p == std::wstring::npos) return std::wstring();
    d.resize(p);
    for (int stufe = 0; stufe < 8 && !d.empty(); ++stufe) {
        if (IstSpielordner(d)) return d;
        p = d.find_last_of(L"\\/");
        if (p == std::wstring::npos) break;
        d.resize(p);
    }
    return std::wstring();
}

void SchreibeStartprotokoll(void* hInstance, const std::wstring& text) {
    const std::wstring ordner = swbf2ablage::Ordner();                    // 1.43.0
    if (ordner.size() <= 12) return;
    CreateDirectoryW(ordner.c_str(), nullptr);
    std::vector<wchar_t> modul(32768, L'\0');
    const DWORD n = GetModuleFileNameW(static_cast<HMODULE>(hInstance), modul.data(), static_cast<DWORD>(modul.size()));
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t zeit[64] = {};
    std::swprintf(zeit, 64, L"%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring alles = std::wstring(L"Start ") + zeit + L"\r\n";
    alles += L"Modul: " + std::wstring(modul.data(), n) + L"\r\n";
    alles += text;
    const std::string u = Utf8(alles);
    std::string fehler;
    fbdatei::SchreibeAlles(Utf8(ordner + L"\\start.log"), std::vector<uint8_t>(u.begin(), u.end()), fehler);
}

} // namespace swbf2
