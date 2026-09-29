// ============================================================
//  swbf2import_animfenster.cpp - siehe swbf2import_animfenster.h
//
//  Gestaltung (0.41.0) nach denselben Regeln wie das Figurenfenster
//  (swbf2import_ui.h): Theme-Farben, WCAG-Kontraste, eigene Zeichnung,
//  dunkle Titelleiste; Oberflaeche auf Englisch wie das Figurenfenster.
//  Die Liste ist virtuell (LBS_NODATA): 82 229 Clips ohne Anzeigegrenze.
//  Figur als Auswahlfeld statt Haekchen; die Figur der Szene ist
//  vorgewaehlt. Sequenzen aus der Notizspur lassen sich durchklicken.
//
//  Das Clipverzeichnis kommt beim zweiten Mal aus der Ablage
//  (clips.fbclips, an die Kopfnummer des Spiels gebunden).
// ============================================================
#include <windows.h>
#include <uxtheme.h>

#include "swbf2import_animfenster.h"
#include "swbf2import_res.h"
#include "swbf2import_ui.h"
#include "fbauswahl.h"
#include "fbdatei.h"
#include "fbgame.h"
#include "fbebx.h"
#include "fbindex.h"
#include "fbfahrzeug.h"
#include "fbzuordnung.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace swbf2 {

namespace {

using swbf2ui::Mische;

struct AnimDaten {
    fbgame::Spiel spiel;
    fbindex::Index idx;
    fbanim::Quelle quelle;
    Ruhelagen ruhe;                            // walrus_humanmale (gemeinsames Skelett)
    std::map<std::string, Ruhelagen> ruheJeFigur;   // 0.68.0: eigenes Skelett je Figur (Grievous, Yoda, Droiden ...)
    // 0.72.0: Zuordnung wie im Spiel - je Figur die Clip-Schluessel, die der
    // Zustandsautomat fuer ihren Heldenwert erreicht (leer = keine gefunden).
    std::map<std::string, std::set<std::string>> spielClips;
    std::map<std::string, int> heldWert;
    // 1.24.0: Die Knochennamen JE CLIP einmal einsammeln (rund 30 s). Danach
    // ist die Clipliste jedes Fahrzeugs eine Mengenschnittmenge und damit
    // sofort da. Vorher rechnete jede Fahrzeugwahl neu und das Fenster stand.
    std::vector<std::pair<size_t, std::set<std::string>>> clipBones;
    bool clipBonesDa = false;
    std::string fahrzeugNotiz;                 // wird beim naechsten Fuelle() protokolliert
    std::string szeneFigur;                    // 1.27.0: Figur/Fahrzeug aus der Szene
    std::map<std::string, std::string> skelettJeFigur;
    std::string skelett;
    std::vector<std::string> suchtext;         // je Clip, klein geschrieben
    std::vector<std::string> figuren;          // Suchwoerter (anakin, darthvader, ...)
    std::set<std::string> fahrzeugNamen;       // 1.42.0: nur fuer diese gilt die Fahrzeug-Ruhelage
    std::vector<size_t> figurZahl;             // ladbare Clips je Figur
    std::vector<size_t> kopien;                // je Clip: in wie vielen Baenken derselbe Key steht
    std::atomic<int> zustand{ 0 };             // 0 frei, 1 liest, 2 fertig, 3 Fehler
    std::string fehler, ablageGrund;
    bool ausAblage = false;
    double sekunden = 0.0;
};

AnimDaten* g_daten = nullptr;                  // lebt bis Max endet (siehe Kopf)
std::chrono::steady_clock::time_point g_start;

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool Ladbar(const fbanim::ClipEintrag& c) {
    // 0.65.0: VBR und DCT dazu (eigene Decoder, siehe fbanim.cpp)
    return c.klasse == "RawAnimationAsset" || c.klasse == "FrameAnimationAsset" || c.klasse == "VbrAnimationAsset" || c.klasse == "DctAnimationAsset";
}

// win32/characters/hero/anakin/anakin_01/vur_anakin_01_bpb -> anakin
std::string FigurSchluessel(const std::string& quelle) {
    std::vector<std::string> teile;
    size_t a = 0;
    for (size_t i = 0; i <= quelle.size(); ++i) {
        if (i == quelle.size() || quelle[i] == '/') { teile.push_back(quelle.substr(a, i - a)); a = i + 1; }
    }
    for (size_t i = 0; i + 2 < teile.size(); ++i) if (teile[i] == "characters") return Klein(teile[i + 2]);
    return std::string();
}
// Namensvarianten einer Figur (0.44.2). Der Figurenschluessel kommt aus dem
// Ordnernamen (characters/hero/darthvader), die Clips heissen aber oft kurz
// (Vader, Dooku, Grievous, Phasma, Maul). Deshalb: der Schluessel selbst
// (wie bisher als Teilstring) und der Schluessel ohne Titel davor - dieser
// nur am WORTANFANG, damit "rey" nicht in "grey" trifft.
std::vector<std::string> FigurVarianten(const std::string& schluessel) {
    std::vector<std::string> v;
    static const char* const titel[] = { "darth", "count", "general", "captain", "emperor", "princess", "commander", "admiral", "lord" };
    for (const char* t : titel) {
        const size_t n = std::strlen(t);
        if (schluessel.size() >= n + 4 && schluessel.compare(0, n, t) == 0) v.push_back(schluessel.substr(n));
    }
    // 1.43.0: "bxcommanddroid" -> "bx". Die 33 Clips des BX heissen A_BX_...,
    // C_BX_..., EoR_BX_Victory_01 - ohne diese Kurzform bekam er das
    // Droidenprofil, und B1-Clips streckten seine Wirbelsaeule um 154 %.
    static const char* const endung[] = { "commanddroid" };
    for (const char* e : endung) {
        const size_t n = std::strlen(e);
        if (schluessel.size() > n + 1 && schluessel.compare(schluessel.size() - n, n, e) == 0) v.push_back(schluessel.substr(0, schluessel.size() - n));
    }
    return v;
}

bool AmWortanfang(const std::string& text, const std::string& wort) {
    for (size_t p = text.find(wort); p != std::string::npos; p = text.find(wort, p + 1)) {
        const char v = p == 0 ? ' ' : text[p - 1];
        if (!((v >= 'a' && v <= 'z') || (v >= '0' && v <= '9'))) return true;
    }
    return false;
}

// Profile fuer Standardklassen (0.66.0). Helden werden ueber ihren Namen im
// Clipnamen gefunden; Soldaten nicht - ihre Clips tragen Klassen- und
// Waffenwoerter (gemessen in START.log 0.49.0: rifle 3734, heavy 1178,
// officer 1264, assault 100, specialist 28, trooper 921, droid 738 Clips).
// Ein Profil passt, wenn EINES seiner Woerter im Clip vorkommt. Mit dem
// Suchfeld laesst sich weiter eingrenzen (z. B. "imperial", "rebel").
struct Profil { const char* schluessel; const char* anzeige; std::vector<const char*> woerter; const char* klassenwort; };
const std::vector<Profil>& Profile() {
    static const std::vector<Profil> p = {
        { "#assault",    "Soldier: Assault (rifle)",      { "assault", "rifle" },       "assault" },
        { "#heavy",      "Soldier: Heavy",                { "heavy" },                  "heavy" },
        { "#officer",    "Soldier: Officer (pistol)",     { "officer", "pistol" },      "officer" },
        { "#specialist", "Soldier: Specialist (sniper)",  { "specialist", "sniper" },   "specialist" },
        { "#droid",      "Droid soldier (B1/B2)",         { "droid", "b1_", "b2_" },    "droid" },
    };
    return p;
}

const Profil* ProfilFuer(const std::string& schluessel) {
    for (const Profil& p : Profile()) if (schluessel == p.schluessel) return &p;
    return nullptr;
}

// Anzeigename in der Figurenliste (Profile mit Klartext).
std::string Anzeige(const std::string& schluessel) {
    const Profil* p = ProfilFuer(schluessel);
    return p != nullptr ? std::string(p->anzeige) : schluessel;
}

// 1.43.0: Clip der Ich-Ansicht? Nur ganze Namensteile zaehlen ("1p_Rifle_...",
// "Iden_A3_Idle_1P_Aim_Idle") - ein Teilstring "1p" stuende auch in anderen Namen.
// 1.43.0: steht "wort" als ganzer Namensteil (zwischen '_') im Namen?
bool Namensteil(const std::string& klein, const char* wort) {
    const size_t n = std::strlen(wort);
    size_t a = 0;
    while (a <= klein.size()) {
        size_t e = klein.find('_', a);
        if (e == std::string::npos) e = klein.size();
        if (e - a == n && klein.compare(a, n, wort) == 0) return true;
        a = e + 1;
    }
    return false;
}

bool IchClip(const std::string& klein) {
    size_t a = 0;
    while (a <= klein.size()) {
        size_t e = klein.find('_', a);
        if (e == std::string::npos) e = klein.size();
        if (e - a == 2 && klein.compare(a, 2, "1p") == 0) return true;
        a = e + 1;
    }
    return false;
}

bool PasstZuFigur(const std::string& suchtext, const std::string& schluessel) {
    if (const Profil* p = ProfilFuer(schluessel)) {
        for (const char* w : p->woerter) {
            const std::string wort(w);
            if (wort.size() >= 5 ? suchtext.find(wort) != std::string::npos : AmWortanfang(suchtext, wort)) return true;
        }
        return false;
    }
    // Kurze Schluessel (rey, luke, leia, yoda) nur am Wortanfang - sonst traefe
    // "rey" auch "grey"; ab fuenf Zeichen wie bisher als Teilstring.
    if (schluessel.size() >= 5 ? suchtext.find(schluessel) != std::string::npos : AmWortanfang(suchtext, schluessel)) return true;
    for (const std::string& w : FigurVarianten(schluessel)) if (AmWortanfang(suchtext, w)) return true;
    return false;
}


const std::set<std::string>& SpielClips(const std::string& figur);   // 1.27.0: weiter unten definiert

void Lies(AnimDaten* d, std::wstring spielordner, std::wstring ablage) {
    const auto t0 = std::chrono::steady_clock::now();
    std::string f, grund;
    if (!d->spiel.Oeffne(fbdatei::Utf8(spielordner), f)) { d->fehler = f; d->zustand = 3; return; }
    if (!fbindex::LadeIndex(d->idx, fbdatei::Utf8(ablage + L"\\index.fbidx"), d->spiel.Kopfnummer(), grund)) {
        d->idx = fbindex::Index();
        if (!fbindex::BaueIndex(d->spiel, d->idx, f)) { d->fehler = f; d->zustand = 3; return; }
    }
    for (const auto& kv : d->idx.ebx) {
        if (Klein(kv.first).find("characters/rigs/humanoids/walrus_humanmale") == std::string::npos) continue;
        std::vector<uint8_t> roh;
        fbebx::Datei e;
        if (!d->spiel.HoleNachSha1(kv.second.sha1, roh, f) || !e.Lies(roh, f)) continue;
        const fbebx::Skelett sk = fbebx::LiesSkelett(e);
        if (!sk.gefunden) continue;
        d->skelett = kv.first;
        for (size_t i = 0; i < sk.namen.size() && i < sk.lokal.size(); ++i) d->ruhe[sk.namen[i]] = sk.lokal[i];
        break;
    }
    if (d->ruhe.empty()) { d->fehler = "skeleton Walrus_HumanMale not found"; d->zustand = 3; return; }
    // Clipverzeichnis: erst aus der Ablage, sonst aus allen Baenken lesen und ablegen.
    const std::string ablageDatei = fbdatei::Utf8(ablage + L"\\clips.fbclips");
    if (d->quelle.Lade(d->spiel, ablageDatei, d->spiel.Kopfnummer(), grund)) {
        d->ausAblage = true;
    } else {
        d->ablageGrund = grund;
        if (!d->quelle.Baue(d->spiel, d->idx, f)) { d->fehler = f; d->zustand = 3; return; }
        std::string f2;
        d->quelle.Speichere(ablageDatei, d->spiel.Kopfnummer(), f2);
    }
    const auto& clips = d->quelle.Clips();
    d->suchtext.reserve(clips.size());
    for (const fbanim::ClipEintrag& c : clips) d->suchtext.push_back(Klein(c.anzeige + "|" + c.name + "|" + d->quelle.BankName(c.bank)));
    // Figuren aus der Figurenliste des Index; nur die, fuer die es ladbare Clips gibt.
    std::map<std::string, size_t> zahl;
    for (const fbauswahl::Figur& fi : fbauswahl::Figuren(d->idx)) {
        const std::string k = FigurSchluessel(fi.name);
        if (k.size() >= 3) zahl.emplace(k, 0);
    }
    for (const Profil& p : Profile()) zahl.emplace(p.schluessel, 0);   // 0.66.0: Standardklassen
    for (auto& kv : zahl) {
        std::set<std::string> gesehen;
        for (size_t i = 0; i < clips.size(); ++i)
            if (Ladbar(clips[i]) && PasstZuFigur(d->suchtext[i], kv.first) &&
                (kv.first[0] != '#' ? !IchClip(Klein(clips[i].anzeige)) : fbzuordnung::PasstZumSkelett(kv.first, Klein(clips[i].anzeige))) &&   // 1.43.0, wie in Treffer
                gesehen.insert(clips[i].key).second) ++kv.second;
    }
    // Derselbe Clip (gleicher Key) steht oft in vielen Baenken - die Liste zeigt
    // ihn einmal und in der Bankspalte, wie oft er vorkommt.
    {
        std::map<std::string, size_t> jeKey;
        for (const fbanim::ClipEintrag& c : clips) ++jeKey[c.key];
        d->kopien.reserve(clips.size());
        for (const fbanim::ClipEintrag& c : clips) d->kopien.push_back(jeKey[c.key]);
    }
    for (const auto& kv : zahl) if (kv.second > 0) { d->figuren.push_back(kv.first); d->figurZahl.push_back(kv.second); }
    // 1.21.0: Fahrzeuge in die Figurenliste. Ihre Clips traegt kein Name (die
    // Zuordnung laeuft ueber die Knochen), deshalb kamen sie in der Auswahl
    // gar nicht vor - im Fenster stand nur "All characters".
    for (const fbfahrzeug::Fahrzeug& v : fbfahrzeug::Fahrzeuge(d->idx)) {
        if (v.meshsetNamen.empty() || v.skelett.empty()) continue;
        d->figuren.push_back(v.name);
        d->fahrzeugNamen.insert(v.name);
        d->figurZahl.push_back(0);                 // wird beim Waehlen gerechnet
    }
    // 1.27.0: Steht ein FAHRZEUG in der Szene, schon hier rechnen - im
    // Ladefaden, nicht spaeter im Fenster. Dann steht die Zahl sofort statt
    // "(?)", und es haengt nichts, wenn DH das Fahrzeug waehlt.
    if (!d->szeneFigur.empty())
        for (const std::string& v : d->figuren)
            if (v == d->szeneFigur) { SpielClips(v); break; }
    d->sekunden = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    d->zustand = 2;
}

struct Fenster {
    AnimBruecke b;
    HWND h = nullptr;
    swbf2ui::Palette pal;
    HFONT fontNormal = nullptr, fontFett = nullptr;
    int zeilenHoehe = 16;
    std::vector<size_t> sichtbar;
    int figur = -1;                            // Index in g_daten->figuren, -1 = alle
    bool nurLadbar = true, notiz = true, gefuellt = false;
    std::vector<Sequenz> sequenzen;
    std::vector<RigInfo> rigs;                 // Rigs in der Szene (0.48.0)
    int rigWahl = -1;                          // Index in rigs, -1 = keins markiert (alte Szenen)
    std::wstring status;
    bool statusFehler = false;
};

void Protokoll(const Fenster& f, const std::string& z) { if (f.b.protokoll != nullptr) f.b.protokoll(z.c_str()); }

void Status(Fenster& f, const std::wstring& t, bool fehler = false) {
    f.status = t;
    f.statusFehler = fehler;
    InvalidateRect(GetDlgItem(f.h, IDC_A_STATUS), nullptr, TRUE);
    UpdateWindow(GetDlgItem(f.h, IDC_A_STATUS));
}

std::vector<std::string> Suchwoerter(HWND h) {
    wchar_t puffer[256] = {};
    GetDlgItemTextW(h, IDC_A_SUCHE, puffer, 255);
    const std::string s = Klein(fbdatei::Utf8(puffer));
    std::vector<std::string> w;
    std::string cur;
    for (char c : s) { if (c == ' ') { if (!cur.empty()) w.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

// Die aktuelle Auswahl: Figur, Suchwoerter (alle muessen passen), ladbar.
// Zuordnung wie im Spiel (0.72.0): Wurzeln und Heldenwert aus den EBX der
// Figur, dann dem Zustandsautomaten mit Helden-Filter folgen. Einmal je Figur
// (zwischengespeichert), dauert etwa eine Sekunde.
void BaueClipBones() {
    AnimDaten* d = g_daten;
    if (d->clipBonesDa) return;
    // 1.25.0: Das dauert einmalig etwa eine halbe Minute. Ohne Sanduhr und
    // Hinweis sieht es aus, als haenge das Fenster.
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    d->clipBonesDa = true;
    const auto& clips = d->quelle.Clips();
    std::set<std::string> gesehen;
    d->clipBones.reserve(clips.size() / 3 + 8);
    for (size_t i = 0; i < clips.size(); ++i) {
        if (!gesehen.insert(clips[i].key).second) continue;
        std::vector<std::string> namen;
        std::string rig;
        size_t benannt = 0;
        if (!d->quelle.Kanalnamen(clips[i], namen, rig, benannt) || namen.empty()) continue;
        std::set<std::string> bones;
        for (const std::string& n : namen) {
            if (n.empty()) continue;
            std::string kurz = Klein(n);
            const size_t p = kurz.find_last_of('.');
            if (p != std::string::npos) kurz = kurz.substr(0, p);
            bones.insert(kurz);
        }
        if (!bones.empty()) d->clipBones.push_back({ i, std::move(bones) });
    }
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

const std::set<std::string>& SpielClips(const std::string& figur) {
    AnimDaten* d = g_daten;
    const auto da = d->spielClips.find(figur);
    if (da != d->spielClips.end()) return da->second;
    std::set<std::string> clipsGefunden;
    // 1.13.0: Fahrzeuge - ihre Clips erkennt man an den Knochennamen, nicht am
    // Namen (Z13: AT-TE 44 Clips, AT-RT 32). Steht ein Fahrzeug in der Szene,
    // wird sein eigenes Skelett genommen und daraus die Clipliste gebildet.
    {
        // Das eigene Skelett des Fahrzeugs suchen; gibt es keins, ist es keine
        // Fahrzeugfigur und es geht unten normal weiter.
        const std::string skelett = fbfahrzeug::SucheSkelett(d->spiel, d->idx, figur);
        if (!skelett.empty()) {
            std::set<std::string> eigene;
            for (const auto& kv : d->idx.ebx) {
                if (kv.first != skelett) continue;
                std::vector<uint8_t> roh;
                std::string f3;
                fbebx::Datei e;
                if (!d->spiel.HoleNachSha1(kv.second.sha1, roh, f3) || !e.Lies(roh, f3)) break;
                const fbebx::Skelett sk = fbebx::LiesSkelett(e);
                for (const std::string& b : sk.namen) eigene.insert(Klein(b));
                break;
            }
            for (const auto& kv : d->ruhe) eigene.erase(Klein(kv.first));      // menschliche Bones raus
            if (eigene.size() >= 4) {
                BaueClipBones();
                const auto tz = std::chrono::steady_clock::now();
                for (const auto& cb : d->clipBones) {
                    size_t drin = 0;
                    for (const std::string& b : eigene) if (cb.second.count(b)) ++drin;
                    if (drin * 2 >= eigene.size()) clipsGefunden.insert(d->quelle.Clips()[cb.first].key);
                }
                d->heldWert[figur] = -1;
                // Das Protokoll haengt am Fenster; hier merken und in Fuelle() ausgeben.
                d->fahrzeugNotiz = "ANIM Fahrzeug " + figur + ": Skelett " + skelett + ", " + std::to_string(eigene.size()) +
                                   " eigene Bones -> " + std::to_string(clipsGefunden.size()) + " Clips in " +
                                   std::to_string(static_cast<int>(std::chrono::duration<double>(std::chrono::steady_clock::now() - tz).count())) + " s";
                return d->spielClips.emplace(figur, std::move(clipsGefunden)).first->second;
            }
        }
    }
    if (!figur.empty()) {                       // 0.75.0: auch die Klassenprofile (#assault ...)
        const fbzuordnung::Wurzeln w = fbzuordnung::LiesWurzeln(d->spiel, d->idx, d->quelle, figur);
        d->heldWert[figur] = w.held;
        // 0.80.0: die obersten Automaten (.3P.Top.SF ...) als zusaetzliche
        // Wurzeln - die Figuren-EBX verweisen sie nicht, dort haengen aber die
        // sichtbaren Bewegungen.
        std::vector<std::string> wurzeln = w.keys;
        for (const std::string& k : fbzuordnung::AutomatenWurzeln(d->quelle)) wurzeln.push_back(k);
        if (!wurzeln.empty() && !w.zustaende.empty()) clipsGefunden = d->quelle.FolgeFuerZustaende(wurzeln, w.zustaende, 400000).clipsHeld;
    }
    return d->spielClips.emplace(figur, std::move(clipsGefunden)).first->second;
}

std::vector<size_t> Treffer(const Fenster& f, bool nurLadbare) {
    AnimDaten* d = g_daten;
    std::vector<size_t> aus;
    const std::vector<std::string> worte = Suchwoerter(f.h);
    const std::string figur = (f.figur >= 0 && static_cast<size_t>(f.figur) < d->figuren.size()) ? d->figuren[static_cast<size_t>(f.figur)] : std::string();
    const auto& clips = d->quelle.Clips();
    const std::set<std::string>* spiel = figur.empty() ? nullptr : &SpielClips(figur);
    // 1.25.0: Fahrzeuge kennen ihre Zahl jetzt - in die Auswahl nachtragen.
    if (spiel != nullptr && f.figur >= 0 && static_cast<size_t>(f.figur) < d->figurZahl.size() && d->figurZahl[static_cast<size_t>(f.figur)] == 0)
        d->figurZahl[static_cast<size_t>(f.figur)] = spiel->size();
    std::set<std::string> gesehen;                 // derselbe Clip steht in vielen Baenken: nur einmal
    const bool szeneIch = Klein(f.b.figur).find("bundle1p") != std::string::npos;   // 1.43.0
    // 1.43.0: welcher Droide steht in der Szene? B2 oder B1 - nach den Namen
    // (d_heavy_preq ist ein B1 mit schwerer Waffe: er traegt den B1-Koerper).
    std::string droideArt;
    {
        const std::string s = Klein(f.b.figurSchluessel + "|" + f.b.figur + "|" + f.b.szeneMeshes);
        if (s.find("b2") != std::string::npos) droideArt = "b2";
        else if (s.find("b1") != std::string::npos || (s.find("d_") != std::string::npos && s.find("_preq") != std::string::npos)) droideArt = "b1";
    }
    for (size_t i = 0; i < clips.size(); ++i) {
        if (nurLadbare && !Ladbar(clips[i])) continue;
        // Figur: Name im Clip ODER vom Spiel dieser Figur zugeordnet (0.72.0);
        // zugeordnete Clips muessen zum Skelett passen (0.81.0: keine
        // Ich-Ansicht, keine Kampfdroiden-Clips bei Menschen).
        if (!figur.empty() && !PasstZuFigur(d->suchtext[i], figur) &&
            !(spiel != nullptr && spiel->count(clips[i].key) && fbzuordnung::PasstZumSkelett(figur, Klein(clips[i].anzeige)))) continue;
        // 1.43.0: Profile finden ihre Clips ueber Woerter wie "rifle" - die stehen
        // auch in den Clips der Kampfdroiden (B1_Rifle_...) und der Ich-Ansicht
        // (1p_Rifle_...). Gemessen am Assault-Profil auf der Ehrengarde: 256
        // Droiden-Clips, Kopf um 262 % gestreckt. Fuer Profile gilt deshalb
        // dieselbe Skelettregel wie fuer die Zuordnung des Spiels.
        if (!figur.empty() && figur[0] == '#' && !fbzuordnung::PasstZumSkelett(figur, Klein(clips[i].anzeige))) continue;
        // 1.43.0: Im Droidenprofil B1 und B2 trennen - B1-Clips auf dem B2-Skelett
        // streckten die Wirbelsaeule um 169 %.
        if (figur == "#droid") {
            const std::string a = Klein(clips[i].anzeige);
            const bool b1 = Namensteil(a, "b1") || a.rfind("b1", 0) == 0, b2 = Namensteil(a, "b2") || a.rfind("b2", 0) == 0;
            // Nur echte Droidenclips: "droid" steht auch in AI_Trooper_DroidShock
            // (Mensch reagiert auf Droiden) und C_FlyingDroids_... - auf dem B1
            // Kopf um 72 % gestreckt.
            if (!b1 && !b2) continue;
            if (!droideArt.empty() && (droideArt == "b2" ? (b1 && !b2) : (b2 && !b1))) continue;
        }
        // 1.43.0: Heldenclips kommen ueber den Namen - auch die der Ich-Ansicht
        // (Iden_A3_Idle_1P_Aim_Idle auf der Aussenansicht: kleiner Finger um
        // 3.759 % gestreckt, Huelle x5,3). Nur fuer eine Ich-Ansicht in der Szene.
        if (!figur.empty() && figur[0] != '#' && !szeneIch && IchClip(Klein(clips[i].anzeige))) continue;
        if (!clips[i].key.empty() && !gesehen.insert(clips[i].key).second) continue;
        bool passt = true;
        for (const std::string& w : worte) if (d->suchtext[i].find(w) == std::string::npos) { passt = false; break; }
        if (passt) aus.push_back(i);
    }
    std::stable_sort(aus.begin(), aus.end(), [&](size_t a, size_t b) {
        const std::string& na = clips[a].anzeige.empty() ? clips[a].name : clips[a].anzeige;
        const std::string& nb = clips[b].anzeige.empty() ? clips[b].name : clips[b].anzeige;
        return na < nb;
    });
    return aus;
}

void Fuelle(Fenster& f) {
    f.sichtbar = Treffer(f, f.nurLadbar);
    HWND liste = GetDlgItem(f.h, IDC_A_LISTE);
    SendMessageW(liste, LB_SETCOUNT, f.sichtbar.size(), 0);
    InvalidateRect(liste, nullptr, TRUE);
    if (!g_daten->fahrzeugNotiz.empty()) {
        Protokoll(f, g_daten->fahrzeugNotiz);
        g_daten->fahrzeugNotiz.clear();
        // 1.25.0: Den Eintrag des Fahrzeugs neu beschriften ("(?)" -> Zahl),
        // ohne die Auswahl zu verlieren.
        if (f.figur >= 0 && static_cast<size_t>(f.figur) < g_daten->figuren.size()) {
            HWND c = GetDlgItem(f.h, IDC_A_FIGUR);
            const int pos = f.figur + 1;
            const std::string t = Anzeige(g_daten->figuren[static_cast<size_t>(f.figur)]) + "  (" +
                                  std::to_string(g_daten->figurZahl[static_cast<size_t>(f.figur)]) + ")";
            const std::wstring w = fbdatei::Breit(t);
            SendMessageW(c, CB_DELETESTRING, static_cast<WPARAM>(pos), 0);
            SendMessageW(c, CB_INSERTSTRING, static_cast<WPARAM>(pos), reinterpret_cast<LPARAM>(w.c_str()));
            SendMessageW(c, CB_SETCURSEL, static_cast<WPARAM>(pos), 0);
        }
    }
    std::wstring z = std::to_wstring(f.sichtbar.size()) + L" clips";
    if (f.figur >= 0) {
        const std::string& fig = g_daten->figuren[static_cast<size_t>(f.figur)];
        z += fbdatei::Breit(" for " + Anzeige(fig));
        const std::set<std::string>& sp = SpielClips(fig);
        if (!sp.empty()) {
            size_t ausSpiel = 0;
            for (size_t i : f.sichtbar) if (sp.count(g_daten->quelle.Clips()[i].key)) ++ausSpiel;   // sichtbar ist bereits gefiltert
            z += L" (" + std::to_wstring(ausSpiel) + L" from the game's own assignment";
            const auto hw = g_daten->heldWert.find(fig);
            if (hw != g_daten->heldWert.end() && hw->second > 0) z += L", hero value " + std::to_wstring(hw->second);
            z += L")";
        }
    }
    z += L". Select one and Load (or double-click) - each clip replaces the previous one.";
    Status(f, z);
    f.gefuellt = true;
}

// Eintrag der Figurenliste fuer eine Szenenfigur: erst der Held mit genau
// diesem Schluessel, sonst das Profil, dessen Klassenwort im Schluessel steht
// (l_assault_newera -> Soldier: Assault). -1 = keiner.
int FigurIndexFuer(const std::string& schluessel) {
    const AnimDaten* d = g_daten;
    if (schluessel.empty()) return -1;
    for (size_t i = 0; i < d->figuren.size(); ++i) if (d->figuren[i] == schluessel) return static_cast<int>(i);
    const std::string k = Klein(schluessel);
    // 1.43.0: Die Separatisten-Klassen (d_assault_preq, d_heavy_preq ...) sind
    // Droiden. Bisher gewann das Klassenwort ("assault") - der B1 bekam
    // Menschenclips, die Beine standen flach zur Seite, Kopf um 72 % gestreckt.
    const bool droide = (k.rfind("d_", 0) == 0 && k.find("_preq") != std::string::npos) ||
                        k.rfind("b1", 0) == 0 || k.rfind("b2", 0) == 0 || k.find("droid") != std::string::npos;
    if (droide)
        for (size_t i = 0; i < d->figuren.size(); ++i) if (d->figuren[i] == "#droid") return static_cast<int>(i);
    for (const Profil& p : Profile()) {
        if (k.find(p.klassenwort) == std::string::npos) continue;
        for (size_t i = 0; i < d->figuren.size(); ++i) if (d->figuren[i] == p.schluessel) return static_cast<int>(i);
    }
    return -1;
}

void FuelleFiguren(Fenster& f) {
    AnimDaten* d = g_daten;
    HWND c = GetDlgItem(f.h, IDC_A_FIGUR);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All characters"));
    int wahl = 0;
    // 1.43.0: Bei einem FAHRZEUG nur der genaue Eintrag (at-st, at_te ...) - kein
    // Profil ueber ein Klassenwort im Namen (lucrehulkclass_droidbattleship
    // bekam sonst "Droid soldier").
    const bool fahrzeugSzene = Klein(f.b.figur).rfind("vehicle: ", 0) == 0;
    int passend = -1;
    if (fahrzeugSzene) {
        for (size_t i = 0; i < d->figuren.size(); ++i) if (d->figuren[i] == f.b.figurSchluessel) passend = static_cast<int>(i);
    } else {
        passend = FigurIndexFuer(f.b.figurSchluessel);
    }
    // Nur fuer eine FIGUR in der Szene - nicht fuer Fahrzeuge: beim Vulture-
    // Droiden und Droid-Trifighter stand "droid" im Mesh-Namen, und das Fenster
    // waehlte die Droiden-Soldaten (1.968 Clips auf ein Fahrzeug); die MC80
    // bekam ueber "heavy" im Mesh-Namen "Soldier: Heavy". Fahrzeuge tragen im
    // Vermerk "vehicle: <art>/<name>".
    if (passend < 0 && !f.b.szeneMeshes.empty() && !f.b.figurSchluessel.empty() && !fahrzeugSzene) {
        // 1.43.0: Der Figurname traegt kein Klassenwort (arctrooper,
        // alderaanhonorguard) - dann das Profil, dessen Klassenwort in den
        // Mesh-Namen der Szene am haeufigsten vorkommt. Vorher stand hier
        // "All characters", und "Load all" haette das ganze Spiel geladen.
        size_t beste = 0;
        // Droiden zuerst: der B2 heisst in den Materialien "M_D_Heavy_Preq_01_..."
        // (im Spiel die schwere Klasse der Separatisten) - das Klassenwort haette
        // das MENSCHEN-Profil Heavy gewaehlt, 959 von 1.191 Clips passten nicht.
        const std::string alles = "|" + Klein(f.b.figurSchluessel) + "|" + f.b.szeneMeshes;
        const bool droide = alles.find("droid") != std::string::npos || alles.find("|b1_") != std::string::npos ||
                            alles.find("|b2_") != std::string::npos || alles.find("|b1|") != std::string::npos || alles.find("|b2|") != std::string::npos;
        if (droide)
            for (size_t i = 0; i < d->figuren.size(); ++i)
                if (d->figuren[i] == "#droid") { passend = static_cast<int>(i); beste = static_cast<size_t>(-1); }
        for (const Profil& p : Profile()) {
            size_t n = 0;
            for (size_t pos = f.b.szeneMeshes.find(p.klassenwort); pos != std::string::npos; pos = f.b.szeneMeshes.find(p.klassenwort, pos + 1)) ++n;
            if (n > beste) {
                for (size_t i = 0; i < d->figuren.size(); ++i)
                    if (d->figuren[i] == p.schluessel) { beste = n; passend = static_cast<int>(i); }
            }
        }
        if (passend >= 0) Protokoll(f, "ANIM Profil aus den Mesh-Namen der Szene: " + Anzeige(d->figuren[static_cast<size_t>(passend)]));
    }
    for (size_t i = 0; i < d->figuren.size(); ++i) {
        // 1.24.0: Fahrzeuge kennen ihre Zahl erst nach der ersten Wahl - dann
        // steht sie hier, vorher ein Fragezeichen statt einer falschen 0.
        const bool offen = d->figurZahl[i] == 0 && d->spielClips.find(d->figuren[i]) == d->spielClips.end();
        std::string t = Anzeige(d->figuren[i]) + "  (" + (offen ? std::string("?") : std::to_string(
            d->figurZahl[i] > 0 ? d->figurZahl[i] : d->spielClips[d->figuren[i]].size())) + ")";
        if (static_cast<int>(i) == passend) { t += "  - in scene"; wahl = static_cast<int>(i) + 1; }
        const std::wstring w = fbdatei::Breit(t);
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
    }
    SendMessageW(c, CB_SETCURSEL, wahl, 0);
    f.figur = wahl - 1;
}

// Rig-Auswahl (0.48.0): welches Rig bekommt die Clips. Nach dem Wechsel wird
// die Figur passend vorgewaehlt - Vaders Clips auf Anakins Rig gehen trotzdem,
// die Figurenauswahl bleibt frei.
unsigned long RigHandle(const Fenster& f) {
    return (f.rigWahl >= 0 && static_cast<size_t>(f.rigWahl) < f.rigs.size()) ? f.rigs[static_cast<size_t>(f.rigWahl)].handle : 0;
}

void FuelleRigs(Fenster& f, bool figurMitwaehlen) {
    const unsigned long vorher = RigHandle(f);
    f.rigs = f.b.rigs != nullptr ? f.b.rigs() : std::vector<RigInfo>();
    HWND c = GetDlgItem(f.h, IDC_A_RIG);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    if (f.rigs.empty()) {
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(skeleton in scene)"));
        SendMessageW(c, CB_SETCURSEL, 0, 0);
        f.rigWahl = -1;
        return;
    }
    int wahl = 0;
    for (size_t i = 0; i < f.rigs.size(); ++i) {
        const RigInfo& r = f.rigs[i];
        const std::wstring t = fbdatei::Breit(r.name + (r.figur.empty() ? std::string() : "  (" + r.figur + ")"));
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
        if (vorher != 0 ? r.handle == vorher : (!f.b.figurSchluessel.empty() && r.figur == f.b.figurSchluessel)) wahl = static_cast<int>(i);
    }
    SendMessageW(c, CB_SETCURSEL, wahl, 0);
    f.rigWahl = wahl;
    (void)figurMitwaehlen;
}

std::wstring Bilder(double b) {
    wchar_t t[32];
    swprintf(t, 32, L"%.0f", b);
    return t;
}

void FuelleSequenzen(Fenster& f) {
    HWND c = GetDlgItem(f.h, IDC_A_SEQUENZ);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    if (f.sequenzen.empty()) {
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(no sequences - use \"Load all to timeline\")"));
        SendMessageW(c, CB_SETCURSEL, 0, 0);
        EnableWindow(c, FALSE);
        return;
    }
    double ende = 0.0;
    for (const Sequenz& s : f.sequenzen) ende = std::max(ende, s.endeBild);
    const std::wstring alle = L"Whole timeline  (0 - " + Bilder(ende) + L")";
    SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(alle.c_str()));
    for (size_t i = 0; i < f.sequenzen.size(); ++i) {
        const Sequenz& s = f.sequenzen[i];
        const std::wstring t = std::to_wstring(i + 1) + L"   " + fbdatei::Breit(s.name) + L"  (" + Bilder(s.startBild) + L" - " +
                               Bilder(s.endeBild) + L")";
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
    }
    SendMessageW(c, CB_SETCURSEL, 0, 0);
    EnableWindow(c, TRUE);
}

void ZeigeSequenz(Fenster& f) {
    if (f.b.zeigeBereich == nullptr || f.sequenzen.empty()) return;
    const LRESULT i = SendDlgItemMessageW(f.h, IDC_A_SEQUENZ, CB_GETCURSEL, 0, 0);
    if (i == CB_ERR) return;
    if (i == 0) {
        int ende = 0;
        for (const Sequenz& s : f.sequenzen) ende = std::max(ende, s.ende);
        f.b.zeigeBereich(0, ende);
        return;
    }
    const Sequenz& s = f.sequenzen[static_cast<size_t>(i - 1)];
    f.b.zeigeBereich(s.start, s.ende);
    Status(f, fbdatei::Breit("Sequence " + std::to_string(i) + ": " + s.name) + L"  (" + Bilder(s.startBild) + L" - " +
                  Bilder(s.endeBild) + L")");
}

// Ruhelage fuer das Ziel-Rig (0.68.0). Bisher kam sie fuer JEDE Figur aus
// walrus_humanmale - bei Figuren mit eigenem Skelett (Grievous: 114 Bones,
// vier Arme, andere Proportionen) setzte das Knochen ohne Verschiebungskanal
// auf menschliche Laengen und die Startpose auf die Walrus-Pose. Jetzt: gibt
// es ein Skelett "<figur>..._ske" (ohne _1p), wird dessen LocalPose genommen.
const Ruhelagen& RuheFuer(Fenster& f, const std::string& figur) {
    AnimDaten* d = g_daten;
    if (figur.empty()) return d->ruhe;
    const auto da = d->ruheJeFigur.find(figur);
    if (da != d->ruheJeFigur.end()) return da->second.empty() ? d->ruhe : da->second;
    Ruhelagen r;
    std::string gefunden;
    // 1.28.0: FAHRZEUGE haben ihre Ruhelage nicht unter characters/. Ohne sie
    // wurden die AT-TE-Clips gegen das MENSCHENSKELETT gerechnet - dabei kippte
    // der Laeufer um und nur 8 von 129 Bones bewegten sich.
    {
        // 1.31.0: Auch wenn der Rigname noch die lange Form traegt
        // ("vehicle: ground/at_te"), den hinteren Teil verwenden.
        std::string kurz = figur;
        if (kurz.compare(0, 9, "vehicle: ") == 0) kurz = kurz.substr(9);
        const size_t s2 = kurz.find_last_of('/');
        if (s2 != std::string::npos) kurz = kurz.substr(s2 + 1);
        // 1.42.0: nur fuer echte Fahrzeuge. Die Namenssuche traf sonst auch
        // Figuren - bei Iden die Trident-Drohne ("tr-iden-t"): dann stand die
        // Ruhelage auf einem Skelett ohne ihre Koerperknochen.
        const std::string skelett = d->fahrzeugNamen.count(kurz) ? fbfahrzeug::SucheSkelett(d->spiel, d->idx, kurz) : std::string();
        if (!skelett.empty()) {
            const auto it = d->idx.ebx.find(skelett);
            if (it != d->idx.ebx.end()) {
                std::vector<uint8_t> roh;
                std::string f3;
                fbebx::Datei e;
                if (d->spiel.HoleNachSha1(it->second.sha1, roh, f3) && e.Lies(roh, f3)) {
                    const fbebx::Skelett sk = fbebx::LiesSkelett(e);
                    if (sk.gefunden && !sk.namen.empty()) {
                        for (size_t i = 0; i < sk.namen.size() && i < sk.lokal.size(); ++i) r[sk.namen[i]] = sk.lokal[i];
                        Protokoll(f, "ANIM Ruhelage fuer " + kurz + ": Fahrzeugskelett " + skelett + " mit " +
                                     std::to_string(r.size()) + " Bones");
                        return d->ruheJeFigur.emplace(figur, std::move(r)).first->second;
                    }
                }
            }
        }
    }
    // 1.43.0: Fuer die Figur IN DER SZENE dieselbe Skelettregel wie beim Import
    // (fbauswahl::SkelettFuer, mit dem Ordner der Figurmeshes). Separatisten-
    // Officer/-Heavy/-Specialist tragen den B1-Koerper; ihre Ruhelage kam hier
    // bisher vom Menschenskelett, weil ihr eigener Ordner kein *_ske hat.
    if (figur == f.b.figurSchluessel && Klein(f.b.figur).rfind("win32/", 0) == 0) {
        std::string herkunft;
        const std::string sk = fbauswahl::SkelettFuer(d->idx, f.b.figur, herkunft);
        const auto it = sk != fbauswahl::kGemeinsamesSkelett ? d->idx.ebx.find(sk) : d->idx.ebx.end();
        if (it != d->idx.ebx.end()) {
            std::vector<uint8_t> roh;
            std::string f3;
            fbebx::Datei e;
            if (d->spiel.HoleNachSha1(it->second.sha1, roh, f3) && e.Lies(roh, f3)) {
                const fbebx::Skelett ske = fbebx::LiesSkelett(e);
                if (ske.gefunden && !ske.namen.empty()) {
                    for (size_t i = 0; i < ske.namen.size() && i < ske.lokal.size(); ++i) r[ske.namen[i]] = ske.lokal[i];
                    d->skelettJeFigur[figur] = sk;
                    Protokoll(f, "ANIM Ruhelage fuer " + figur + ": " + sk + " (" + herkunft + ", " + std::to_string(r.size()) + " Bones)");
                    return d->ruheJeFigur.emplace(figur, std::move(r)).first->second;
                }
            }
        }
    }
    for (const auto& kv : d->idx.ebx) {
        const std::string n = Klein(kv.first);
        if (n.compare(0, 11, "characters/") != 0 || n.size() < 5 || n.compare(n.size() - 4, 4, "_ske") != 0) continue;
        if (n.find("_1p") != std::string::npos) continue;
        // 1.42.0: Die Figur muss ein eigener ORDNER im Pfad sein
        // (characters/hero/yoda/...), kein Teilstring des Dateinamens - sonst
        // passte "iden" auf characters/npc/vehicles/trident/trident_01_ske.
        if (n.find("/" + figur + "/") == std::string::npos) continue;
        std::vector<uint8_t> roh;
        std::string f2;
        fbebx::Datei e;
        if (!d->spiel.HoleNachSha1(kv.second.sha1, roh, f2) || !e.Lies(roh, f2)) continue;
        const fbebx::Skelett sk = fbebx::LiesSkelett(e);
        if (!sk.gefunden) continue;
        for (size_t i = 0; i < sk.namen.size() && i < sk.lokal.size(); ++i) r[sk.namen[i]] = sk.lokal[i];
        gefunden = kv.first;
        break;
    }
    d->skelettJeFigur[figur] = gefunden.empty() ? d->skelett : gefunden;
    Protokoll(f, "ANIM Ruhelage fuer " + figur + ": " + (gefunden.empty() ? "gemeinsames Skelett " + d->skelett
                                                                          : "eigenes Skelett " + gefunden + " (" + std::to_string(r.size()) + " Bones)"));
    auto& platz = d->ruheJeFigur[figur];
    platz = std::move(r);
    return platz.empty() ? d->ruhe : platz;
}

// Figur des Ziel-Rigs: gewaehltes Rig, sonst die Figur in der Szene.
std::string ZielFigur(const Fenster& f) {
    if (f.rigWahl >= 0 && static_cast<size_t>(f.rigWahl) < f.rigs.size()) return f.rigs[static_cast<size_t>(f.rigWahl)].figur;
    return f.b.figurSchluessel;
}

void Laden(Fenster& f) {
    AnimDaten* d = g_daten;
    if (d == nullptr || d->zustand != 2) return;
    const LRESULT sel = SendDlgItemMessageW(f.h, IDC_A_LISTE, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || static_cast<size_t>(sel) >= f.sichtbar.size()) { Status(f, L"Select a clip first."); return; }
    const fbanim::ClipEintrag& ce = d->quelle.Clips()[f.sichtbar[static_cast<size_t>(sel)]];
    if (!Ladbar(ce)) { Status(f, L"Curve clips cannot be loaded yet."); return; }
    fbanim::Clip clip;
    std::string fehler;
    if (!d->quelle.Entpacke(ce, clip, fehler)) { Status(f, fbdatei::Breit("Could not unpack: " + fehler), true); return; }
    Protokoll(f, "ANIM " + (ce.anzeige.empty() ? ce.name : ce.anzeige) + " (" + ce.name + ", " + ce.klasse + ", Bank " +
                     d->quelle.BankName(ce.bank) + ")");
    double gier = 0.0;
    if (fbanim::NormiereLaufrichtung(clip, gier))            // 1.42.0
        Protokoll(f, "ANIM Laufrichtung: AITrajectory stand konstant auf " + std::to_string(static_cast<int>(std::lround(gier))) +
                     " Grad - fuer die Anzeige auf die Grundrichtung -90 gesetzt");
    std::wstring bericht;
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const bool ok = (f.b.wendeAn != nullptr) && f.b.wendeAn(clip, ce.fps, RuheFuer(f, ZielFigur(f)), RigHandle(f), bericht);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    Status(f, bericht, !ok);
    Protokoll(f, fbdatei::Utf8(bericht));
}

void AlleLaden(Fenster& f) {
    AnimDaten* d = g_daten;
    if (d == nullptr || d->zustand != 2 || f.b.wendeAnFolge == nullptr) return;
    const std::vector<size_t> wahl = Treffer(f, true);
    if (wahl.empty()) { Status(f, L"No loadable clips in the current selection."); return; }
    BOOL ok = FALSE;
    int abstand = static_cast<int>(GetDlgItemInt(f.h, IDC_A_ABSTAND, &ok, FALSE));
    if (!ok || abstand < 0) abstand = 10;
    abstand = std::min(abstand, 1000);
    if (wahl.size() > 50) {
        wchar_t frage[400];
        const std::wstring rigName = f.rigWahl >= 0 ? fbdatei::Breit(f.rigs[static_cast<size_t>(f.rigWahl)].name) : std::wstring(L"the skeleton");
        swprintf(frage, 400, L"Load %zu clips one after another onto %ls (gap %d frames)?\n\n%ls", wahl.size(), rigName.c_str(), abstand,
                 f.sequenzen.empty() ? L"The timeline starts at frame 0 with the bind pose."
                                     : L"They are appended after the sequences already in the timeline.");
        if (MessageBoxW(f.h, frage, L"SWBF2 Animations", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<fbanim::Clip> clips;
    std::vector<std::string> namen;
    std::vector<float> fps;
    clips.reserve(wahl.size());
    size_t fehler = 0, richtung = 0;
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    for (size_t n = 0; n < wahl.size(); ++n) {
        if (n % 8 == 0) Status(f, L"Unpacking " + std::to_wstring(n + 1) + L" of " + std::to_wstring(wahl.size()) + L" ...");
        const fbanim::ClipEintrag& ce = d->quelle.Clips()[wahl[n]];
        fbanim::Clip c;
        std::string f2;
        if (!d->quelle.Entpacke(ce, c, f2)) {
            if (fehler < 12) Protokoll(f, "ANIM nicht entpackbar: " + (ce.anzeige.empty() ? ce.name : ce.anzeige) + " (" + ce.klasse + "): " + f2);   // 0.68.0
            ++fehler;
            continue;
        }
        namen.push_back(ce.anzeige.empty() ? ce.name : ce.anzeige);
        fps.push_back(ce.fps);
        double gier = 0.0;                                   // 1.42.0: Laufrichtung
        if (fbanim::NormiereLaufrichtung(c, gier)) {
            if (richtung < 8) Protokoll(f, "ANIM Laufrichtung: " + namen.back() + " - AITrajectory " + std::to_string(static_cast<int>(std::lround(gier))) +
                                           " Grad -> Grundrichtung -90");
            ++richtung;
        }
        clips.push_back(std::move(c));
    }
    if (richtung > 0) Protokoll(f, "ANIM Laufrichtung: in " + std::to_string(richtung) + " Clips auf die Grundrichtung gesetzt");
    const double entpackt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    Status(f, L"Setting keys ...");
    char z[200];
    std::snprintf(z, sizeof z, "ANIM Folge: %zu Clips in %.2f s entpackt, %zu nicht entpackbar, Abstand %d, %s", clips.size(), entpackt,
                  fehler, abstand, f.notiz ? "mit Notizspur" : "ohne Notizspur");
    Protokoll(f, z);
    std::wstring bericht;
    std::vector<Sequenz> plan;
    const bool gut = f.b.wendeAnFolge(clips, namen, fps, abstand, f.notiz, RuheFuer(f, ZielFigur(f)), RigHandle(f), plan, bericht);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    if (fehler) bericht += L"; " + std::to_wstring(fehler) + L" could not be unpacked";
    Status(f, bericht, !gut);
    Protokoll(f, fbdatei::Utf8(bericht));
    if (gut) {
        f.sequenzen = plan;
        FuelleSequenzen(f);
    }
}

// ---- Zeichnen -------------------------------------------------------
struct Spalten { int name, codec, bilder, fps, bank, ende; };

Spalten SpaltenFuer(const RECT& r, int rand) {
    const int b = static_cast<int>(r.right - r.left) - 2 * rand;
    Spalten s{};
    s.name = r.left + rand;
    s.bank = r.left + rand + b * 64 / 100;
    s.fps = s.bank - b * 7 / 100;
    s.bilder = s.fps - b * 9 / 100;
    s.codec = s.bilder - b * 9 / 100;
    s.ende = r.right - rand;
    return s;
}

void Text(HDC dc, const std::wstring& t, int links, int rechts, const RECT& r, UINT ausrichtung) {
    RECT z = { links, r.top, rechts, r.bottom };
    DrawTextW(dc, t.c_str(), -1, &z, ausrichtung | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void ZeichneKopf(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    FillRect(dc, &r, f.pal.pinselGrund);
    const int rand = std::max(4, f.zeilenHoehe / 3);
    const Spalten s = SpaltenFuer(r, rand);
    HGDIOBJ alt = SelectObject(dc, f.fontFett != nullptr ? f.fontFett : f.fontNormal);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, f.pal.dim);
    Text(dc, L"Name", s.name, s.codec - rand, r, DT_LEFT);
    Text(dc, L"Codec", s.codec, s.bilder - rand, r, DT_LEFT);
    Text(dc, L"Frames", s.bilder, s.fps - rand, r, DT_RIGHT);
    Text(dc, L"FPS", s.fps, s.bank - rand, r, DT_RIGHT);
    Text(dc, L"Bank", s.bank, s.ende, r, DT_LEFT);
    SelectObject(dc, alt);
}

void ZeichneZeile(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    const bool sel = (d.itemState & ODS_SELECTED) != 0;
    HBRUSH hg = CreateSolidBrush(sel ? f.pal.auswahl : f.pal.feld);
    FillRect(dc, &r, hg);
    DeleteObject(hg);
    if (d.itemID == static_cast<UINT>(-1) || d.itemID >= f.sichtbar.size() || g_daten == nullptr) return;
    const fbanim::ClipEintrag& c = g_daten->quelle.Clips()[f.sichtbar[d.itemID]];
    const bool ladbar = Ladbar(c);
    const int rand = std::max(4, f.zeilenHoehe / 3);
    const Spalten s = SpaltenFuer(r, rand);
    SetBkMode(dc, TRANSPARENT);
    const COLORREF haupt = sel ? f.pal.auswahlText : f.pal.feldText;
    const COLORREF neben = sel ? f.pal.auswahlDim : f.pal.feldDim;
    HGDIOBJ alt = SelectObject(dc, f.fontNormal);
    SetTextColor(dc, ladbar ? haupt : neben);
    Text(dc, fbdatei::Breit(c.anzeige.empty() ? c.name : c.anzeige), s.name, s.codec - rand, r, DT_LEFT);
    SetTextColor(dc, neben);
    std::string codec = c.codec;
    while (!codec.empty() && codec.back() == ' ') codec.pop_back();
    Text(dc, fbdatei::Breit(codec), s.codec, s.bilder - rand, r, DT_LEFT);
    Text(dc, std::to_wstring(c.endFrame + 1), s.bilder, s.fps - rand, r, DT_RIGHT);
    Text(dc, std::to_wstring(c.fps > 0.5f ? static_cast<int>(c.fps + 0.5f) : 30), s.fps, s.bank - rand, r, DT_RIGHT);
    const std::string& bank = g_daten->quelle.BankName(c.bank);
    std::string bankText = bank.substr(bank.rfind('/') == std::string::npos ? 0 : bank.rfind('/') + 1);
    const size_t idx = f.sichtbar[d.itemID];
    if (idx < g_daten->kopien.size() && g_daten->kopien[idx] > 1) bankText += " (+" + std::to_string(g_daten->kopien[idx] - 1) + ")";
    Text(dc, fbdatei::Breit(bankText), s.bank, s.ende, r, DT_LEFT);
    SelectObject(dc, alt);
    if (!sel) {
        HPEN p = CreatePen(PS_SOLID, 1, Mische(f.pal.feld, f.pal.feldText, 0.08));
        HGDIOBJ a = SelectObject(dc, p);
        MoveToEx(dc, r.left + rand, r.bottom - 1, nullptr);
        LineTo(dc, r.right - rand, r.bottom - 1);
        SelectObject(dc, a);
        DeleteObject(p);
    }
}

void ZeichneStatus(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    FillRect(dc, &r, f.pal.pinselGrund);
    const bool laeuft = g_daten != nullptr && g_daten->zustand == 1;
    const int balken = std::max(3, static_cast<int>(r.bottom - r.top) / 6);
    RECT t = r;
    if (laeuft) t.bottom -= balken + 2;
    HGDIOBJ alt = SelectObject(dc, f.fontNormal);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, f.statusFehler ? f.pal.fehler : f.pal.text);
    DrawTextW(dc, f.status.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, alt);
    if (!laeuft) return;
    const RECT spur = { r.left, r.bottom - balken, r.right, r.bottom };
    HBRUSH ps = CreateSolidBrush(f.pal.knopf);
    FillRect(dc, &spur, ps);
    DeleteObject(ps);
    const int breite = static_cast<int>(spur.right - spur.left);
    const int block = std::max(20, breite / 5);
    const int pos = static_cast<int>((GetTickCount() / 4) % static_cast<DWORD>(breite + block)) - block;
    RECT fuell = spur;
    fuell.left = std::max(spur.left, static_cast<LONG>(spur.left + pos));
    fuell.right = std::min(spur.right, static_cast<LONG>(spur.left + pos + block));
    if (fuell.right > fuell.left) {
        HBRUSH pf = CreateSolidBrush(f.pal.auswahl);
        FillRect(dc, &fuell, pf);
        DeleteObject(pf);
    }
}

void ZeichneAuswahlfeld(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    const bool sel = (d.itemState & ODS_SELECTED) != 0 && (d.itemState & ODS_COMBOBOXEDIT) == 0;
    const bool aus = (d.itemState & ODS_DISABLED) != 0;
    HBRUSH hg = CreateSolidBrush(sel ? f.pal.auswahl : f.pal.feld);
    FillRect(dc, &r, hg);
    DeleteObject(hg);
    if (d.itemID == static_cast<UINT>(-1)) return;
    wchar_t text[400] = {};
    const LRESULT len = SendMessageW(d.hwndItem, CB_GETLBTEXTLEN, d.itemID, 0);
    if (len > 0 && len < 399) SendMessageW(d.hwndItem, CB_GETLBTEXT, d.itemID, reinterpret_cast<LPARAM>(text));
    HGDIOBJ alt = SelectObject(dc, f.fontNormal);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, aus ? f.pal.feldDim : (sel ? f.pal.auswahlText : f.pal.feldText));
    RECT t = r;
    InflateRect(&t, -4, 0);
    DrawTextW(dc, text, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, alt);
}

INT_PTR CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Fenster* f = reinterpret_cast<Fenster*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG: {
        f = reinterpret_cast<Fenster*>(lp);
        f->h = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(f));
        f->pal.Baue(f->b.farbe);
        f->fontNormal = reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0));
        LOGFONTW lf{};
        if (f->fontNormal != nullptr && GetObjectW(f->fontNormal, sizeof(lf), &lf) == sizeof(lf)) {
            lf.lfWeight = FW_SEMIBOLD;
            f->fontFett = CreateFontIndirectW(&lf);
            f->zeilenHoehe = std::max(12, static_cast<int>(std::abs(lf.lfHeight)) + 4);
        }
        swbf2ui::DunkleTitelleiste(h, f->pal.dunkel);
        if (f->pal.dunkel) {
            // Dunkle Bildlaufleisten und Auswahlfelder (Windows 10 1809+; aeltere
            // Staende ignorieren den Namen einfach).
            SetWindowTheme(GetDlgItem(h, IDC_A_LISTE), L"DarkMode_Explorer", nullptr);
            SetWindowTheme(GetDlgItem(h, IDC_A_FIGUR), L"DarkMode_CFD", nullptr);
            SetWindowTheme(GetDlgItem(h, IDC_A_SEQUENZ), L"DarkMode_CFD", nullptr);
            SetWindowTheme(GetDlgItem(h, IDC_A_RIG), L"DarkMode_CFD", nullptr);
        }
        SendDlgItemMessageW(h, IDC_A_LISTE, LB_SETITEMHEIGHT, 0, f->zeilenHoehe + 6);
        SetWindowTextW(h, (L"SWBF2 Animations " + f->b.version).c_str());
        SetDlgItemInt(h, IDC_A_ABSTAND, 10, FALSE);
        SendDlgItemMessageW(h, IDC_A_FIGUR, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(reading ...)"));
        SendDlgItemMessageW(h, IDC_A_FIGUR, CB_SETCURSEL, 0, 0);
        if (f->b.liesSequenzen != nullptr) f->b.liesSequenzen(f->sequenzen);
        FuelleSequenzen(*f);
        FuelleRigs(*f, false);
        if (g_daten == nullptr) {
            g_daten = new AnimDaten();
            std::vector<wchar_t> p(1024);
            const std::wstring ini = f->b.ablage + L"\\einstellungen.ini";
            GetPrivateProfileStringW(L"Fenster", L"Spielordner", L"", p.data(), static_cast<DWORD>(p.size()), ini.c_str());
            const std::wstring ordner = p.data();
            if (ordner.empty()) {
                g_daten->fehler = "No game folder saved yet - open the character window once and pick the game folder.";
                g_daten->zustand = 3;
            } else {
                g_daten->zustand = 1;
                g_daten->szeneFigur = f->b.figurSchluessel;      // 1.27.0: fuer Fahrzeuge vorrechnen
                g_start = std::chrono::steady_clock::now();
                std::thread(Lies, g_daten, ordner, f->b.ablage).detach();
            }
        }
        SetTimer(h, 1, 80, nullptr);
        return TRUE;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* m = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        const int zh = (f != nullptr) ? f->zeilenHoehe : 16;
        m->itemHeight = static_cast<UINT>(m->CtlType == ODT_COMBOBOX ? zh + 4 : zh + 6);
        return TRUE;
    }
    case WM_DRAWITEM: {
        if (f == nullptr) break;
        const DRAWITEMSTRUCT* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        switch (d->CtlID) {
        case IDC_A_LISTE: ZeichneZeile(*f, *d); return TRUE;
        case IDC_A_KOPF: ZeichneKopf(*f, *d); return TRUE;
        case IDC_A_STATUS: ZeichneStatus(*f, *d); return TRUE;
        case IDC_A_FIGUR:
        case IDC_A_RIG:
        case IDC_A_SEQUENZ: ZeichneAuswahlfeld(*f, *d); return TRUE;
        case IDC_A_NUR: swbf2ui::ZeichneKnopf(f->pal, f->fontNormal, *d, f->nurLadbar); return TRUE;
        case IDC_A_NOTIZ: swbf2ui::ZeichneKnopf(f->pal, f->fontNormal, *d, f->notiz); return TRUE;
        case IDC_A_LADEN: swbf2ui::ZeichneKnopf(f->pal, f->fontNormal, *d, true); return TRUE;
        default: swbf2ui::ZeichneKnopf(f->pal, f->fontNormal, *d, false); return TRUE;
        }
    }
    case WM_TIMER: {
        if (f == nullptr || g_daten == nullptr) return TRUE;
        const int z = g_daten->zustand;
        if (z == 1) {
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
            wchar_t t[160];
            swprintf(t, 160, L"Reading animation banks ... %.0f s (only the first time; later it comes from the cache)", s);
            Status(*f, t);
        } else if (z == 2 && !f->gefuellt) {
            KillTimer(h, 1);
            char zl[260];
            std::snprintf(zl, sizeof zl, "ANIM Verzeichnis: %zu Clips, %zu Figuren mit ladbaren Clips, Skelett %s mit %zu Bones, %.1f s%s%s",
                          g_daten->quelle.Clips().size(), g_daten->figuren.size(), g_daten->skelett.c_str(), g_daten->ruhe.size(),
                          g_daten->sekunden, g_daten->ausAblage ? " (aus der Ablage)" : ", neu gelesen: ", g_daten->ausAblage ? "" : g_daten->ablageGrund.c_str());
            Protokoll(*f, zl);
            FuelleFiguren(*f);
            Fuelle(*f);
        } else if (z == 3) {
            KillTimer(h, 1);
            Status(*f, fbdatei::Breit(g_daten->fehler), true);
            Protokoll(*f, "ANIM FEHLER: " + g_daten->fehler);
            g_daten = nullptr;               // beim naechsten Oeffnen neu versuchen
        }
        return TRUE;
    }
    case WM_COMMAND: {
        if (f == nullptr) break;
        const bool bereit = g_daten != nullptr && g_daten->zustand == 2;
        switch (LOWORD(wp)) {
        case IDC_A_SUCHE:
            if (HIWORD(wp) == EN_CHANGE && bereit) Fuelle(*f);
            return TRUE;
        case IDC_A_FIGUR:
            if (HIWORD(wp) == CBN_SELCHANGE && bereit) {
                f->figur = static_cast<int>(SendDlgItemMessageW(h, IDC_A_FIGUR, CB_GETCURSEL, 0, 0)) - 1;
                Fuelle(*f);
            }
            return TRUE;
        case IDC_A_SEQUENZ:
            if (HIWORD(wp) == CBN_SELCHANGE) ZeigeSequenz(*f);
            return TRUE;
        case IDC_A_RIG:
            if (HIWORD(wp) == CBN_DROPDOWN) FuelleRigs(*f, false);        // Szene kann sich geaendert haben
            if (HIWORD(wp) == CBN_SELCHANGE) {
                f->rigWahl = static_cast<int>(SendDlgItemMessageW(h, IDC_A_RIG, CB_GETCURSEL, 0, 0));
                if (f->rigWahl >= static_cast<int>(f->rigs.size())) f->rigWahl = -1;
                // Figur des Rigs vorwaehlen (bleibt frei aenderbar).
                if (bereit && f->rigWahl >= 0) {
                    const std::string& fig = f->rigs[static_cast<size_t>(f->rigWahl)].figur;
                    const int i = FigurIndexFuer(fig);                  // 0.66.0: auch Standardklassen
                    if (i >= 0) {
                        SendDlgItemMessageW(h, IDC_A_FIGUR, CB_SETCURSEL, static_cast<WPARAM>(i + 1), 0);
                        f->figur = i;
                        Fuelle(*f);
                    }
                }
            }
            return TRUE;
        case IDC_A_NUR:
            f->nurLadbar = !f->nurLadbar;
            InvalidateRect(GetDlgItem(h, IDC_A_NUR), nullptr, TRUE);
            if (bereit) Fuelle(*f);
            return TRUE;
        case IDC_A_NOTIZ:
            f->notiz = !f->notiz;
            InvalidateRect(GetDlgItem(h, IDC_A_NOTIZ), nullptr, TRUE);
            return TRUE;
        case IDC_A_LISTE:
            if (HIWORD(wp) == LBN_DBLCLK) Laden(*f);
            return TRUE;
        case IDC_A_LADEN: Laden(*f); return TRUE;
        case IDC_A_ALLE: AlleLaden(*f); return TRUE;
        case IDCANCEL:
            KillTimer(h, 1);
            EndDialog(h, 1);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (f != nullptr && f->pal.pinselGrund != nullptr) {
            SetTextColor(reinterpret_cast<HDC>(wp), f->pal.text);
            SetBkColor(reinterpret_cast<HDC>(wp), f->pal.grund);
            return reinterpret_cast<INT_PTR>(f->pal.pinselGrund);
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (f != nullptr && f->pal.pinselFeld != nullptr) {
            SetTextColor(reinterpret_cast<HDC>(wp), f->pal.feldText);
            SetBkColor(reinterpret_cast<HDC>(wp), f->pal.feld);
            return reinterpret_cast<INT_PTR>(f->pal.pinselFeld);
        }
        break;
    case WM_PAINT: {
        if (f == nullptr) break;
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        for (int id : { IDC_A_SUCHE, IDC_A_ABSTAND, IDC_A_LISTE }) swbf2ui::Kante(h, dc, id, f->pal.linie);
        EndPaint(h, &ps);
        return TRUE;
    }
    case WM_DESTROY:
        if (f != nullptr) {
            if (f->fontFett != nullptr) DeleteObject(f->fontFett);
            f->pal.Frei();
        }
        break;
    }
    return FALSE;
}

} // namespace

int ZeigeAnimFenster(const AnimBruecke& b) {
    Fenster f;
    f.b = b;
    return static_cast<int>(DialogBoxParamW(static_cast<HINSTANCE>(b.hInstance), MAKEINTRESOURCEW(IDD_ANIMATIONEN),
                                            static_cast<HWND>(b.eltern), Proc, reinterpret_cast<LPARAM>(&f)));
}

} // namespace swbf2
