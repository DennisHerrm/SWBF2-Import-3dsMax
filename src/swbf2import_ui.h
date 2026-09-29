// ============================================================
//  swbf2import_ui.h - gemeinsame Gestaltung der Fenster (Win32, ohne SDK).
//
//  Dieselben Regeln wie im Figurenfenster: alle Farben aus Max' Theme
//  (GetCustSysColor ueber die Bruecke), Schrift- und Hintergrundkontrast
//  nach WCAG 2.2 mindestens 4,5:1, abgeleitete Toene statt fester Farben,
//  eigene Zeichnung fuer Knoepfe, Listen und Auswahlfelder, dunkle
//  Titelleiste, wenn Max dunkel ist. Das Figurenfenster hat diese Teile
//  noch als eigene Kopie; neue Fenster nehmen diese Datei.
// ============================================================
#pragma once

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace swbf2ui {

inline COLORREF Mische(COLORREF a, COLORREF b, double t) {
    auto kanal = [t](int x, int y) {
        const double v = x + (y - x) * t;
        return static_cast<BYTE>(v <= 0.0 ? 0.0 : (v >= 255.0 ? 255.0 : v + 0.5));
    };
    return RGB(kanal(GetRValue(a), GetRValue(b)), kanal(GetGValue(a), GetGValue(b)), kanal(GetBValue(a), GetBValue(b)));
}

inline double Linear(int v) {
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

inline double Leuchtdichte(COLORREF c) {
    return 0.2126 * Linear(GetRValue(c)) + 0.7152 * Linear(GetGValue(c)) + 0.0722 * Linear(GetBValue(c));
}

inline double Kontrast(COLORREF a, COLORREF b) {
    double x = Leuchtdichte(a), y = Leuchtdichte(b);
    if (x < y) std::swap(x, y);
    return (x + 0.05) / (y + 0.05);
}

// Ein gedaempfter Ton, der den Mindestkontrast gerade noch haelt.
inline COLORREF Gedaempft(COLORREF grund, COLORREF schrift, double mindest) {
    for (int i = 50; i <= 100; i += 2) {
        const COLORREF c = Mische(grund, schrift, i / 100.0);
        if (Kontrast(c, grund) >= mindest) return c;
    }
    return schrift;
}

struct Palette {
    COLORREF grund = 0, text = 0, dim = 0, feld = 0, feldText = 0, feldDim = 0;
    COLORREF auswahl = 0, auswahlText = 0, auswahlDim = 0, knopf = 0, fehler = 0, linie = 0;
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
        if (Kontrast(auswahlText, auswahl) < 4.5) {
            auswahlText = Kontrast(RGB(0, 0, 0), auswahl) > Kontrast(RGB(255, 255, 255), auswahl) ? RGB(0, 0, 0) : RGB(255, 255, 255);
        }
        dunkel = Leuchtdichte(grund) < 0.18;
        dim = Gedaempft(grund, text, 4.5);
        feldDim = Gedaempft(feld, feldText, 4.5);
        auswahlDim = Gedaempft(auswahl, auswahlText, 4.5);
        knopf = Mische(grund, text, dunkel ? 0.13 : 0.07);
        linie = Mische(grund, text, 0.28);
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

// Knopf wie im Figurenfenster: abgerundet, betont = Auswahlfarbe (Hauptknopf
// oder eingeschalteter Umschalter), Fokus als farbige Kante.
inline void ZeichneKnopf(const Palette& pal, HFONT font, const DRAWITEMSTRUCT& d, bool betont) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    FillRect(dc, &r, pal.pinselGrund);
    const bool gedrueckt = (d.itemState & ODS_SELECTED) != 0;
    const bool aus = (d.itemState & ODS_DISABLED) != 0;
    const bool fokus = (d.itemState & ODS_FOCUS) != 0 && (d.itemState & ODS_NOFOCUSRECT) == 0;
    COLORREF flaeche = (betont && !aus) ? pal.auswahl : pal.knopf;
    if (gedrueckt) flaeche = Mische(flaeche, pal.text, 0.18);
    const COLORREF schrift = aus ? pal.dim : ((betont && !aus) ? pal.auswahlText : pal.text);
    COLORREF linie = Mische(flaeche, pal.text, betont ? 0.25 : 0.20);
    if (fokus) linie = betont ? pal.auswahlText : pal.auswahl;
    const int rund = std::max(4, static_cast<int>(r.bottom - r.top) / 4);
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
    HGDIOBJ altF = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, schrift);
    RECT t = r;
    InflateRect(&t, -4, 0);
    DrawTextW(dc, text, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, altF);
}

// Einfache 1-Pixel-Kante um Eingabefelder und Listen (die Steuerelemente
// selbst haben keinen Rahmen - der Systemrahmen waere hell).
inline void Kante(HWND dlg, HDC dc, int id, COLORREF farbe) {
    HWND h = GetDlgItem(dlg, id);
    if (h == nullptr || !IsWindowVisible(h)) return;
    RECT r{};
    GetWindowRect(h, &r);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&r), 2);
    HPEN stift = CreatePen(PS_SOLID, 1, farbe);
    HGDIOBJ altS = SelectObject(dc, stift);
    HGDIOBJ altP = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left - 1, r.top - 1, r.right + 1, r.bottom + 1);
    SelectObject(dc, altP);
    SelectObject(dc, altS);
    DeleteObject(stift);
}

// Dunkle Titelleiste: 20 = DWMWA_USE_IMMERSIVE_DARK_MODE, aeltere
// Windows-10-Staende kennen nur 19. Scheitert beides, bleibt sie hell.
inline void DunkleTitelleiste(HWND h, bool dunkel) {
    if (!dunkel) return;
    const BOOL an = TRUE;
    if (FAILED(DwmSetWindowAttribute(h, 20, &an, sizeof(an)))) DwmSetWindowAttribute(h, 19, &an, sizeof(an));
}

} // namespace swbf2ui
