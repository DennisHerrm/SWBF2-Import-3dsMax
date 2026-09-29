// ============================================================
//  SWBF2 Import - Stufe 1: das Skelett
//
//  Aus einer .fbmodel werden die Bones angelegt, in Hierarchie
//  gehaengt und in ihre Ruhelage gesetzt.
//
//  Was hier bewusst NICHT passiert: es werden keine Keyframes
//  gesetzt. Der Animationsmodus bleibt aus, damit SetNodeTM
//  wirklich die Ruhelage setzt und nicht heimlich Keys anlegt.
// ============================================================
#include "swbf2import.h"
#include "fbbeipack.h"
#include "fbanim.h"
#include "fbdatei.h"
#include "swbf2import_animfenster.h"
#include "swbf2import_ablage.h"

#include <icolorman.h>
#include <iskin.h>
#include <stdmat.h>
#include <bitmap.h>
#include <istdplug.h>
#include <notetrck.h>
#include <maxscript/maxscript.h>
#include <functional>
#include <map>
#include <cstdint>
#include <unordered_map>

#include <algorithm>
#include <charconv>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <string>
#include <vector>

#ifndef BONE_OBJ_CLASSID
#define BONE_OBJ_CLASSID Class_ID(BONE_OBJ_CLASS_ID, 0)
#endif

// Die Instanz der .dlu (swbf2import_dll.cpp) - dort liegt die Dialogvorlage.
extern HINSTANCE hInstance;

namespace swbf2 {

// ------------------------------------------------------------
//  Protokoll
//
//  Landet als "Import swbf2.log" im Downloads-Ordner. Ohne das
//  ist ein Fehlimport eine reine Ratesache: man sieht ein Bild
//  und weiss nicht, welche Zahlen dahinterstehen.
// ------------------------------------------------------------
namespace {

// ------------------------------------------------------------
//  Dezimalpunkt erzwingen
//
//  3ds Max stellt die C-Laufzeit auf die Regionseinstellung des
//  Anwenders. Auf einem deutschen Windows schreibt printf("%.6f")
//  dann "1,000000" statt "1.000000" - und die Gegenprobe scheitert
//  mit "could not convert string to float: '1,000000'".
//
//  Das SDK hat dafuer MaxLocaleHandler(LC_NUMERIC, _M("C")) in
//  winutil.h, ausdruecklich fuer "locale independent data to or
//  from a file". Hier steht dasselbe zu Fuss, damit es auch gegen
//  aeltere SDK-Fassungen uebersetzt: merken, auf "C" stellen, im
//  Destruktor zuruecksetzen.
// ------------------------------------------------------------
struct PunktStattKomma {
    std::string vorher;
    PunktStattKomma() {
        const char* alt = std::setlocale(LC_NUMERIC, nullptr);
        if (alt != nullptr) vorher = alt;
        std::setlocale(LC_NUMERIC, "C");
    }
    ~PunktStattKomma() {
        if (!vorher.empty()) std::setlocale(LC_NUMERIC, vorher.c_str());
    }
};

std::FILE* g_log = nullptr;

std::wstring Protokollpfad() {
    return swbf2ablage::Protokoll();                      // 1.43.0: Testschalter, siehe swbf2import_ablage.h
}

// Geschachtelt: das Figurenfenster oeffnet das Protokoll fuer die ganze
// Sitzung, und jeder Import darin oeffnet und schliesst es noch einmal.
// Ohne Zaehler haette der erste Import die Datei geschlossen und alle
// weiteren Zeilen des Fensters waeren still verloren gegangen.
int g_logTiefe = 0;

void LogAuf() {
    ++g_logTiefe;
    if (g_log != nullptr) return;
    // 1.21.0: ANHAENGEN statt ueberschreiben. Bisher legte jede Aktion das
    // Protokoll neu an - oeffnete DH nach dem Import das Animationsfenster,
    // waren die Material- und Texturzeilen des Imports weg. Beim ersten Mal in
    // einer Max-Sitzung wird noch einmal frisch begonnen.
    static bool ersteAktion = true;
    g_log = _wfopen(Protokollpfad().c_str(), ersteAktion ? L"wb" : L"ab");
    ersteAktion = false;
}

void LogZu() {
    if (g_logTiefe > 0) --g_logTiefe;
    if (g_logTiefe == 0 && g_log != nullptr) { std::fclose(g_log); g_log = nullptr; }
}

void Log(const char* format, ...) {
    if (g_log == nullptr) return;
    char puffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(puffer, sizeof(puffer), format, args);
    va_end(args);
    std::fprintf(g_log, "%s\n", puffer);
    std::fflush(g_log);   // sofort schreiben - stuerzt Max ab, ist es sonst weg
}

// Ohne festen Puffer: Zeilen aus dem Fenster koennen lang sein (Bundlenamen,
// Skelett-Kandidaten) und wuerden in Log() bei 1023 Zeichen abgeschnitten.
void LogRoh(const char* zeile) {
    if (g_log == nullptr || zeile == nullptr) return;
    std::fputs(zeile, g_log);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

} // namespace

// ------------------------------------------------------------
//  Zeichenketten: std::string (UTF-8) nach MSTR
//
//  3ds Max ist seit 2013 durchgehend Unicode. MSTR ist dort WStr,
//  also wchar_t, und HAT KEINEN Konstruktor aus char* mehr - der
//  wurde damals abgeschafft. Genau daran ist der erste Bau
//  gescheitert (C2665 bei WStr::WStr, C2440 bei const char* nach
//  MSTR), und zwar bei jeder der zwoelf Max-Fassungen gleich.
//
//  Die Doku nennt den Ersatz beim Namen: WStr::FromACP(),
//  WStr::FromUTF8(), WStr::FromCP() oder WStr::FromMCHAR().
//  Unsere Dumps sind UTF-8, also FromUTF8. Das gibt es seit
//  Max 2013 und deckt damit 2016 bis 2027 ab - eine Weiche nach
//  Jahrgang braucht es nicht.
//
//  Wichtig ist ausserdem, das Ergebnis in einer BENANNTEN
//  Variablen zu halten. MSTR(...).data() an einem temporaeren
//  Objekt zeigt auf Speicher, den es beim naechsten Semikolon
//  nicht mehr gibt; ab Max 2025 ist die Umwandlung fuer Rvalues
//  ausserdem ausdruecklich geloescht.
// ------------------------------------------------------------
static MSTR AusUtf8(const std::string& s) {
    return MSTR::FromUTF8(s.c_str());
}

// MAXScript aus C++ (Layer, Notizspur) - definiert weiter unten.
namespace {
bool FuehreSkript(const std::wstring& skript);
std::wstring SkriptText(const std::string& s);
}

// ------------------------------------------------------------
Matrix3 AchsenMatrix() {
    // 3ds Max ist rechtshaendig mit Z nach OBEN und Y IN den Bildschirm
    // (steht so in der SDK-Doku zum Konvertierungsmanager). Das Spiel hat
    // Y oben. Die Umrechnung ist damit eine Drehung um X:
    //
    //   x -> ( 1, 0, 0)
    //   y -> ( 0, 0, 1)      Spiel-Hoehe wird Max-Hoehe
    //   z -> ( 0,-1, 0)
    //
    // Also (x, y, z) -> (x, -z, y). Genau so macht es auch der
    // Ogre-Importer fuer Max, und Ogre ist ebenfalls Y-oben:
    //   position.x = v.x;  position.y = -v.z;  position.z = v.y;
    //
    // Die Determinante ist +1, die Haendigkeit bleibt also erhalten.
    // Waere das Spiel LINKSHAENDIG (Z in den Bildschirm), muesste hier
    // gespiegelt werden - dann steht die Figur zwar aufrecht, ist aber
    // seitenverkehrt. Genau daran wird man es sehen.
    // Nicht Matrix3(TRUE): dieser Konstruktor ist ab Max 2025 als veraltet
    // gemeldet ("Matrix3 wird jetzt standardmaessig auf die Einheitsmatrix
    // gesetzt"). Bei aelteren SDKs setzt der Standardkonstruktor aber NICHTS.
    // IdentityMatrix() gilt in beiden Faellen und meldet nirgends etwas an.
    Matrix3 a;
    a.IdentityMatrix();
    a.SetRow(0, Point3(1.0f,  0.0f, 0.0f));
    a.SetRow(1, Point3(0.0f,  0.0f, 1.0f));
    a.SetRow(2, Point3(0.0f, -1.0f, 0.0f));
    a.SetRow(3, Point3(0.0f,  0.0f, 0.0f));
    return a;
}

Matrix3 RuheAlsMatrix(const float r[12]) {
    Matrix3 m;
    m.IdentityMatrix();
    m.SetRow(0, Point3(r[0], r[1],  r[2]));    // right
    m.SetRow(1, Point3(r[3], r[4],  r[5]));    // up
    m.SetRow(2, Point3(r[6], r[7],  r[8]));    // forward
    m.SetRow(3, Point3(r[9], r[10], r[11]));   // trans
    return m;
}

namespace {

// Weltmatrizen im SPIELRAUM: Kind mal Elternteil, wie in Max auch.
std::vector<Matrix3> WeltImSpielraum(const fb::Model& modell) {
    std::vector<Matrix3> welt(modell.bones.size());
    for (size_t i = 0; i < modell.bones.size(); ++i) {
        const fb::Bone& b = modell.bones[i];
        const Matrix3 lokal = RuheAlsMatrix(b.rest);
        if (b.parent >= 0 && static_cast<size_t>(b.parent) < i) {
            welt[i] = lokal * welt[static_cast<size_t>(b.parent)];
        } else {
            welt[i] = lokal;   // Wurzel, oder Elternteil steht spaeter
        }
    }
    return welt;
}

// Nur der aktuelle Zustand, sauber wiederhergestellt.
struct AnimationAus {
    BOOL vorher;
    AnimationAus() : vorher(Animating()) {
        SuspendAnimate();
        AnimateOff();
    }
    ~AnimationAus() {
        ResumeAnimate();
        if (vorher) AnimateOn();
    }
};

std::vector<INode*> g_letzteBones;   // fuer den Bone-Dump
std::vector<INode*> g_boneKnoten;    // je Skelettindex, nullptr wo keiner entstand (Skinning)
std::vector<Matrix3> g_gesetzteTM;  // was wir gesetzt haben

void node_setze_welt(INode* n, const Matrix3& m) {
    Matrix3 kopie = m;                 // SetNodeTM nimmt eine Referenz
    n->SetNodeTM(0, kopie);
}

} // namespace

// ------------------------------------------------------------
//  Massstab
//
//  Der Dump steht in METERN. Was ein Meter in Max ist, haengt an
//  der Systemeinheit des Anwenders - die kann Zoll, Zentimeter
//  oder Meter sein. Die Abfrage liefert laut SDK-Doku "die Zahl
//  der Meter je Systemeinheit". Um von Metern in Systemeinheiten
//  zu kommen, wird also GETEILT.
//
//  Ohne das war die Figur 1,8 Einheiten gross. Max legt Bones mit
//  Breite 4 an - jeder einzelne Knochen war damit doppelt so
//  breit wie die ganze Figur. Genau das war der Kastenhaufen.
//
//  ZWEI NAMEN FUER DIESELBE SACHE:
//  GetMasterScale gibt es bis Max 2021. In 2022 wurde es fuer
//  veraltet erklaert ("please use GetSystemUnitScale instead"),
//  ab 2023 ist es GANZ WEG - daher C3861, Bezeichner nicht
//  gefunden, und zwar genau bei 2023 bis 2027, waehrend 2016 bis
//  2022 sauber durchliefen. Die Bedeutung ist unveraendert:
//  GetSystemUnitScale(UNITS_INCHES) liefert die Zahl der Zoll je
//  Einheit. MAX_RELEASE 24000 ist Max 2022.
// ------------------------------------------------------------
// Einen Wert aus einer INI lesen, ohne Windows-Profil-API (0.48.1). Das
// Figurenfenster legt die Datei als UTF-16LE mit BOM an (dann speichert
// WritePrivateProfileStringW Unicode); ANSI/UTF-8 geht ebenso. Abschnitt und
// Schluessel ohne Ruecksicht auf Gross-/Kleinschreibung, wie bei Windows.
bool LiesIniWert(const std::wstring& pfad, const std::string& abschnitt, const std::string& schluessel, std::string& wert) {
    std::vector<uint8_t> roh;
    std::string fehler;
    if (!fbdatei::LiesAlles(fbdatei::Utf8(pfad), roh, fehler)) return false;
    std::string text;
    if (roh.size() >= 2 && roh[0] == 0xFF && roh[1] == 0xFE) {
        std::wstring w;
        for (size_t i = 2; i + 1 < roh.size(); i += 2) w += static_cast<wchar_t>(roh[i] | (roh[i + 1] << 8));
        text = fbdatei::Utf8(w);
    } else {
        const size_t s0 = (roh.size() >= 3 && roh[0] == 0xEF && roh[1] == 0xBB && roh[2] == 0xBF) ? 3 : 0;
        text.assign(roh.begin() + static_cast<long>(s0), roh.end());
    }
    auto klein = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return x; };
    auto trim = [](std::string x) {
        const size_t a = x.find_first_not_of(" \t\r");
        if (a == std::string::npos) return std::string();
        const size_t b = x.find_last_not_of(" \t\r");
        return x.substr(a, b - a + 1);
    };
    const std::string ab = klein(abschnitt), sl = klein(schluessel);
    std::string aktuell;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t ende = text.find('\n', pos);
        if (ende == std::string::npos) ende = text.size();
        const std::string z = trim(text.substr(pos, ende - pos));
        pos = ende + 1;
        if (z.empty() || z[0] == ';') continue;
        if (z[0] == '[') { const size_t e = z.find(']'); aktuell = klein(trim(z.substr(1, e == std::string::npos ? std::string::npos : e - 1))); continue; }
        if (aktuell != ab) continue;
        const size_t gl = z.find('=');
        if (gl == std::string::npos || klein(trim(z.substr(0, gl))) != sl) continue;
        wert = trim(z.substr(gl + 1));
        return true;
    }
    return false;
}

// FESTER MASSSTAB (0.47.0, Wunsch DH): die Figur soll in jeder Szene gleich
// gross sein, egal ob die Systemeinheit Zoll oder Meter ist. Bis 0.46.0 wurde
// massstabsgetreu umgerechnet - in einer Meter-Szene war Anakin 1,8 Einheiten
// gross und neben dem 10er-Raster kaum zu sehen (Max 2025 bei DH). Laut
// Autodesk-Hilfe ist die Standard-Systemeinheit 1 Zoll; genau so viele
// Einheiten bekommt die Figur jetzt immer: 1 m = 39,37 Einheiten. In einer
// Zoll-Szene ist das zugleich die echte Groesse. Anders einstellbar in
// %LOCALAPPDATA%\SWBF2Import\einstellungen.ini, [Import] EinheitenJeMeter=
// (z. B. 100 fuer "wie Zentimeter"); 0 = wieder massstabsgetreu.
float Massstab() {
    double jeMeter = 39.37007874015748;
    {
        // Die INI wird SELBST gelesen (0.48.1): ab Max 2019 steht im SDK eine
        // gleichnamige GetPrivateProfileStringW, die per using im globalen
        // Namensraum landet - der Aufruf war in 0.47.0 und 0.48.0 in neun
        // Jahrgaengen mehrdeutig, auch mit ::. Eigener Leser = keine Abhaengigkeit.
        const std::wstring ini = swbf2ablage::Ordner() + L"\\einstellungen.ini";   // 1.43.0
        std::string t;
        if (LiesIniWert(ini, "Import", "EinheitenJeMeter", t) && !t.empty()) {
            double w = 0.0;
            const auto r = std::from_chars(t.data(), t.data() + t.size(), w);
            if (r.ec == std::errc() && w >= 0.0 && w < 1e6) jeMeter = w;
        }
    }
    if (jeMeter > 0.0) return static_cast<float>(jeMeter);
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    const double m = GetSystemUnitScale(UNITS_METERS);
#else
    const double m = GetMasterScale(UNITS_METERS);
#endif
    if (m <= 0.0) return 1.0f;          // ungueltige Angabe: nichts tun
    return static_cast<float>(1.0 / m);
}

// Nur fuers Protokoll - dieselbe Zahl, ohne den Kehrwert.
double SystemeinheitInMetern() {
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    return GetSystemUnitScale(UNITS_METERS);
#else
    return GetMasterScale(UNITS_METERS);
#endif
}

int BaueSkelett(Interface* ip, const fb::Model& modell, std::wstring& bericht) {
    if (ip == nullptr || modell.bones.empty()) {
        bericht = L"The file contains no skeleton.";
        Log("ABBRUCH: kein Skelett in der Datei");
        return 0;
    }

    const std::vector<Matrix3> welt = WeltImSpielraum(modell);
    const Matrix3 achsen = AchsenMatrix();
    const float mass = Massstab();

    Log("Systemeinheit: %.6f Meter je Einheit -> Massstab %.4f",
        SystemeinheitInMetern(), mass);

    // Ausdehnung des Rigs, um daraus die Bonebreite abzuleiten.
    float lo[3] = { 1e30f, 1e30f, 1e30f };
    float hi[3] = { -1e30f, -1e30f, -1e30f };
    for (const Matrix3& w : welt) {
        const Point3 t = w.GetRow(3);
        const float v[3] = { t.x, t.y, t.z };
        for (int k = 0; k < 3; ++k) {
            if (v[k] < lo[k]) lo[k] = v[k];
            if (v[k] > hi[k]) hi[k] = v[k];
        }
    }
    float groesse = 0.0f;
    for (int k = 0; k < 3; ++k) {
        const float d = hi[k] - lo[k];
        if (d > groesse) groesse = d;
    }
    Log("Rig im Spielraum: %.3f x %.3f x %.3f m", hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2]);
    Log("Rig in Einheiten: %.3f x %.3f x %.3f", (hi[0]-lo[0])*mass, (hi[1]-lo[1])*mass, (hi[2]-lo[2])*mass);

    // Bonebreite: ein Viertel der MITTLEREN Bonelaenge, nicht ein
    // Anteil der Gesamtausdehnung. Grund: einzelne Bones sitzen
    // meterweit von ihrem Elternteil entfernt - bei Anakin ist
    // Wep_Aim_Target_Rig 3,4 m und Camera3pDefPos_Rig 2,5 m weg.
    // Die wuerden das Mass verzerren. Der Median liegt bei rund
    // 8 cm und beschreibt das Rig richtig.
    std::vector<float> laengen;
    laengen.reserve(modell.bones.size());
    for (const fb::Bone& b : modell.bones) {
        if (b.parent < 0) continue;
        const float d = std::sqrt(b.rest[9] * b.rest[9] +
                                  b.rest[10] * b.rest[10] +
                                  b.rest[11] * b.rest[11]);
        if (d > 1e-5f) laengen.push_back(d);
    }
    float mittel = 0.08f;                       // Rueckfall, falls nichts da
    if (!laengen.empty()) {
        std::sort(laengen.begin(), laengen.end());
        mittel = laengen[laengen.size() / 2];
    }
    // Breite und Hoehe der Bone-Objekte: 0 (Wunsch DH, 0.43.0) - die Bones
    // erscheinen als duenne Linien und verdecken das Gesicht nicht mehr.
    const float boneBreite = 0.0f;
    Log("Mittlere Bonelaenge: %.4f m (%zu gemessen)", mittel, laengen.size());
    Log("Bonebreite und -hoehe: 0 (frueher ein Viertel der mittleren Bonelaenge)");

    AnimationAus keineKeys;

    std::vector<INode*> knoten(modell.bones.size(), nullptr);
    g_letzteBones.clear();
    g_gesetzteTM.clear();
    g_gesetzteTM.resize(modell.bones.size());
    int alsHelfer = 0;

    // 1. Alle Knoten anlegen und in ihre Weltlage setzen.
    for (size_t i = 0; i < modell.bones.size(); ++i) {
        Object* obj = static_cast<Object*>(
            ip->CreateInstance(GEOMOBJECT_CLASS_ID, BONE_OBJ_CLASSID));
        bool istBone = (obj != nullptr);

        // Rueckfallebene: ein Point-Helper. Der braucht keine
        // Parameterblock-Kenntnis, sieht mit ShowBone wie ein Bone
        // aus, und der Skin-Modifier nimmt ihn spaeter genauso.
        if (obj == nullptr) {
            obj = static_cast<Object*>(
                ip->CreateInstance(HELPER_CLASS_ID, Class_ID(POINTHELP_CLASS_ID, 0)));
            if (obj != nullptr) ++alsHelfer;
        }
        if (obj == nullptr) continue;

        INode* node = ip->CreateObjectNode(obj);
        if (node == nullptr) continue;

        MSTR name = AusUtf8(modell.bones[i].name);
        node->SetName(name.data());

        // Achsen umrechnen, DANN die Verschiebung in Einheiten
        // umrechnen. Die Basisvektoren bleiben unangetastet -
        // sonst waeren die Bones mitskaliert.
        Matrix3 tm = welt[i] * achsen;
        const Point3 t = tm.GetRow(3);
        tm.SetRow(3, Point3(t.x * mass, t.y * mass, t.z * mass));
        g_gesetzteTM[i] = tm;          // gesetzt wird erst nach dem Einhaengen

        node->ShowBone(1);
        node->SetBoneNodeOnOff(TRUE, 0);

        // ------------------------------------------------------------
        //  Selbstausrichtung AUS.
        //
        //  Die Doku zu den Bone-Eigenschaften sagt es woertlich: mit
        //  eingeschalteter Selbstausrichtung "rotates the node such that
        //  its X axis points at the average position of all children",
        //  und "the translation of a child bone will be converted into
        //  rotation of the parent". Max dreht also beim Einhaengen an
        //  den Knochen herum.
        //
        //  Wir setzen die Weltlage aber selbst und wollen sie GENAU so
        //  behalten. Genau deshalb wich Wep2_Root um 0,0096 in der
        //  Drehung ab, waehrend die Lage aller 248 Bones auf 0,000017
        //  stimmte.
        //
        //  Die Laenge einfrieren gehoert dazu: sonst rechnet Max die
        //  Entfernung zum Kind in eine Streckung um.
        // ------------------------------------------------------------
        node->SetBoneAutoAlign(FALSE);
        node->SetBoneFreezeLen(TRUE);

        node->SetRenderable(FALSE);

        // Breite und Hoehe des Bone-Objekts.
        //
        // GetParamBlockByID, NICHT GetParamBlock(0): Object erbt von
        // BaseObject ein parameterloses GetParamBlock(), das die alte
        // Parameterblockfassung liefert und die gewuenschte
        // Ueberladung verdeckt. GetParamBlockByID kommt direkt von
        // Animatable und wird nicht verdeckt.
        // boneobj_width und boneobj_height sind die ersten beiden
        // Eintraege des Blocks.
        if (istBone) {
            Object* bobj = node->GetObjectRef();
            if (bobj != nullptr) bobj = bobj->FindBaseObject();
            if (bobj != nullptr) {
                IParamBlock2* bpb = bobj->GetParamBlockByID(0);
                if (bpb == nullptr) bpb = bobj->Animatable::GetParamBlock(0);
                if (bpb != nullptr) {
                    bpb->SetValue(0, 0, boneBreite);
                    bpb->SetValue(1, 0, boneBreite);
                }
            }
        }

        knoten[i] = node;
    }

    // 2. Einhaengen - noch OHNE Weltlage, die Knoten stehen alle im
    //    Ursprung. keepTM spielt deshalb keine Rolle.
    for (size_t i = 0; i < modell.bones.size(); ++i) {
        const int e = modell.bones[i].parent;
        if (knoten[i] == nullptr) continue;
        if (e >= 0 && static_cast<size_t>(e) < knoten.size() && knoten[e] != nullptr) {
            knoten[e]->AttachChild(knoten[i], 0);
        } else {
            // Oberster Bone: die Figur vermerken, damit das Animationsfenster
            // weiss, WELCHE Figur in der Szene steht (0.38.1). Steht in den
            // Objekteigenschaften unter "Benutzerdefiniert" und bleibt mit der
            // Szene gespeichert.
            knoten[i]->SetUserPropString(MSTR(_T("swbf2_figur")), AusUtf8(modell.source));
        }
    }

    // 3. ERST JETZT die Weltlage setzen, Elternteil vor Kind.
    //
    //    Vorher lief es andersherum - Lage setzen, dann einhaengen mit
    //    keepTM - und dabei hat Max bei Wep2_Root die Matrix um 0,009571
    //    veraendert. Nachgewiesen mit der SETZ-Zeile im Dump: gesetzt und
    //    zurueckgelesen waren verschieden, also war es Max und nicht
    //    unsere Rechnung. SetBoneAutoAlign(FALSE) hat daran nichts
    //    geaendert.
    //
    //    In dieser Reihenfolge gibt es nichts mehr umzurechnen: der
    //    Knoten haengt schon, SetNodeTM setzt die Weltlage, Max leitet
    //    die lokale daraus ab, und danach fasst niemand mehr an. Die
    //    Bones stehen im Dump ohnehin Elternteil vor Kind (gemessen,
    //    Elternindex immer kleiner als der eigene), ein Durchlauf reicht.
    //
    //    SOFORT ZURUECKLESEN. Die SDK-Doku sagt, SetNodeTM "sets the node's
    //    world space transformation matrix, and will call Control::SetValue()
    //    on the transform controller" - der Wert geht also IMMER durch den
    //    Transformregler, egal in welcher Reihenfolge. Deshalb wird hier
    //    direkt nach dem Setzen gelesen. Weicht es schon JETZT ab, war es der
    //    Regler. Weicht es erst am Ende ab, hat etwas Spaeteres angefasst.
    double sofortMax = 0.0;
    std::string sofortWo;
    for (size_t i = 0; i < modell.bones.size(); ++i) {
        if (knoten[i] == nullptr) continue;
        node_setze_welt(knoten[i], g_gesetzteTM[i]);

        const Matrix3 zurueck = knoten[i]->GetNodeTM(0);
        for (int r = 0; r < 4; ++r) {
            const Point3 a = zurueck.GetRow(r);
            const Point3 b = g_gesetzteTM[i].GetRow(r);
            const float d[3] = { a.x - b.x, a.y - b.y, a.z - b.z };
            for (int k = 0; k < 3; ++k) {
                const double ad = d[k] < 0 ? -d[k] : d[k];
                if (ad > sofortMax) {
                    sofortMax = ad;
                    char t[128];
                    std::snprintf(t, sizeof t, "%s Zeile %d Spalte %d",
                                  modell.bones[i].name.c_str(), r, k);
                    sofortWo = t;
                }
            }
        }
    }
    Log("Sofort zurueckgelesen: groesste Abweichung %.6f%s%s",
        sofortMax, sofortWo.empty() ? "" : " bei ", sofortWo.c_str());

    int gezaehlt = 0;
    g_boneKnoten = knoten;
    for (INode* n : knoten) {
        if (n != nullptr) { g_letzteBones.push_back(n); ++gezaehlt; }
    }
    Log("Knoten angelegt: %d von %d (%d davon als Point-Helper)",
        gezaehlt, static_cast<int>(modell.bones.size()), alsHelfer);

    MSTR skelett = modell.skeleton.empty() ? MSTR(_T("(ohne Namen)"))
                                           : AusUtf8(modell.skeleton);
    MCHAR puffer[256];
    // Einheit mit in den Bericht (0.46.0): in einer Szene mit Meter als
    // Systemeinheit ist Anakin 1,8 Einheiten gross - neben dem 10er-Raster
    // winzig, aber massstabsgetreu (DH, Max 2025).
    _sntprintf(puffer, 256,
               _T("%d of %d bones created.\nSkeleton: %s\nScale: 1 m = %.2f units, the same in every scene (system unit here: 1 unit = %g m)"),
               gezaehlt, static_cast<int>(modell.bones.size()), skelett.data(), static_cast<double>(Massstab()), SystemeinheitInMetern());
    bericht = puffer;
    return gezaehlt;
}

// ------------------------------------------------------------
//  Stufe 2: die Meshes
//
//  Je Section im Dump ein eigener Knoten. Positionen und UV
//  liegen fertig vor; umgerechnet wird nur, was auch beim
//  Skelett umgerechnet wurde: Achsen und Massstab.
//
//  WINDUNG: unsere Achsenmatrix hat Determinante +1, spiegelt
//  also nicht. Die Reihenfolge der Dreiecksecken bleibt deshalb,
//  wie sie ist. Zeigen die Normalen spaeter nach innen, wird
//  genau hier getauscht - an einer Stelle.
//
//  UV: Frostbite legt den Ursprung oben links, Max unten links.
//  Deshalb v -> 1-v. Sieht man sofort, wenn eine Textur drauf
//  ist, und bis dahin steht es hier als einzige Stelle.
// ------------------------------------------------------------
int BaueMeshes(Interface* ip, const fb::Model& modell, float mass,
               std::wstring& bericht, std::vector<INode*>& meshKnoten) {
    meshKnoten.assign(modell.meshes.size(), nullptr);
    if (ip == nullptr || modell.meshes.empty()) {
        bericht = L"The file contains no meshes.";
        Log("Keine Meshes in der Datei");
        return 0;
    }

    const Matrix3 achsen = AchsenMatrix();
    int gebaut = 0;
    size_t vertsGesamt = 0, trisGesamt = 0;

    for (size_t mi = 0; mi < modell.meshes.size(); ++mi) {
        const fb::Mesh& q = modell.meshes[mi];
        if (q.vertexCount == 0 || q.triangleCount == 0) {
            Log("Mesh %zu (%s): leer, uebersprungen", mi, q.name.c_str());
            continue;
        }
        if (!q.has(fb::STREAM_POS)) {
            Log("Mesh %zu (%s): ohne Positionen, uebersprungen", mi, q.name.c_str());
            continue;
        }

        TriObject* tri = CreateNewTriObject();
        if (tri == nullptr) {
            Log("Mesh %zu (%s): TriObject liess sich nicht anlegen", mi, q.name.c_str());
            continue;
        }
        Mesh& mesh = tri->GetMesh();

        mesh.setNumVerts(static_cast<int>(q.vertexCount));
        for (uint32_t v = 0; v < q.vertexCount; ++v) {
            // Erst in den Max-Raum drehen, dann in Einheiten umrechnen.
            const Point3 p(q.pos[v * 3 + 0], q.pos[v * 3 + 1], q.pos[v * 3 + 2]);
            const Point3 m(p.x, -p.z, p.y);
            mesh.setVert(static_cast<int>(v), m.x * mass, m.y * mass, m.z * mass);
        }

        mesh.setNumFaces(static_cast<int>(q.triangleCount));
        const int matId = (q.material >= 0) ? q.material : 0;
        for (uint32_t f = 0; f < q.triangleCount; ++f) {
            const uint32_t a = q.indices[f * 3 + 0];
            const uint32_t b = q.indices[f * 3 + 1];
            const uint32_t c = q.indices[f * 3 + 2];
            if (a >= q.vertexCount || b >= q.vertexCount || c >= q.vertexCount) {
                continue;                       // kaputtes Dreieck ueberspringen
            }
            mesh.faces[f].setVerts(static_cast<int>(a), static_cast<int>(b),
                                   static_cast<int>(c));
            mesh.faces[f].setEdgeVisFlags(1, 1, 1);
            mesh.faces[f].setSmGroup(1);
            mesh.faces[f].setMatID(static_cast<unsigned short>(matId));
        }

        // 1.27.0: Bei FAHRZEUGEN ist der ZWEITE Satz der richtige. DH hat es in
        // Max nachgestellt: mit Map-Kanal 2 sitzt die Bemalung des AT-TE
        // ueberall, mit Kanal 1 nur an den Fuessen. Also fuer Fahrzeuge uv1 in
        // den Hauptkanal und uv0 nach Kanal 2 (zum Vergleichen).
        const bool fahrzeug = modell.source.compare(0, 9, "vehicle: ") == 0;
        const std::vector<float>& uvHaupt = (fahrzeug && q.has(fb::STREAM_UV1) && !q.uv1.empty()) ? q.uv1 : q.uv0;
        const std::vector<float>& uvZweit = (fahrzeug && q.has(fb::STREAM_UV1) && !q.uv1.empty()) ? q.uv0 : q.uv1;
        if (!uvHaupt.empty()) {
            mesh.setNumTVerts(static_cast<int>(q.vertexCount));
            for (uint32_t v = 0; v < q.vertexCount; ++v) {
                mesh.setTVert(static_cast<int>(v), uvHaupt[v * 2 + 0],
                              1.0f - uvHaupt[v * 2 + 1], 0.0f);
            }
            mesh.setNumTVFaces(static_cast<int>(q.triangleCount));
            for (uint32_t f = 0; f < q.triangleCount; ++f) {
                mesh.tvFace[f].setTVerts(static_cast<int>(q.indices[f * 3 + 0]),
                                         static_cast<int>(q.indices[f * 3 + 1]),
                                         static_cast<int>(q.indices[f * 3 + 2]));
            }
        }

        // 1.26.0: Den ZWEITEN UV-Satz als Map-Kanal 2 mitgeben. DH: beim AT-TE
        // sitzen die Texturen nur an den Fuessen richtig, an Kabine und Rumpf
        // nicht. Beide Saetze sind gleich gross (UVBEREICH-Zeilen), Frostbite
        // nutzt fuer manche Teile TexCoord1. Mit Kanal 2 in der Szene laesst
        // sich das in Max umschalten und vergleichen, ohne neu zu importieren.
        if (!uvZweit.empty()) {
            mesh.setMapSupport(2, TRUE);
            mesh.setNumMapVerts(2, static_cast<int>(q.vertexCount));
            for (uint32_t v = 0; v < q.vertexCount; ++v)
                mesh.setMapVert(2, static_cast<int>(v), Point3(uvZweit[v * 2 + 0], 1.0f - uvZweit[v * 2 + 1], 0.0f));
            mesh.setNumMapFaces(2, static_cast<int>(q.triangleCount));
            for (uint32_t f = 0; f < q.triangleCount; ++f)
                mesh.mapFaces(2)[f].setTVerts(static_cast<int>(q.indices[f * 3 + 0]),
                                              static_cast<int>(q.indices[f * 3 + 1]),
                                              static_cast<int>(q.indices[f * 3 + 2]));
        }

        mesh.InvalidateTopologyCache();
        mesh.InvalidateGeomCache();
        mesh.buildNormals();

        INode* node = ip->CreateObjectNode(tri);
        if (node == nullptr) {
            Log("Mesh %zu (%s): Knoten liess sich nicht anlegen", mi, q.name.c_str());
            continue;
        }
        MSTR name = AusUtf8(q.name);
        node->SetName(name.data());
        meshKnoten[mi] = node;

        // Der Knoten steht im Ursprung - die Vertices tragen die Lage
        // schon in sich. Sonst waere alles doppelt verschoben.
        Matrix3 eins;
        eins.IdentityMatrix();
        node->SetNodeTM(0, eins);

        ++gebaut;
        vertsGesamt += q.vertexCount;
        trisGesamt += q.triangleCount;
        Log("Mesh %2zu %-40s LOD %u  %6u Vertices  %6u Dreiecke  Material %d%s",
            mi, q.name.c_str(), q.lod, q.vertexCount, q.triangleCount, q.material,
            q.has(fb::STREAM_UV0) ? "  mit UV" : "  OHNE UV");
    }

    (void)achsen;
    MCHAR puffer[256];
    _sntprintf(puffer, 256,
               _T("%d of %d meshes created.\n%d vertices, %d triangles."),
               gebaut, static_cast<int>(modell.meshes.size()),
               static_cast<int>(vertsGesamt), static_cast<int>(trisGesamt));
    bericht = puffer;
    Log("Meshes angelegt: %d von %zu, zusammen %zu Vertices und %zu Dreiecke",
        gebaut, modell.meshes.size(), vertsGesamt, trisGesamt);
    return gebaut;
}

// ------------------------------------------------------------
bool SchreibeMeshDump(const fb::Model& modell, float mass,
                      const std::wstring& zieldatei) {
    std::FILE* f = _wfopen(zieldatei.c_str(), L"wb");
    if (f == nullptr) return false;

    std::fprintf(f, "QUELLE %s\n", modell.source.c_str());
    std::fprintf(f, "MASSSTAB %.6f\n", static_cast<double>(mass));
    std::fprintf(f, "FASSUNG %ls\n", SWBF2IMPORT_VERSION_STR);
    std::fprintf(f, "MESHES %zu\n", modell.meshes.size());

    for (size_t mi = 0; mi < modell.meshes.size(); ++mi) {
        const fb::Mesh& q = modell.meshes[mi];
        float lo[3] = { 1e30f, 1e30f, 1e30f };
        float hi[3] = { -1e30f, -1e30f, -1e30f };
        for (uint32_t v = 0; v < q.vertexCount && q.pos.size() >= (v + 1) * 3; ++v) {
            const float p[3] = { q.pos[v * 3 + 0], q.pos[v * 3 + 1], q.pos[v * 3 + 2] };
            const float m[3] = { p[0] * mass, -p[2] * mass, p[1] * mass };
            for (int k = 0; k < 3; ++k) {
                if (m[k] < lo[k]) lo[k] = m[k];
                if (m[k] > hi[k]) hi[k] = m[k];
            }
        }
        if (q.vertexCount == 0) { lo[0] = lo[1] = lo[2] = hi[0] = hi[1] = hi[2] = 0.0f; }
        std::fprintf(f, "MESH %zu %s lod=%u verts=%u tris=%u material=%d",
                     mi, q.name.c_str(), q.lod, q.vertexCount, q.triangleCount,
                     q.material);
        for (int k = 0; k < 3; ++k) std::fprintf(f, " %.6f", lo[k]);
        for (int k = 0; k < 3; ++k) std::fprintf(f, " %.6f", hi[k]);
        std::fprintf(f, "\n");
    }
    std::fclose(f);
    return true;
}

// ------------------------------------------------------------
bool SchreibeBoneDump(Interface* ip, const fb::Model& modell,
                      const std::wstring& zieldatei) {
    // Der Massstab MUSS mit in die Datei. Die Gegenprobe rechnet die
    // Sollwerte sonst ohne ihn und meldet eine Abweichung, die keine ist -
    // genau das ist passiert: 116,11 Einheiten bei Wep_Aim_Target_Rig,
    // exakt der Unterschied zwischen -3,026 m und -119,14 Zoll.
    if (ip == nullptr) return false;

    // "wb", damit unter Windows kein \r dazukommt - sonst ist der
    // Zeichenvergleich mit der Python-Referenz wertlos. Genau
    // daran hat sich Stufe 0 schon einmal aufgehaengt.
    FILE* f = _wfopen(zieldatei.c_str(), L"wb");
    if (f == nullptr) return false;

    // 1.21.0: UV-Bereiche je Mesh - liegt die UV-Karte ausserhalb 0..1 oder ist
    // sie leer, passt die Textur nicht (Verdacht bei den Fahrzeugen).
    for (size_t i = 0; i < modell.meshes.size(); ++i) {
        const auto& me = modell.meshes[i];
        double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
        bool erste = true;
        for (size_t k = 0; k + 1 < me.uv0.size(); k += 2) {
            const double u = static_cast<double>(me.uv0[k]), v = static_cast<double>(me.uv0[k + 1]);
            if (erste) { u0 = u1 = u; v0 = v1 = v; erste = false; }
            if (u < u0) u0 = u;
            if (u > u1) u1 = u;
            if (v < v0) v0 = v;
            if (v > v1) v1 = v;
        }
        // 1.26.0: Sind die beiden Saetze ueberhaupt verschieden? Wenn uv1 = uv0,
        // hilft das Umschalten nicht und die Ursache liegt woanders.
        size_t anders = 0;
        for (size_t k = 0; k + 1 < me.uv0.size() && k + 1 < me.uv1.size(); k += 2)
            if (std::fabs(me.uv0[k] - me.uv1[k]) > 0.001f || std::fabs(me.uv0[k + 1] - me.uv1[k + 1]) > 0.001f) ++anders;
        // 1.35.0: Welche Vertexstroeme hat das Mesh wirklich? Beim AT-AT steht
        // bei allen zwoelf Abschnitten "keine Gewichte in der Datei" - hier ist
        // zu sehen, ob Gewichte fehlen oder nur ungelesen bleiben.
        {
            std::string str;
            if (me.has(fb::STREAM_POS)) str += " pos";
            if (me.has(fb::STREAM_NORMAL)) str += " normal";
            if (me.has(fb::STREAM_TANGENT)) str += " tangent";
            if (me.has(fb::STREAM_UV0)) str += " uv0";
            if (me.has(fb::STREAM_UV1)) str += " uv1";
            if (me.has(fb::STREAM_COLOR)) str += " farbe";
            if (me.has(fb::STREAM_BONEIDX)) str += " boneidx";
            if (me.has(fb::STREAM_BONEWGT)) str += " bonewgt";
            if (me.has(fb::STREAM_BONEIDX2)) str += " boneidx2";
            if (me.has(fb::STREAM_BONEWGT2)) str += " bonewgt2";
            Log("STROEME  %2zu %-44s%s, boneRefs %zu, weight %zu Werte", i, me.name.c_str(), str.c_str(), me.boneRefs.size(),
                me.weight.size());
        }
        Log("UVBEREICH %2zu %-44s uv0 %zu Werte, u %.3f..%.3f, v %.3f..%.3f, uv1 %zu Werte, davon %zu anders als uv0%s", i, me.name.c_str(),
            me.uv0.size() / 2, u0, u1, v0, v1, me.uv1.size() / 2, anders,
            modell.source.compare(0, 9, "vehicle: ") == 0 ? "  (Fahrzeug: uv1 ist Hauptkanal)" : "");
        (void)erste;
    }
    std::fprintf(f, "QUELLE %s\n", modell.source.c_str());
    std::fprintf(f, "SKELETT %s bones=%zu\n", modell.skeleton.c_str(),
                 g_letzteBones.size());
    std::fprintf(f, "MASSSTAB %.6f\n", static_cast<double>(Massstab()));
    // Die Fassung MUSS mit hinein. START.bat vergleicht in Schritt 2, das
    // Plugin wird aber erst in Schritt 3 gebaut und in Schritt 4 installiert -
    // geprueft wird also immer der Dump des VORIGEN Imports. Ohne diese Zeile
    // sieht eine alte Abweichung aus wie eine neue.
    std::fprintf(f, "FASSUNG %ls\n", SWBF2IMPORT_VERSION_STR);

    for (size_t i = 0; i < g_letzteBones.size(); ++i) {
        INode* n = g_letzteBones[i];
        if (n == nullptr) continue;

        INode* e = n->GetParentNode();
        int eltern = -1;
        if (e != nullptr && !e->IsRootNode()) {
            for (size_t k = 0; k < g_letzteBones.size(); ++k) {
                if (g_letzteBones[k] == e) { eltern = static_cast<int>(k); break; }
            }
        }

        const Matrix3 m = n->GetNodeTM(0);
        char name[256];
        std::snprintf(name, sizeof(name), "%ls", n->GetName());

        std::fprintf(f, "BONE %zu %s eltern=%d", i, name, eltern);
        for (int r = 0; r < 4; ++r) {
            const Point3 p = m.GetRow(r);
            std::fprintf(f, " %.6f %.6f %.6f", p.x, p.y, p.z);
        }
        std::fprintf(f, "\n");

        // Dieselbe Matrix, wie sie GESETZT wurde. Weicht sie von der
        // zurueckgelesenen ab, hat Max sie veraendert - und nicht wir.
        if (i < g_gesetzteTM.size()) {
            std::fprintf(f, "SETZ %zu", i);
            for (int r = 0; r < 4; ++r) {
                const Point3 p = g_gesetzteTM[i].GetRow(r);
                std::fprintf(f, " %.6f %.6f %.6f", p.x, p.y, p.z);
            }
            std::fprintf(f, "\n");
        }
    }
    std::fclose(f);
    return true;
}

// ------------------------------------------------------------
// ------------------------------------------------------------
//  Stufe 3: Skinning.
//
//  Je Mesh ein Skin-Modifikator, gefuellt ueber ISkinImportData. Der
//  Ablauf, den das SDK verlangt: Modifikator anlegen, Bones anmelden,
//  den Knoten EINMAL auswerten (erst dabei legt Skin seine Daten fuer
//  diesen Knoten an - vorher scheitert AddWeights), dann die Gewichte.
//  Bones und Mesh stehen in diesem Moment beide in der Ruhelage; genau
//  die merkt sich Skin als Bindung.
//
//  Die Gewichte kommen aus der .fbmodel: bis ACHT Einfluesse je Vertex
//  (zwei Saetze zu vier), der lokale Index geht ueber boneRefs auf den
//  Skelettindex (bei SWBF2 ist das die Identitaet 0..247). Ein Max-Vertex
//  ist genau ein Vertex der Datei (BaueMeshes schweisst nichts zusammen).
//
//  Gemessen wird sofort: jedes Gewicht wird ueber ISkinContextData
//  zurueckgelesen und gegen die Datei gehalten (normiert, je Bone
//  zusammengefasst). Die groesste Abweichung steht im Protokoll.
// ------------------------------------------------------------
int BaueSkin(Interface* ip, const fb::Model& modell, const std::vector<INode*>& meshKnoten,
             const std::vector<INode*>& boneKnoten, std::wstring& bericht) {
    std::unordered_map<INode*, int> boneIndex;
    for (size_t b = 0; b < boneKnoten.size(); ++b) if (boneKnoten[b] != nullptr) boneIndex[boneKnoten[b]] = static_cast<int>(b);

    int gebaut = 0;
    size_t vertsGewichtet = 0, vertsOhne = 0, vertsGesamt = 0;
    double groesste = 0.0;
    int schlimmsterMesh = -1;
    size_t starr = 0;
    for (size_t mi = 0; mi < modell.meshes.size() && mi < meshKnoten.size(); ++mi) {
        INode* node = meshKnoten[mi];
        const fb::Mesh& q = modell.meshes[mi];
        if (node == nullptr) continue;
        const size_t n4 = static_cast<size_t>(q.vertexCount) * 4;
        // 1.36.0: STARRE Bindung. Die STROEME-Zeile beim AT-AT zeigt "boneidx"
        // OHNE "bonewgt": jeder Vertex gehoert zu GENAU EINEM Bone, das Gewicht
        // ist dann 1,0 und wird gar nicht erst gespeichert. Frueher verlangte
        // der Skin beide Stroeme und uebersprang alle zwoelf Abschnitte.
        const bool ohneGewichte = q.boneIndex.size() >= n4 && q.weight.size() < n4;
        if (q.boneIndex.size() < n4) {
            // 1.35.0: Ohne Gewichte, aber MIT genau einem Bone-Verweis ist das
            // Teil STARR an diesem Bone befestigt (so baut das Spiel den AT-AT:
            // zwoelf Abschnitte, kein einziger geskinnt). Dann haengen wir den
            // Knoten einfach an den Bone - damit bewegt es sich mit.
            if (q.boneRefs.size() == 1) {
                const size_t bi = static_cast<size_t>(q.boneRefs[0]);
                if (bi < boneKnoten.size() && boneKnoten[bi] != nullptr) {
                    boneKnoten[bi]->AttachChild(node, 1);
                    Log("Skin %2zu %-40s keine Gewichte - starr an Bone %s gehaengt", mi, q.name.c_str(),
                        modell.bones[bi].name.c_str());
                    ++starr;
                    continue;
                }
            }
            Log("Skin %2zu %-40s keine Gewichte in der Datei (boneRefs %zu), uebersprungen", mi, q.name.c_str(), q.boneRefs.size());
            continue;
        }
        if (ohneGewichte) {
            // 1.37.0: Wie sehen die Knochenindizes wirklich aus? Beim AT-AT
            // bekamen nur 9 670 von 22 852 Vertices ein Gewicht - offenbar
            // liegen viele Werte ausserhalb der boneRefs-Tabelle. Hier je Mesh:
            // Spannweite der vier Plaetze, wie viele ausserhalb liegen und ob
            // die Plaetze 1..3 ueberhaupt belegt sind.
            uint16_t min0 = 0xFFFF, max0 = 0;
            size_t ausserhalb = 0, belegt123 = 0;
            for (uint32_t v = 0; v < q.vertexCount; ++v) {
                const uint16_t b0 = q.boneIndex[static_cast<size_t>(v) * 4];
                if (b0 < min0) min0 = b0;
                if (b0 > max0) max0 = b0;
                if (!q.boneRefs.empty() && b0 >= q.boneRefs.size()) ++ausserhalb;
                for (int k = 1; k < 4; ++k) if (q.boneIndex[static_cast<size_t>(v) * 4 + static_cast<size_t>(k)] != 0) { ++belegt123; break; }
            }
            // 1.40.0: Die ROHWERTE der ersten Vertices und die Tabelle selbst -
            // daran ist die Kodierung abzulesen (Faktor, Versatz, Bytepaare).
            {
                std::string z;
                for (uint32_t v = 0; v < q.vertexCount && v < 6; ++v) {
                    z += "  [";
                    for (int k = 0; k < 4; ++k) {
                        if (k) z += ",";
                        z += std::to_string(q.boneIndex[static_cast<size_t>(v) * 4 + static_cast<size_t>(k)]);
                    }
                    z += "]";
                }
                std::string t;
                for (size_t k = 0; k < q.boneRefs.size() && k < 12; ++k) t += " " + std::to_string(q.boneRefs[k]);
                Log("Skin %2zu %-40s Rohwerte:%s | boneRefs:%s", mi, q.name.c_str(), z.c_str(), t.c_str());
            }
            Log("Skin %2zu %-40s ohne Gewichtstrom - Platz0 %u..%u, %zu ausserhalb von %zu boneRefs, Plaetze 1-3 belegt bei %zu Vertices",
                mi, q.name.c_str(), static_cast<unsigned>(min0), static_cast<unsigned>(max0), ausserhalb, q.boneRefs.size(), belegt123);
        }
        const bool acht = q.boneIndex2.size() >= n4 && q.weight2.size() >= n4;
        const int einfluesse = acht ? 8 : 4;
        auto skelettIndex = [&](uint16_t lokal) -> int {
            if (!q.boneRefs.empty()) {
                if (lokal < q.boneRefs.size()) return static_cast<int>(q.boneRefs[lokal]);
                // 1.40.0: Der Rueckfall aus 1.37.0 (Wert = Skelettindex) war
                // FALSCH. Das Protokoll zeigte es: der Kopf haengte danach an
                // "Reference" und an Beinknochen. Die Werte sind also doch
                // Tabellenplaetze - nur anders kodiert. Bis das geklaert ist,
                // faellt so ein Vertex wieder weg, statt falsch zu binden.
                return -1;
            }
            return static_cast<int>(lokal);
        };
        auto einfluss = [&](uint32_t v, int k, int& bone, float& w) {
            const size_t i = static_cast<size_t>(v) * 4 + static_cast<size_t>(k & 3);
            if (ohneGewichte) w = (k == 0) ? 1.0f : 0.0f;                  // 1.36.0
            else w = (k < 4) ? q.weight[i] : q.weight2[i];
            bone = skelettIndex((k < 4) ? q.boneIndex[i] : q.boneIndex2[i]);
        };

        // Welche Bones dieses Mesh wirklich braucht.
        std::vector<char> benutzt(boneKnoten.size(), 0);
        for (uint32_t v = 0; v < q.vertexCount; ++v) {
            for (int k = 0; k < einfluesse; ++k) {
                int b = -1;
                float w = 0.0f;
                einfluss(v, k, b, w);
                if (w > 0.0f && b >= 0 && static_cast<size_t>(b) < boneKnoten.size() && boneKnoten[static_cast<size_t>(b)] != nullptr) {
                    benutzt[static_cast<size_t>(b)] = 1;
                }
            }
        }

        Modifier* mod = static_cast<Modifier*>(ip->CreateInstance(OSM_CLASS_ID, SKIN_CLASSID));
        ISkinImportData* imp = (mod != nullptr) ? static_cast<ISkinImportData*>(mod->GetInterface(I_SKINIMPORTDATA)) : nullptr;
        ISkin* iskin = (mod != nullptr) ? static_cast<ISkin*>(mod->GetInterface(I_SKIN)) : nullptr;
        if (mod == nullptr || imp == nullptr || iskin == nullptr) {
            Log("Skin %2zu %-40s Skin-Modifikator liess sich nicht anlegen", mi, q.name.c_str());
            continue;
        }
        // Der dokumentierte Weg (SDK, "Adding Modifiers to Objects"): einen
        // mit CreateInstance angelegten Modifikator ueber Interface7::AddModifier
        // auf den Knoten legen. Max legt das abgeleitete Objekt dabei selbst an
        // oder nimmt das vorhandene. Von Hand (CreateDerivedObject) braeuchte es
        // modstack.h - das hat 0.34.0 gefehlt, alle zwoelf Jahrgaenge brachen ab.
        Interface7* ip7 = GetCOREInterface7();
        const int ergebnis = (ip7 != nullptr) ? static_cast<int>(ip7->AddModifier(*node, *mod)) : -99;
        if (ergebnis != static_cast<int>(Interface7::kRES_SUCCESS)) {
            Log("Skin %2zu %-40s AddModifier meldete %d (%s)", mi, q.name.c_str(), ergebnis,
                ergebnis == static_cast<int>(Interface7::kRES_MOD_NOT_APPLICABLE) ? "nicht anwendbar" :
                ergebnis == static_cast<int>(Interface7::kRES_INTERNAL_ERROR) ? "interner Fehler" : "unbekannt");
            continue;
        }

        int angemeldet = 0;
        for (size_t b = 0; b < benutzt.size(); ++b) {
            if (!benutzt[b]) continue;
            imp->AddBoneEx(boneKnoten[b], FALSE);
            ++angemeldet;
        }
        node->EvalWorldState(ip->GetTime());

        size_t gesetzt = 0, ohne = 0;
        for (uint32_t v = 0; v < q.vertexCount; ++v) {
            Tab<INode*> bones;
            Tab<float> gewichte;
            for (int k = 0; k < einfluesse; ++k) {
                int b = -1;
                float w = 0.0f;
                einfluss(v, k, b, w);
                if (w <= 0.0f || b < 0 || static_cast<size_t>(b) >= boneKnoten.size()) continue;
                INode* bn = boneKnoten[static_cast<size_t>(b)];
                if (bn == nullptr) continue;
                bones.Append(1, &bn);
                gewichte.Append(1, &w);
            }
            if (bones.Count() == 0) { ++ohne; continue; }
            if (imp->AddWeights(node, static_cast<int>(v), bones, gewichte)) ++gesetzt;
        }
        node->EvalWorldState(ip->GetTime());

        // Zuruecklesen und gegen die Datei halten.
        double abw = -1.0;
        ISkinContextData* ctx = iskin->GetContextInterface(node);
        if (ctx != nullptr && ctx->GetNumPoints() == static_cast<int>(q.vertexCount)) {
            abw = 0.0;
            std::unordered_map<int, double> soll, ist;
            for (uint32_t v = 0; v < q.vertexCount; ++v) {
                soll.clear();
                ist.clear();
                double summe = 0.0;
                for (int k = 0; k < einfluesse; ++k) {
                    int b = -1;
                    float w = 0.0f;
                    einfluss(v, k, b, w);
                    if (w <= 0.0f || b < 0 || static_cast<size_t>(b) >= boneKnoten.size() || boneKnoten[static_cast<size_t>(b)] == nullptr) continue;
                    soll[b] += w;
                    summe += w;
                }
                if (summe <= 0.0) continue;
                const int zahl = ctx->GetNumAssignedBones(static_cast<int>(v));
                for (int j = 0; j < zahl; ++j) {
                    INode* bn = iskin->GetBone(ctx->GetAssignedBone(static_cast<int>(v), j));
                    const auto it = boneIndex.find(bn);
                    ist[it != boneIndex.end() ? it->second : -2] += ctx->GetBoneWeight(static_cast<int>(v), j);
                }
                for (const auto& s : soll) {
                    const auto it = ist.find(s.first);
                    const double d = std::fabs(s.second / summe - (it != ist.end() ? it->second : 0.0));
                    if (d > abw) abw = d;
                }
                for (const auto& i : ist) {
                    if (soll.find(i.first) == soll.end() && i.second > abw) abw = i.second;
                }
            }
        }
        if (abw > groesste) { groesste = abw; schlimmsterMesh = static_cast<int>(mi); }
        ++gebaut;
        vertsGewichtet += gesetzt;
        vertsOhne += ohne;
        vertsGesamt += q.vertexCount;
        if (abw >= 0.0) {
            Log("Skin %2zu %-40s %3d Bones  %6zu von %6u Vertices  bis %d Einfluesse  zurueckgelesen: Abweichung %.6f",
                mi, q.name.c_str(), angemeldet, gesetzt, q.vertexCount, einfluesse, abw);
            // 1.39.0: WELCHE Bones benutzt dieses Mesh? DH: "Kopf hat keinen
            // Skin" - obwohl 21 993 von 21 993 Vertices gewichtet sind. Dann
            // haengen sie vermutlich an den falschen Bones.
            {
                // 1.43.0: der Bone mit dem groessten Gewicht je Vertex - bisher
                // Platz 0, auch mit Gewicht 0. Beim ARC Trooper stand dort ein
                // Fuellwert (145 = RightArm), und der Koerper "hing an RightArm".
                std::map<int, size_t> proBone;
                for (uint32_t v = 0; v < q.vertexCount; ++v) {
                    int beste = -1;
                    float groesstes = 0.0f;
                    for (int k = 0; k < einfluesse; ++k) {
                        int b = -1;
                        float w = 0;
                        einfluss(v, k, b, w);
                        if (b >= 0 && w > groesstes) { groesstes = w; beste = b; }
                    }
                    if (beste >= 0) ++proBone[beste];
                }
                std::vector<std::pair<size_t, int>> sortiert;
                for (const auto& kv : proBone) sortiert.push_back({ kv.second, kv.first });
                std::sort(sortiert.rbegin(), sortiert.rend());
                std::string z;
                for (size_t k = 0; k < sortiert.size() && k < 6; ++k) {
                    const size_t bi = static_cast<size_t>(sortiert[k].second);
                    z += "  " + (bi < modell.bones.size() ? modell.bones[bi].name : std::string("?")) + " (" +
                         std::to_string(sortiert[k].first) + ")";
                }
                Log("Skin %2zu %-40s haengt vor allem an:%s", mi, q.name.c_str(), z.c_str());
            }
        } else {
            Log("Skin %2zu %-40s %3d Bones  %6zu von %6u Vertices  bis %d Einfluesse  zurueckgelesen: NICHT MOEGLICH",
                mi, q.name.c_str(), angemeldet, gesetzt, q.vertexCount, einfluesse);
        }
    }
    Log("Skin angelegt: %d Meshes, %zu starr an einen Bone gehaengt, %zu von %zu Vertices gewichtet (%zu ohne Gewicht), groesste Abweichung %.6f%s",
        gebaut, starr, vertsGewichtet, vertsGesamt, vertsOhne, groesste < 0.0 ? 0.0 : groesste,
        schlimmsterMesh >= 0 ? (" bei " + modell.meshes[static_cast<size_t>(schlimmsterMesh)].name).c_str() : "");
    MCHAR puffer[256];
    _sntprintf(puffer, 256, _T("Skin: %d meshes, %d of %d vertices weighted."),
               gebaut, static_cast<int>(vertsGewichtet), static_cast<int>(vertsGesamt));
    bericht = puffer;
    return gebaut;
}

// ------------------------------------------------------------
//  Stufe 4: Materialien und Texturen.
//
//  Die Zuordnung steht im Beipackzettel neben der .fbmodel (fbbeipack),
//  geschrieben vom Figurenfenster. Hier entsteht je Kombination aus
//  Materialname und Texturen EIN Standardmaterial:
//    - Farbe  (BaseColor/CS)  -> Diffuse-Kanal, Bitmap mit Standard-Gamma
//                                (die PNG ist sRGB wie die BC7_SRGB-Quelle)
//    - Normal (Normal/NW)     -> Bump-Kanal ueber Normal Bump, Bitmap mit
//                                Gamma 1,0 (Normalen sind linear)
//  Standard statt Physical: es gibt ihn in allen zwoelf Jahrgaengen mit
//  dokumentierter C++-Schnittstelle (stdmat.h). Die Kanalnummern gehen
//  ueber StdIDToChannel, wie das SDK es fuer Shader-Materialien verlangt.
//  Danach kommen die Materialien in den Compact Material Editor
//  (Interface::PutMtlToMtlEditor, Slots 0..23 ohne Rueckfrage).
// ------------------------------------------------------------
namespace {

// UTF-16 -> UTF-8 ohne Windows-Aufruf (die Vorabpruefung laeuft ohne Windows).
std::string Utf8Aus(const std::wstring& w) {
    std::string s;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = static_cast<uint32_t>(w[i]);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
            const uint32_t c2 = static_cast<uint32_t>(w[i + 1]);
            if (c2 >= 0xDC00 && c2 <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00); ++i; }
        }
        if (c < 0x80) s += static_cast<char>(c);
        else if (c < 0x800) { s += static_cast<char>(0xC0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { s += static_cast<char>(0xE0 | (c >> 12)); s += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); s += static_cast<char>(0x80 | (c & 0x3F)); }
        else { s += static_cast<char>(0xF0 | (c >> 18)); s += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); s += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); s += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return s;
}

std::string DateiBlatt(const std::string& p) {
    const size_t i = p.find_last_of("\\/");
    return i == std::string::npos ? p : p.substr(i + 1);
}

// Normal Bump (imtl.h: GNORMAL_CLASS_ID). Die Kennung steht hier selbst, weil
// nicht sicher ist, dass jeder Jahrgang das Makro mitbringt; gn_map_normal ist
// der dritte Eintrag in Normal_Param_IDs (gn_mult_spin, gn_bmult_spin,
// gn_map_normal, ...), der Block ist gnormal_params = 0.
const Class_ID kNormalBump(0x243e22c6, 0x63f6a014);
constexpr int kGnMapNormal = 2;

Mtl* BaueStandardMaterial(Interface* ip, const std::string& name, const fbbeipack::Textur* farbe,
                          const fbbeipack::Textur* normal, const fbbeipack::Textur* deckkraft) {
    StdMat2* m = NewDefaultStdMat();
    if (m == nullptr) return nullptr;
    MSTR mname = AusUtf8(name);
    m->SetName(mname);
    if (farbe != nullptr) {
        BitmapTex* bt = NewDefaultBitmapTex();
        if (bt != nullptr) {
            MSTR pfad = AusUtf8(farbe->datei);
            bt->SetMapName(pfad.data());
            MSTR tname = AusUtf8(DateiBlatt(farbe->datei));
            bt->SetName(tname);
            const int kanal = static_cast<int>(m->StdIDToChannel(ID_DI));
            m->SetSubTexmap(kanal, bt);
            m->EnableMap(kanal, TRUE);
            // Im Viewport zeigen (SDK: Interface::ActivateTexture).
            m->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED, TRUE);
            ip->ActivateTexture(bt, m);
        }
    }
    if (deckkraft != nullptr) {
        // Haar-Karten: Alpha als Graubild in den Opacity-Kanal, beidseitig.
        BitmapTex* ot = NewDefaultBitmapTex();
        if (ot != nullptr) {
            MSTR pfad = AusUtf8(deckkraft->datei);
            ot->SetMapName(pfad.data());
            MSTR tname = AusUtf8(DateiBlatt(deckkraft->datei));
            ot->SetName(tname);
            const int kanal = static_cast<int>(m->StdIDToChannel(ID_OP));
            m->SetSubTexmap(kanal, ot);
            m->EnableMap(kanal, TRUE);
            m->SetTwoSided(TRUE);
        }
    }
    if (normal != nullptr) {
        BitmapTex* nb = NewDefaultBitmapTex();
        Texmap* gn = static_cast<Texmap*>(ip->CreateInstance(TEXMAP_CLASS_ID, kNormalBump));
        IParamBlock2* pb = (gn != nullptr) ? gn->GetParamBlockByID(0) : nullptr;
        if (nb != nullptr && gn != nullptr && pb != nullptr) {
            MSTR pfad = AusUtf8(normal->datei);
            nb->SetMapName(pfad.data());
            MSTR tname = AusUtf8(DateiBlatt(normal->datei));
            nb->SetName(tname);
            // Normalen sind linear: Gamma 1,0 statt der sRGB-Annahme.
            BitmapInfo bi;
            bi.SetName(pfad.data());
            bi.SetCustomGamma(1.0f);
            bi.SetCustomFlag(BMM_CUSTOM_GAMMA);
            nb->SetBitmapInfo(bi);
            pb->SetValue(kGnMapNormal, 0, static_cast<Texmap*>(nb));
            const int kanal = static_cast<int>(m->StdIDToChannel(ID_BU));
            m->SetSubTexmap(kanal, gn);
            m->EnableMap(kanal, TRUE);
            m->SetTexmapAmt(kanal, 1.0f, 0);
        }
    }
    return m;
}

} // namespace

// ------------------------------------------------------------
//  PBR-Materialien (0.45.0). Recherche: Physical Material gibt es seit
//  Max 2017 (SDK "What's New 2017"), OpenPBR_Material ab 2025.3 und als
//  Standardmaterial seit 2026 (MAXScript-Hilfe 2026: base_color_map,
//  base_metalness_map, specular_roughness_map, bump_map, geometry_opacity_map,
//  geometry_thin_walled). Gebaut per MAXScript, weil beide Klassen dort mit
//  benannten Parametern erreichbar sind:
//    2017-2025: PhysicalMaterial (roughness_map mit roughness_inv = Glanz)
//    2026+    : OpenPBR_Material (Glanz-Map mit output.invert = Rauheit)
//  Normalen ueber Normal_Bump (Gamma 1,0), Glanz/Metall/Deckkraft linear.
//  Das Skript meldet in eine Statusdatei zurueck, wie viele Materialien
//  entstanden und welche Klasse benutzt wurde.
// ------------------------------------------------------------
struct PbrGruppe {
    std::string name;
    std::string farbe, normal, deckkraft, glanz, metall;
    std::vector<INode*> knoten;
};

bool BauePbrMaterialien(const std::vector<PbrGruppe>& gruppen, std::wstring& klasseAus, size_t& gebautAus) {
    const std::wstring status = swbf2ablage::Ordner() + L"\\materialien.txt";   // 1.43.0
    const bool openpbr = MAX_RELEASE >= 28000;
    auto txt = [](const std::string& x) { return L"\"" + SkriptText(x) + L"\""; };
    std::wstring sk =
        L"(\n fn swbfMap f g = (local t = Bitmaptexture(); t.filename = f; try (t.bitmap = openBitMap f gamma:g) catch (); t)\n"
        L" local cKlasse = " + std::wstring(openpbr ? L"(if OpenPBR_Material != undefined then OpenPBR_Material else PhysicalMaterial)"
                                                  : L"PhysicalMaterial") + L"\n"
        L" local bOpen = (cKlasse != PhysicalMaterial)\n local iGebaut = 0\n";
    for (size_t gi = 0; gi < gruppen.size(); ++gi) {
        const PbrGruppe& g = gruppen[gi];
        std::wstring handles;
        for (size_t k = 0; k < g.knoten.size(); ++k) if (g.knoten[k] != nullptr) handles += (handles.empty() ? L"" : L",") + std::to_wstring(g.knoten[k]->GetHandle());
        sk += L" (\n  local m = cKlasse name:" + txt(g.name) + L"\n";
        if (!g.farbe.empty()) sk += L"  m.base_color_map = swbfMap " + txt(g.farbe) + L" #auto\n";
        if (!g.glanz.empty())
            sk += L"  if bOpen then (local t = swbfMap " + txt(g.glanz) + L" 1.0; t.output.invert = true; m.specular_roughness_map = t)"
                  L" else (m.roughness_map = swbfMap " + txt(g.glanz) + L" 1.0; m.roughness_inv = true)\n";
        if (!g.metall.empty())
            sk += L"  if bOpen then m.base_metalness_map = swbfMap " + txt(g.metall) + L" 1.0 else m.metalness_map = swbfMap " + txt(g.metall) + L" 1.0\n";
        if (!g.normal.empty())
            sk += L"  (local nb = Normal_Bump(); nb.normal_map = swbfMap " + txt(g.normal) + L" 1.0; m.bump_map = nb; try (m.bump_map_amt = 1.0) catch ())\n";
        if (!g.deckkraft.empty())
            sk += L"  if bOpen then (m.geometry_opacity_map = swbfMap " + txt(g.deckkraft) + L" 1.0; m.geometry_thin_walled = true)"
                  L" else m.cutout_map = swbfMap " + txt(g.deckkraft) + L" 1.0\n";
        if (!handles.empty()) sk += L"  for h in #(" + handles + L") do (local n = maxOps.getNodeByHandle h; if (n != undefined) do n.material = m)\n";
        if (gi < 24) sk += L"  meditMaterials[" + std::to_wstring(gi + 1) + L"] = m\n";
        sk += L"  iGebaut += 1\n )\n";
    }
    sk += L" local f = createFile " + txt(fbdatei::Utf8(status)) + L"\n format \"PBR % %\\n\" iGebaut (cKlasse as string) to:f\n close f\n OK\n)\n";
    _wremove(status.c_str());
    const bool gelungen = FuehreSkript(sk);
    std::vector<uint8_t> roh;
    std::string f2;
    gebautAus = 0;
    klasseAus.clear();
    if (fbdatei::LiesAlles(fbdatei::Utf8(status), roh, f2)) {
        const std::string t(roh.begin(), roh.end());
        char k[96] = {};
        if (std::sscanf(t.c_str(), "PBR %zu %95s", &gebautAus, k) >= 1) klasseAus = fbdatei::Breit(k);
    }
    return gelungen && gebautAus == gruppen.size();
}

int BaueMaterialien(Interface* ip, const std::wstring& fbmodelPfad, const std::vector<INode*>& meshKnoten,
                    std::wstring& bericht) {
    const std::string zettel = Utf8Aus(fbmodelPfad + L".material.txt");
    std::vector<fbbeipack::MeshMaterial> mm;
    std::string fehler;
    if (!fbbeipack::Lies(zettel, mm, fehler)) {
        Log("Materialien: kein Beipackzettel (%s) - Meshes bleiben ohne Material", fehler.c_str());
        return 0;
    }
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 19000)
    {
        // Gruppen wie bisher: gleiche Texturen = ein Material.
        std::vector<PbrGruppe> gruppen;
        std::map<std::string, size_t> nachSchluessel;
        std::map<std::string, int> namen;
        for (size_t mi = 0; mi < meshKnoten.size() && mi < mm.size(); ++mi) {
            if (meshKnoten[mi] == nullptr) continue;
            const fbbeipack::MeshMaterial& q = mm[mi];
            PbrGruppe g;
            for (const fbbeipack::Textur& t : q.texturen) {
                if (t.datei.empty()) continue;
                std::string* ziel = t.art == "farbe" ? &g.farbe : t.art == "normal" ? &g.normal : t.art == "deckkraft" ? &g.deckkraft
                                  : t.art == "glanz" ? &g.glanz : t.art == "metall" ? &g.metall : nullptr;
                if (ziel != nullptr && ziel->empty()) *ziel = t.datei;
            }
            const std::string schluessel = q.materialName + "|" + g.farbe + "|" + g.normal + "|" + g.deckkraft + "|" + g.glanz + "|" + g.metall;
            auto it = nachSchluessel.find(schluessel);
            if (it == nachSchluessel.end()) {
                g.name = q.materialName.empty() ? std::string("SWBF2_Material") : q.materialName;
                if (namen[g.name]++ > 0) g.name += " (" + q.meshAsset.substr(q.meshAsset.find_last_of('/') + 1) + ")";
                it = nachSchluessel.emplace(schluessel, gruppen.size()).first;
                gruppen.push_back(g);
            }
            gruppen[it->second].knoten.push_back(meshKnoten[mi]);
        }
        std::wstring klasse;
        size_t gebaut = 0;
        const bool gelungen = BauePbrMaterialien(gruppen, klasse, gebaut);
        for (size_t gi = 0; gi < gruppen.size(); ++gi) {
            const PbrGruppe& g = gruppen[gi];
            Log("Material %2zu %-24s Farbe %s  Normal %s  Glanz %s  Metall %s  Deckkraft %s", gi, g.name.c_str(),
                g.farbe.empty() ? "-" : DateiBlatt(g.farbe).c_str(), g.normal.empty() ? "-" : DateiBlatt(g.normal).c_str(),
                g.glanz.empty() ? "-" : DateiBlatt(g.glanz).c_str(), g.metall.empty() ? "-" : DateiBlatt(g.metall).c_str(),
                g.deckkraft.empty() ? "-" : DateiBlatt(g.deckkraft).c_str());
        }
        Log("Materialien PBR: %zu von %zu gebaut, Klasse %ls%s", gebaut, gruppen.size(), klasse.empty() ? L"?" : klasse.c_str(),
            gelungen ? "" : " - FEHLGESCHLAGEN, Rueckfall auf Standardmaterial");
        if (gelungen) {
            Interface13* ip13 = GetCOREInterface13();
            const bool slateOffen = (ip13 != nullptr) && ip13->IsMtlDlgShowing(1);
            if (ip13 != nullptr && !slateOffen && !ip13->IsMtlDlgShowing(0)) ip13->SetMtlDlgMode(0);
            if (ip13 != nullptr && !slateOffen) ip13->OpenMtlDlg(0);
            MCHAR puffer[200];
            _sntprintf(puffer, 200, _T("Materials: %d %s, Compact Material Editor slots 1-%d."), static_cast<int>(gebaut),
                       klasse.empty() ? _T("PBR") : klasse.c_str(), static_cast<int>(std::min<size_t>(gebaut, 24)));
            bericht = puffer;
            return static_cast<int>(gebaut);
        }
    }
#endif
    std::map<std::string, Mtl*> fertig;
    std::map<std::string, int> namen;
    std::vector<Mtl*> reihe;
    std::vector<std::string> reiheName;
    int zugewiesen = 0, ohne = 0;
    for (size_t mi = 0; mi < meshKnoten.size() && mi < mm.size(); ++mi) {
        INode* node = meshKnoten[mi];
        if (node == nullptr) continue;
        const fbbeipack::MeshMaterial& q = mm[mi];
        const fbbeipack::Textur* farbe = nullptr;
        const fbbeipack::Textur* normal = nullptr;
        const fbbeipack::Textur* deckkraft = nullptr;
        for (const fbbeipack::Textur& t : q.texturen) {
            if (t.datei.empty()) continue;
            if (t.art == "farbe" && farbe == nullptr) farbe = &t;
            else if (t.art == "normal" && normal == nullptr) normal = &t;
            else if (t.art == "deckkraft" && deckkraft == nullptr) deckkraft = &t;
        }
        if (farbe == nullptr && normal == nullptr) ++ohne;
        const std::string schluessel = q.materialName + "|" + (farbe ? farbe->datei : "") + "|" + (normal ? normal->datei : "") +
                                       "|" + (deckkraft ? deckkraft->datei : "");
        auto it = fertig.find(schluessel);
        if (it == fertig.end()) {
            std::string name = q.materialName.empty() ? std::string("SWBF2_Material") : q.materialName;
            if (namen[name]++ > 0) name += " (" + q.meshAsset.substr(q.meshAsset.find_last_of('/') + 1) + ")";
            Mtl* mtl = BaueStandardMaterial(ip, name, farbe, normal, deckkraft);
            it = fertig.emplace(schluessel, mtl).first;
            if (mtl != nullptr) {
                reihe.push_back(mtl);
                reiheName.push_back(name);
                Log("Material %2zu %-24s Farbe %s  Normal %s (Gamma 1.0)  Deckkraft %s  ueber %s", reihe.size() - 1, name.c_str(),
                    farbe ? DateiBlatt(farbe->datei).c_str() : "-", normal ? DateiBlatt(normal->datei).c_str() : "-",
                    deckkraft ? DateiBlatt(deckkraft->datei).c_str() : "-", q.weg.empty() ? "-" : q.weg.c_str());
            }
        }
        if (it->second != nullptr) { node->SetMtl(it->second); ++zugewiesen; }
    }

    // In den Compact Material Editor. Laut SDK ersetzt PutMtlToMtlEditor mit
    // einer Slotnummer 0..23 das Material OHNE Rueckfrage; der Modus kommt
    // aus Interface13 (mtlDlgMode_Basic = 0, mtlDlgMode_Advanced = 1). Ist der
    // Slate-Editor offen, wird nicht umgeschaltet - zwei Editoren zugleich
    // nennt das SDK instabil.
    const int slots = static_cast<int>(std::min<size_t>(reihe.size(), 24));
    Interface13* ip13 = GetCOREInterface13();
    const bool slateOffen = (ip13 != nullptr) && ip13->IsMtlDlgShowing(1);
    if (ip13 != nullptr && !slateOffen && !ip13->IsMtlDlgShowing(0)) ip13->SetMtlDlgMode(0);
    for (int s = 0; s < slots; ++s) ip->PutMtlToMtlEditor(reihe[static_cast<size_t>(s)], s);
    if (ip13 != nullptr && !slateOffen) ip13->OpenMtlDlg(0);
    Log("Materialien: %zu angelegt, %d Meshes zugewiesen, %d ohne Textur; Material-Editor: Slots 0..%d%s",
        reihe.size(), zugewiesen, ohne, slots - 1, slateOffen ? " (Slate war offen - nicht umgeschaltet)" : " (Compact)");
    if (reihe.size() > 24) Log("Materialien: nur die ersten 24 passen in den Compact-Editor");
    MCHAR puffer[160];
    _sntprintf(puffer, 160, _T("Materials: %d created, Compact Material Editor slots 1-%d."),
               static_cast<int>(reihe.size()), slots);
    bericht = puffer;
    return static_cast<int>(reihe.size());
}

// ------------------------------------------------------------
//  Layer (0.40.0), wie im XFBIN-Importer: ein Layer fuer die Bones und
//  einer fuer die Meshes jeder Figur - "anakin_01 Bones", "anakin_01
//  Meshes"; eine zweite Figur desselben Namens bekommt " #2". Gebaut wird
//  per MAXScript (LayerManager), die Knoten kommen ueber ihre Handles.
// ------------------------------------------------------------
std::string LayerBasis(const std::string& quelle) {
    // 1.31.0: Fahrzeuge - "vehicle: ground/at_te" -> at_te. Vorher kam hier
    // "vehicle: ground" heraus; dieser Name landete als Rig-Vermerk in der
    // Szene, das Animationsfenster fragte damit nach der Ruhelage, fand kein
    // Fahrzeug und nahm wieder das Menschenskelett - daher die kippende Pose.
    if (quelle.compare(0, 9, "vehicle: ") == 0) {
        const std::string rest = quelle.substr(9);
        const size_t s2 = rest.find_last_of('/');
        return s2 == std::string::npos ? rest : rest.substr(s2 + 1);
    }
    // win32/characters/hero/anakin/anakin_01/vur_anakin_01_bpb -> anakin_01
    std::vector<std::string> teile;
    size_t a = 0;
    for (size_t i = 0; i <= quelle.size(); ++i) {
        if (i == quelle.size() || quelle[i] == '/') { if (i > a) teile.push_back(quelle.substr(a, i - a)); a = i + 1; }
    }
    if (teile.size() >= 2) return teile[teile.size() - 2];
    if (!teile.empty()) return teile.back();
    return "SWBF2";
}

void SortiereInLayer(const fb::Model& modell, const std::vector<INode*>& bones, const std::vector<INode*>& meshes) {
    const std::string basis = LayerBasis(modell.source);
    std::wstring hb, hm, hw;
    size_t nb = 0, nmesh = 0, nw = 0;
    for (INode* n : bones) if (n != nullptr) { hb += (nb++ ? L"," : L"") + std::to_wstring(n->GetHandle()); }
    for (size_t i = 0; i < meshes.size(); ++i) {
        INode* n = meshes[i];
        if (n == nullptr) continue;
        // Waffen (Meshname "weapon_...", 0.42.0) in einen eigenen Layer.
        const bool waffe = i < modell.meshes.size() && modell.meshes[i].name.compare(0, 7, "weapon_") == 0;
        if (waffe) hw += (nw++ ? L"," : L"") + std::to_wstring(n->GetHandle());
        else hm += (nmesh++ ? L"," : L"") + std::to_wstring(n->GetHandle());
    }
    const std::wstring b = SkriptText(basis);
    std::wstring sk =
        L"(\n local sBasis = \"" + b + L"\"\n local sName = sBasis\n local iNr = 1\n local bFrei = false\n"
        L" while (not bFrei) do (\n"
        L"  local iDrin = 0\n"
        L"  for sArt in #(\" Bones\", \" Meshes\", \" Weapons\") do (\n"
        L"   local l = LayerManager.getLayerFromName (sName + sArt)\n"
        L"   if (l != undefined) do (local a = #(); l.nodes &a; iDrin += a.count)\n"
        L"  )\n"
        L"  if (iDrin == 0) then bFrei = true else (iNr += 1; sName = sBasis + \" #\" + (iNr as string))\n"
        L" )\n"
        L" local lb = LayerManager.getLayerFromName (sName + \" Bones\")\n"
        L" if (lb == undefined) do lb = LayerManager.newLayerFromName (sName + \" Bones\")\n"
        L" local lm = LayerManager.getLayerFromName (sName + \" Meshes\")\n"
        L" if (lm == undefined) do lm = LayerManager.newLayerFromName (sName + \" Meshes\")\n"
        L" for h in #(" + hb + L") do (local n = maxOps.getNodeByHandle h; if (n != undefined and lb != undefined) do lb.addNode n)\n"
        // Rigname (0.48.0) = derselbe freie Name wie die Layer ("anakin_01", "anakin_01 #2"),
        // auf JEDEN Bone - daran findet das Animationsfenster das Rig wieder.
        L" for h in #(" + hb + L") do (local n = maxOps.getNodeByHandle h; if (n != undefined) do setUserProp n \"swbf2_rig\" sName)\n"
        L" for h in #(" + hm + L") do (local n = maxOps.getNodeByHandle h; if (n != undefined and lm != undefined) do lm.addNode n)\n"
        + (nw > 0 ? L" local lw = LayerManager.getLayerFromName (sName + \" Weapons\")\n"
                    L" if (lw == undefined) do lw = LayerManager.newLayerFromName (sName + \" Weapons\")\n"
                    L" for h in #(" + hw + L") do (local n = maxOps.getNodeByHandle h; if (n != undefined and lw != undefined) do lw.addNode n)\n"
                  : std::wstring())
        + L" OK\n)\n";
    const bool gelungen = FuehreSkript(sk);
    Log("Layer: \"%s Bones\" (%zu), \"%s Meshes\" (%zu), \"%s Weapons\" (%zu)%s", basis.c_str(), nb, basis.c_str(), nmesh,
        basis.c_str(), nw, gelungen ? " - bei belegtem Namen mit #2, #3 ..." : " - MAXScript FEHLGESCHLAGEN");
}

// ------------------------------------------------------------
//  Basispose (0.43.0) gegen die "verzerrten Koepfe".
//
//  Frostbite-Koepfe sehen in der Bindepose verzerrt aus; das Spiel legt
//  eine Pose darueber (ZenHAX, id-daemon: Werkzeuge fuer SWBF2 und weitere
//  Frostbite-Spiele). Bei SWBF2 steht sie als BasePoseTransforms in den
//  ObjectBlueprints der Figur (Kopf, Haare, Koerper). castool/Fenster
//  schreiben sie als Beipack "<fbmodel>.pose.txt" (lokale Lage je Bone,
//  Spielraum, Meter). Hier: NACH dem Skin auf die Bones legen - der Skin ist
//  in der Ruhelage gebunden, die Pose verformt das Gesicht in seine Form.
// ------------------------------------------------------------
bool LiesPoseDatei(const std::wstring& pfad, std::map<std::string, std::array<double, 12>>& aus) {
    aus.clear();
    std::vector<uint8_t> roh;
    std::string f;
    if (!fbdatei::LiesAlles(fbdatei::Utf8(pfad), roh, f)) return false;
    std::string zeile;
    for (size_t i = 0; i <= roh.size(); ++i) {
        const char c = i < roh.size() ? static_cast<char>(roh[i]) : '\n';
        if (c != '\n') { if (c != '\r') zeile += c; continue; }
        if (zeile.compare(0, 5, "BONE ") == 0) {
            std::vector<std::string> teile;
            std::string cur;
            for (char z : zeile) { if (z == ' ') { if (!cur.empty()) teile.push_back(cur); cur.clear(); } else cur += z; }
            if (!cur.empty()) teile.push_back(cur);
            if (teile.size() == 14) {
                std::array<double, 12> w{};
                bool gelungen = true;
                for (int k = 0; k < 12; ++k) {
                    const std::string& t = teile[static_cast<size_t>(k + 2)];
                    const auto r = std::from_chars(t.data(), t.data() + t.size(), w[static_cast<size_t>(k)]);
                    if (r.ec != std::errc()) gelungen = false;
                }
                if (gelungen) aus[teile[1]] = w;
            }
        }
        zeile.clear();
    }
    return !aus.empty();
}

void WendeBasisposeAn(const fb::Model& modell, const std::vector<INode*>& bones, const std::wstring& posePfad) {
    std::map<std::string, std::array<double, 12>> pose;
    if (!LiesPoseDatei(posePfad, pose)) { Log("Basispose: keine (%ls)", posePfad.c_str()); return; }
    const Matrix3 achsen = AchsenMatrix();
    const float mass = Massstab();
    size_t gesetzt = 0;
    double groesste = 0.0;
    SuspendAnimate();
    AnimateOff();
    for (size_t i = 0; i < modell.bones.size() && i < bones.size(); ++i) {       // Eltern vor Kind
        INode* n = bones[i];
        const auto it = pose.find(modell.bones[i].name);
        if (n == nullptr || it == pose.end()) continue;
        const std::array<double, 12>& w = it->second;
        Matrix3 lokal;
        lokal.SetRow(0, Point3(static_cast<float>(w[0]), static_cast<float>(w[1]), static_cast<float>(w[2])));
        lokal.SetRow(1, Point3(static_cast<float>(w[3]), static_cast<float>(w[4]), static_cast<float>(w[5])));
        lokal.SetRow(2, Point3(static_cast<float>(w[6]), static_cast<float>(w[7]), static_cast<float>(w[8])));
        lokal.SetRow(3, Point3(static_cast<float>(w[9]), static_cast<float>(w[10]), static_cast<float>(w[11])));
        const bool wurzel = n->GetParentNode() == nullptr || n->GetParentNode()->IsRootNode();
        if (wurzel) lokal = lokal * achsen;
        const Point3 t = lokal.GetRow(3);
        lokal.SetRow(3, Point3(t.x * mass, t.y * mass, t.z * mass));
        const Matrix3 vorher = n->GetNodeTM(0);
        // NICHT const: bis Max 2021 heisst es SetNodeTM(TimeValue, Matrix3&) -
        // 0.43.0 brach dort in sechs Jahrgaengen ab (C2664), ab 2022 lief es.
        Matrix3 neu = wurzel ? lokal : lokal * n->GetParentNode()->GetNodeTM(0);
        n->SetNodeTM(0, neu);
        const Point3 a = vorher.GetRow(3), b = neu.GetRow(3);
        const double d = std::sqrt(static_cast<double>((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)));
        groesste = std::max(groesste, d);
        ++gesetzt;
    }
    ResumeAnimate();
    // Merken, welche Pose zu dieser Figur gehoert - das Animationsfenster
    // nimmt sie als Ruhelage fuer diese Bones.
    for (size_t i = 0; i < bones.size() && i < modell.bones.size(); ++i) {
        if (bones[i] != nullptr && modell.bones[i].parent < 0) bones[i]->SetUserPropString(MSTR(_T("swbf2_pose")), MSTR(posePfad.c_str()));
    }
    Log("Basispose: %zu von %zu Bones aus %ls gesetzt, groesste Verschiebung %.3f Einheiten", gesetzt, pose.size(), posePfad.c_str(), groesste);
}

int ImportiereDatei(const MCHAR* pfad, BOOL ohneRueckfragen, std::wstring* berichtAus) {
    if (pfad == nullptr) return 0;
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) return 0;

    // Fuer den ganzen Import den Dezimalpunkt erzwingen - Protokoll
    // UND Gegenprobe sollen dieselben Zahlen zeigen wie Python.
    PunktStattKomma punkt;

    LogAuf();
    Log("SWBF2 Import %ls", SWBF2IMPORT_VERSION_STR);
    Log("Zahlenformat: Dezimalpunkt erzwungen (vorher \"%s\")",
        punkt.vorher.empty() ? "unbekannt" : punkt.vorher.c_str());
    Log("Datei: %ls", pfad);

    std::vector<uint8_t> daten;
    std::string fehler;
    // Das Plugin gibt es nur unter Windows; der weite Pfad ist Pflicht,
    // sonst scheitern Dateinamen mit Umlauten oder fremden Zeichen.
    if (!fb::readFileW(pfad, daten, fehler)) {
        Log("ABBRUCH: Datei nicht lesbar (%s)", fehler.c_str());
        LogZu();
        if (berichtAus != nullptr) *berichtAus = L"File could not be read";
        if (!ohneRueckfragen) {
            MessageBox(ip->GetMAXHWnd(),
                       _T("The file could not be read."),
                       _T("SWBF2 Import ") SWBF2IMPORT_VERSION_STR, MB_ICONERROR);
        }
        return 0;
    }

    Log("Gelesen: %zu Byte", daten.size());

    fb::Model modell;
    if (!fb::readModel(daten, modell, fehler)) {
        Log("ABBRUCH beim Lesen: %s", fehler.c_str());
        LogZu();
        if (berichtAus != nullptr) { MSTR m = AusUtf8(fehler); *berichtAus = m.data(); }
        if (!ohneRueckfragen) {
            MSTR m = AusUtf8(fehler);
            MessageBox(ip->GetMAXHWnd(), m.data(),
                       _T("SWBF2 Import ") SWBF2IMPORT_VERSION_STR, MB_ICONERROR);
        }
        return 0;
    }

    size_t verts = 0, tris = 0;
    for (const fb::Mesh& mm : modell.meshes) { verts += mm.vertexCount; tris += mm.triangleCount; }
    Log("Quelle: %s", modell.source.c_str());
    Log("Skelett: %s, %zu Bones", modell.skeleton.c_str(), modell.bones.size());
    Log("Meshes: %zu, Vertices: %zu, Dreiecke: %zu, Materialien: %zu",
        modell.meshes.size(), verts, tris, modell.materials.size());
    Log("Achsen: Spiel (x,y,z) -> Max (x,-z,y)");

    std::wstring bericht, berichtMesh, berichtSkin;
    std::vector<INode*> meshKnoten;
    theHold.Begin();
    const int n = BaueSkelett(ip, modell, bericht);
    const int nm = BaueMeshes(ip, modell, Massstab(), berichtMesh, meshKnoten);
    if (n > 0 && nm > 0) BaueSkin(ip, modell, meshKnoten, g_boneKnoten, berichtSkin);
    if (n > 0) WendeBasisposeAn(modell, g_boneKnoten, std::wstring(pfad) + L".pose.txt");
    std::wstring berichtMat;
    if (nm > 0) BaueMaterialien(ip, std::wstring(pfad), meshKnoten, berichtMat);
    if (n > 0 || nm > 0) SortiereInLayer(modell, g_boneKnoten, meshKnoten);
    if (n > 0 || nm > 0) {
        theHold.Accept(_T("SWBF2 Import"));
    } else {
        theHold.Cancel();
    }

    // Die Gegenprobe wird IMMER geschrieben, ohne Zusatzschalter.
    std::wstring dump(pfad);
    dump += L"_max_bones.txt";
    const bool dumpOk = SchreibeBoneDump(ip, modell, dump);
    Log("Gegenprobe %ls: %ls", dumpOk ? L"geschrieben" : L"FEHLGESCHLAGEN", dump.c_str());

    std::wstring dumpM(pfad);
    dumpM += L"_max_meshes.txt";
    const bool dumpMOk = SchreibeMeshDump(modell, Massstab(), dumpM);
    Log("Meshprobe %ls: %ls", dumpMOk ? L"geschrieben" : L"FEHLGESCHLAGEN", dumpM.c_str());
    Log("Fertig.");
    LogZu();

    ip->RedrawViews(ip->GetTime());

    std::wstring text = bericht;
    text += L"\n";
    text += berichtMesh;
    if (!berichtSkin.empty()) { text += L"\n"; text += berichtSkin; }
    if (!berichtMat.empty()) { text += L"\n"; text += berichtMat; }
    text += L"\n\nCross-check written:\n";
    text += dump;
    if (berichtAus != nullptr) *berichtAus = text;
    if (!ohneRueckfragen) {
        MSTR m(text.c_str());   // schon wchar_t - hier ist der Konstruktor richtig
        MessageBox(ip->GetMAXHWnd(), m.data(),
                   _T("SWBF2 Import ") SWBF2IMPORT_VERSION_STR, MB_ICONINFORMATION);
    }
    return n > 0 ? 1 : 0;
}

// ------------------------------------------------------------
//  Das Figurenfenster mit Max verbinden.
//
//  Das Fenster (swbf2import_fenster.cpp) kennt kein SDK. Hier bekommt
//  es: Max' Theme-Farben, den Import ueber GENAU denselben Weg wie
//  Datei -> Importieren (ImportiereDatei), und das Protokoll.
// ------------------------------------------------------------
namespace {

uint32_t MaxFarbe(int welche) {
    return static_cast<uint32_t>(GetCustSysColor(welche));
}

bool MaxImportiere(const std::wstring& pfad, std::wstring& bericht) {
    return ImportiereDatei(pfad.c_str(), TRUE, &bericht) > 0;
}

void MaxProtokoll(const char* zeile) {
    LogRoh(zeile);
}

std::wstring LokaleAblage() {
    return swbf2ablage::Ordner();                         // 1.43.0
}

} // namespace

int OeffneFigurenFenster(const std::wstring& startOrdner) {
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) return -1;
    LogAuf();
    Log("SWBF2 Import %ls - Figurenfenster", SWBF2IMPORT_VERSION_STR);
    FensterBruecke b;
    b.hInstance = hInstance;
    b.eltern = ip->GetMAXHWnd();
    b.farbe = &MaxFarbe;
    b.importiere = &MaxImportiere;
    b.protokoll = &MaxProtokoll;
    b.ablage = LokaleAblage();
    b.version = SWBF2IMPORT_VERSION_STR;
    b.startOrdner = startOrdner;
    Log("Ablage: %ls", b.ablage.c_str());
    if (!startOrdner.empty()) Log("Spielordner vom Importer: %ls", startOrdner.c_str());
    const int r = ZeigeFigurenFenster(b);
    Log("Figurenfenster geschlossen (%d)", r);
    LogZu();
    return r;
}

// ------------------------------------------------------------
//  Der Eingang von Datei -> Importieren.
//
//  .fbmodel: der bekannte Import. .toc aus dem Spiel: das Figurenfenster
//  fuer genau dieses Spiel. Das ist ein zweiter Weg ins Fenster, der
//  nicht an MAXScript haengt - er funktioniert, solange die .dlu geladen
//  ist, auch wenn die Skriptschnittstelle einmal nicht erreichbar sein
//  sollte (das Menue nutzt ihn dann als Rueckweg).
// ------------------------------------------------------------
// ------------------------------------------------------------
//  Animation auf das Skelett legen (0.38.0).
//
//  Gemessen (0.37.1, outro_team1): Quaternion x,y,z,w; die Zeilen der
//  lokalen Lage sind die SPALTEN von R(q) (Median 8,3 bzw. 18,4 Grad zur
//  Ruhelage, alle anderen Lesarten 36 bis 177 Grad); Verschiebung im Raum
//  der Ruhelage, in Metern (Median-Abstand 0,0000 m).
//  Lokal bleibt lokal: der Import rechnet Welt_max = Welt_spiel * A, also
//  ist die lokale Lage jedes Kindes die des Spiels (nur die Verschiebung
//  mal Massstab); allein ein oberster Bone bekommt A.
//
//  Max-Regeln (Recherche 10.09.2026, SDK-Doku):
//   * Quat und AngAxis der API folgen der LINKE-Hand-Regel, die Oberflaeche
//     der rechten. Deshalb nie Komponenten kopieren, sondern ueber die
//     Matrix gehen: Quat(Matrix3) ist Max' eigene, stimmige Umrechnung.
//   * Keyframe-Controller speichern Drehkeys RELATIV zum vorigen Key. Also
//     nicht mit absoluten Werten ueber IKeyControl schreiben, sondern
//     Control::SetValue(CTRL_ABSOLUTE) im Animationsmodus - Max rechnet um.
//   * Linear-Controller fuer Drehung und Position: exakte Wiedergabe der
//     abgetasteten Spieldaten. Euler XYZ ist laut Doku nicht so glatt wie
//     Quaternionen und kippt; TCB schwingt zwischen den Keys.
//   * Neue Controller ersetzen die alten (Control::Set...Controller), damit
//     loescht jeder Clip den vorigen vollstaendig.
//   * 4800 Ticks je Sekunde; Bildrate aus dem ClipController (sonst 30).
// ------------------------------------------------------------
namespace {

Matrix3 LageZuMatrix(const fbebx::Lage& l) {
    Matrix3 m;
    m.SetRow(0, Point3(static_cast<float>(l.right[0]), static_cast<float>(l.right[1]), static_cast<float>(l.right[2])));
    m.SetRow(1, Point3(static_cast<float>(l.up[0]), static_cast<float>(l.up[1]), static_cast<float>(l.up[2])));
    m.SetRow(2, Point3(static_cast<float>(l.forward[0]), static_cast<float>(l.forward[1]), static_cast<float>(l.forward[2])));
    m.SetRow(3, Point3(static_cast<float>(l.trans[0]), static_cast<float>(l.trans[1]), static_cast<float>(l.trans[2])));
    return m;
}

// Drehung aus x,y,z,w: die Zeilen sind die Spalten von R(q) (gemessen).
void QuatInZeilen(const float* q, Matrix3& m) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double R[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                             { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                             { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
    for (int r = 0; r < 3; ++r)
        m.SetRow(r, Point3(static_cast<float>(R[0][r]), static_cast<float>(R[1][r]), static_cast<float>(R[2][r])));
}

void ZuMax(Matrix3 lokal, bool wurzel, const Matrix3& achsen, float mass, Quat& q, Point3& p) {
    if (wurzel) lokal = lokal * achsen;
    const Point3 t = lokal.GetRow(3);
    p = Point3(t.x * mass, t.y * mass, t.z * mass);
    lokal.SetRow(3, Point3(0.0f, 0.0f, 0.0f));
    q = Quat(lokal);
}

void SammleKnoten(INode* n, std::map<std::string, INode*>& aus) {
    if (n == nullptr) return;
    const MCHAR* name = n->GetName();
    if (name != nullptr) aus.emplace(fbdatei::Utf8(std::wstring(name)), n);
    for (int i = 0; i < n->NumberOfChildren(); ++i) SammleKnoten(n->GetChildNode(i), aus);
}

// Das Skelett: von der Auswahl (oder dem ersten bekannten Bone) zum
// obersten Vorfahren, dann alle Nachfahren nach Namen.
// Die Rigs in der Szene (0.48.0): oberste Bones mit swbf2_rig (seit 0.48.0)
// oder wenigstens swbf2_figur (aeltere Importe).
std::string UserProp(INode* n, const MCHAR* schluessel) {
    MSTR w;
    if (n == nullptr || !n->GetUserPropString(MSTR(schluessel), w) || w.data() == nullptr) return std::string();
    return fbdatei::Utf8(std::wstring(w.data()));
}

// win32/characters/hero/anakin/anakin_01/vur_anakin_01_bpb -> anakin (wie
// fbfigur::FigurSchluessel; hier eigen, damit das Plugin-Modul ohne fbfigur
// auskommt - die Vorabpruefung bindet es gegen die Attrappe).
std::string FigurAusQuelle(const std::string& quelle) {
    // 1.33.0: Fahrzeuge. Die Quelle heisst "vehicle: ground/at_te"; ohne diesen
    // Zweig lieferte die Rig-Liste eine LEERE Figur, ZielFigur gab sie an
    // RuheFuer weiter, und dort kam das Menschenskelett heraus. Messbar war das
    // an "Abweichung erster Key zur Ruhelage: Mittel 72,1 Grad ... (5 Spuren)"
    // - nur fuenf Namen trafen ueberhaupt, naemlich menschliche wie "Head".
    if (quelle.compare(0, 9, "vehicle: ") == 0) {
        std::string k = quelle.substr(9);
        const size_t s2 = k.find_last_of('/');
        if (s2 != std::string::npos) k = k.substr(s2 + 1);
        for (char& ch : k) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        return k;
    }
    const std::string marke = "characters/";
    const size_t a = quelle.find(marke);
    if (a == std::string::npos) return std::string();
    const size_t b = quelle.find('/', a + marke.size());
    if (b == std::string::npos) return std::string();
    const size_t c = quelle.find('/', b + 1);
    std::string k = quelle.substr(b + 1, (c == std::string::npos ? quelle.size() : c) - b - 1);
    for (char& ch : k) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return k;
}

std::vector<RigInfo> MaxRigs() {
    std::vector<RigInfo> aus;
    Interface* ip = GetCOREInterface();
    if (ip == nullptr || ip->GetRootNode() == nullptr) return aus;
    INode* w = ip->GetRootNode();
    for (int i = 0; i < w->NumberOfChildren(); ++i) {
        INode* n = w->GetChildNode(i);
        const std::string rig = UserProp(n, _T("swbf2_rig"));
        const std::string quelle = UserProp(n, _T("swbf2_figur"));
        if (rig.empty() && quelle.empty()) continue;
        RigInfo r;
        r.handle = n->GetHandle();
        r.name = !rig.empty() ? rig : fbdatei::Utf8(std::wstring(n->GetName()));
        r.figur = FigurAusQuelle(quelle);
        aus.push_back(r);
    }
    return aus;
}

bool SkelettKnoten(Interface* ip, const Ruhelagen& ruhe, std::map<std::string, INode*>& knoten, unsigned long rig = 0) {
    if (rig != 0) {
        // Gezielt (0.48.0): der oberste Bone des gewaehlten Rigs und NUR seine Hierarchie.
        INode* top = ip->GetINodeByHandle(rig);
        if (top == nullptr) return false;
        SammleKnoten(top, knoten);
        return !knoten.empty();
    }
    INode* start = ip->GetSelNodeCount() > 0 ? ip->GetSelNode(0) : nullptr;
    if (start == nullptr) {
        for (const auto& r : ruhe) {
            MSTR n = AusUtf8(r.first);
            start = ip->GetINodeByName(n.data());
            if (start != nullptr) break;
        }
    }
    if (start == nullptr) return false;
    while (start->GetParentNode() != nullptr && !start->GetParentNode()->IsRootNode()) start = start->GetParentNode();
    SammleKnoten(start, knoten);
    return true;
}

// Die Ruhelage der Szene: LocalPose des Skeletts, fuer die Bones der
// Basispose (Beipack am obersten Bone, 0.43.0) deren korrigierte Lage - sonst
// faellt das Gesicht in Clips ohne Gesichtskanaele zurueck in die Verzerrung.
Ruhelagen MitFigurPose(const Ruhelagen& ruhe, const std::map<std::string, INode*>& knoten) {
    Ruhelagen r = ruhe;
    for (const auto& kv : knoten) {
        INode* n = kv.second;
        if (n == nullptr || !(n->GetParentNode() == nullptr || n->GetParentNode()->IsRootNode())) continue;
        MSTR wert;
        if (!n->GetUserPropString(MSTR(_T("swbf2_pose")), wert) || wert.data() == nullptr || wert.data()[0] == 0) break;
        std::map<std::string, std::array<double, 12>> pose;
        if (!LiesPoseDatei(std::wstring(wert.data()), pose)) break;
        size_t ersetzt = 0;
        for (const auto& p : pose) {
            fbebx::Lage l;
            for (int i = 0; i < 3; ++i) {
                l.right[i] = p.second[static_cast<size_t>(i)];
                l.up[i] = p.second[static_cast<size_t>(3 + i)];
                l.forward[i] = p.second[static_cast<size_t>(6 + i)];
                l.trans[i] = p.second[static_cast<size_t>(9 + i)];
            }
            r[p.first] = l;
            ++ersetzt;
        }
        Log("ANIM Ruhelage: %zu Bones aus der Basispose der Figur", ersetzt);
        break;
    }
    return r;
}

struct Spur { const fbanim::Kanal* q = nullptr; const fbanim::Kanal* t = nullptr; };
struct SpurSatz {
    std::map<std::string, Spur> spuren;
    size_t skal = 0, floats = 0;
};

SpurSatz Spuren(const fbanim::Clip& c) {
    SpurSatz s;
    for (const fbanim::Kanal& k : c.kanaele) {
        const std::string& n = k.name;
        if (n.size() > 2 && n.compare(n.size() - 2, 2, ".q") == 0 && k.art == 'q') s.spuren[n.substr(0, n.size() - 2)].q = &k;
        else if (n.size() > 2 && n.compare(n.size() - 2, 2, ".t") == 0 && k.art == 't') s.spuren[n.substr(0, n.size() - 2)].t = &k;
        else if (n.size() > 2 && n.compare(n.size() - 2, 2, ".s") == 0) ++s.skal;
        else ++s.floats;
    }
    return s;
}

// Schutz: wie viele Bones der Animation stehen in der Szene? Unter 80 %
// passt der Clip nicht (ein Droiden- oder Fahrzeugclip auf einem Menschen).
// 1.00.0: Bisher mussten 80 % der Clip-Spuren im Skelett vorkommen. Das ist
// fuer Figuren richtig (248 Bones, die Clips treffen fast alle), scheitert aber
// bei Fahrzeugen und Teilszenen: die gemeinsamen Soldaten-Clips haben 100+
// Spuren, ein AT-TE oder eine Frontend-Figur nur wenige Dutzend Bones. Dann
// bleibt NICHTS uebrig ("No clip of the selection fits this skeleton").
// Jetzt zaehlt die andere Richtung mit: ein Clip passt auch, wenn er die
// MEISTEN Bones der Szene bedient (>= 60 %) - dann laeuft das, was da ist.
bool Passt(const SpurSatz& s, const std::map<std::string, INode*>& knoten, size_t& gefunden) {
    gefunden = 0;
    for (const auto& sp : s.spuren) if (knoten.count(sp.first)) ++gefunden;
    if (s.spuren.empty() || knoten.empty()) return false;
    if (gefunden * 10 >= s.spuren.size() * 8) return true;          // Clip fast ganz im Skelett
    return gefunden * 10 >= knoten.size() * 6 && gefunden >= 4;     // Skelett fast ganz im Clip
}

bool Bewegt(const Spur& sp) { return (sp.q != nullptr && !sp.q->konstant) || (sp.t != nullptr && !sp.t->konstant); }

// Lokale Lage bei Key i: Drehung und Verschiebung aus den Kanaelen, was fehlt
// aus der Ruhelage.
// additiv (0.65.0): viele DCT-/VBR-Clips speichern nur die Abweichung von der
// Ruhelage (unbewegte Knochen = Identitaet). Dann wird die Drehung auf die
// Ruhelage gesetzt (erst die Abweichung im Knochenraum, dann die Ruhelage) und
// die Verschiebung addiert.
Matrix3 LokalBei(const Matrix3& ruheM, const Spur& sp, size_t i, bool additiv) {
    Matrix3 m = ruheM;
    if (sp.q != nullptr) {
        const size_t o = sp.q->konstant ? 0 : 4 * i;
        if (o + 4 <= sp.q->werte.size()) {
            const Point3 t = m.GetRow(3);
            if (additiv) {
                Matrix3 delta;
                delta.IdentityMatrix();
                QuatInZeilen(&sp.q->werte[o], delta);
                delta.SetRow(3, Point3(0, 0, 0));
                Matrix3 ruheRot = ruheM;
                ruheRot.SetRow(3, Point3(0, 0, 0));
                m = delta * ruheRot;
            } else {
                QuatInZeilen(&sp.q->werte[o], m);
            }
            m.SetRow(3, t);
        }
    }
    if (sp.t != nullptr) {
        const size_t o = sp.t->konstant ? 0 : 3 * i;
        if (o + 3 <= sp.t->werte.size()) {
            const Point3 wert(sp.t->werte[o], sp.t->werte[o + 1], sp.t->werte[o + 2]);
            const Point3 r0 = ruheM.GetRow(3);
            m.SetRow(3, additiv ? Point3(r0.x + wert.x, r0.y + wert.y, r0.z + wert.z) : wert);
        }
    }
    return m;
}

int Bildrate(float fps) { return (fps > 0.5f) ? static_cast<int>(fps + 0.5f) : 30; }

struct AnimModus {          // RAII: Animationsmodus sicher an und wieder aus
    AnimModus() { AnimateOn(); }
    ~AnimModus() { AnimateOff(); }
};

// Neue Linear-Controller je Bone (die alten gibt Max frei) und die Ruhelage
// als Grundwert. Liefert false, wenn der Knoten keine Controller hat.
bool FrischeController(Interface* ip, INode* n, Control*& rot, Control*& pos) {
    Control* tmc = n->GetTMController();
    if (tmc == nullptr) return false;
    Control* r = static_cast<Control*>(ip->CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
    Control* p = static_cast<Control*>(ip->CreateInstance(CTRL_POSITION_CLASS_ID, Class_ID(LININTERP_POSITION_CLASS_ID, 0)));
    if (r != nullptr) tmc->SetRotationController(r);
    if (p != nullptr) tmc->SetPositionController(p);
    rot = tmc->GetRotationController();
    pos = tmc->GetPositionController();
    return rot != nullptr && pos != nullptr;
}

bool MaxWendeAn(const fbanim::Clip& c, float fps, const Ruhelagen& ruhe, unsigned long rig, std::wstring& bericht) {
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) { bericht = L"no 3ds Max interface"; return false; }
    std::map<std::string, INode*> knoten;
    if (!SkelettKnoten(ip, ruhe, knoten, rig)) { bericht = L"No skeleton in the scene - import a character first."; return false; }
    const Ruhelagen ruheP = MitFigurPose(ruhe, knoten);
    const SpurSatz ss = Spuren(c);
    size_t gefunden = 0;
    if (!Passt(ss, knoten, gefunden)) {
        char m[240];
        std::snprintf(m, sizeof m, ": does not fit this skeleton - only %zu of %zu animated bones exist in the scene. "
                      "Nothing changed.", gefunden, ss.spuren.size());
        bericht = fbdatei::Breit(c.name + m);
        Log("ANIM %s: passt nicht (%zu von %zu Bones), nichts veraendert", c.name.c_str(), gefunden, ss.spuren.size());
        return false;
    }

    // Bildrate: aus dem ClipController, sonst 30.
    const int bildrate = Bildrate(fps);
    if (bildrate > 0 && TIME_TICKSPERSEC % bildrate == 0 && GetFrameRate() != bildrate) SetFrameRate(bildrate);
    const double ticksJeBild = static_cast<double>(TIME_TICKSPERSEC) / static_cast<double>(bildrate);

    const Matrix3 achsen = AchsenMatrix();
    const float mass = Massstab();
    size_t bones = 0, mitKeys = 0, keys = 0, ohneSpur = 0, flips = 0;
    const size_t keyZahl = c.zeiten.size();
    theHold.Suspend();                 // ein Clip ist Datenimport, kein Undo-Schritt
    ip->DisableSceneRedraw();
    SuspendAnimate();
    for (const auto& kv : knoten) {
        const auto r = ruheP.find(kv.first);
        if (r == ruheP.end()) continue;
        INode* n = kv.second;
        Control* rot = nullptr;
        Control* pos = nullptr;
        if (!FrischeController(ip, n, rot, pos)) continue;
        ++bones;
        const bool wurzel = n->GetParentNode() == nullptr || n->GetParentNode()->IsRootNode();
        const Matrix3 ruheM = LageZuMatrix(r->second);
        Quat qR;
        Point3 pR;
        ZuMax(ruheM, wurzel, achsen, mass, qR, pR);
        rot->SetValue(0, &qR, 1, CTRL_ABSOLUTE);          // Grundwert: die Ruhelage
        pos->SetValue(0, &pR, 1, CTRL_ABSOLUTE);
        const auto sp = ss.spuren.find(kv.first);
        if (sp == ss.spuren.end()) { ++ohneSpur; continue; }
        if (!Bewegt(sp->second) || keyZahl == 0) {
            Quat q;
            Point3 p;
            ZuMax(LokalBei(ruheM, sp->second, 0, c.additiv), wurzel, achsen, mass, q, p);
            rot->SetValue(0, &q, 1, CTRL_ABSOLUTE);
            pos->SetValue(0, &p, 1, CTRL_ABSOLUTE);
            continue;
        }
        ++mitKeys;
        AnimModus modus;
        Quat vorher;
        for (size_t i = 0; i < keyZahl; ++i) {
            const TimeValue t = static_cast<TimeValue>(std::lround(static_cast<double>(c.zeiten[i]) * ticksJeBild));
            Quat q;
            Point3 p;
            ZuMax(LokalBei(ruheM, sp->second, i, c.additiv), wurzel, achsen, mass, q, p);
            if (i > 0 && (q.x * vorher.x + q.y * vorher.y + q.z * vorher.z + q.w * vorher.w) < 0.0f) {
                q = Quat(-q.x, -q.y, -q.z, -q.w);   // Vorzeichen fortlaufend halten
                ++flips;
            }
            vorher = q;
            rot->SetValue(t, &q, 1, CTRL_ABSOLUTE);
            pos->SetValue(t, &p, 1, CTRL_ABSOLUTE);
            ++keys;
        }
    }
    ResumeAnimate();
    const int ende = std::max(1, c.endFrame > 0 ? c.endFrame : static_cast<int>(keyZahl > 0 ? c.zeiten.back() : 1));
    ip->SetAnimRange(Interval(0, static_cast<TimeValue>(std::lround(ende * ticksJeBild))));
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->RedrawViews(ip->GetTime());

    char b[400];
    std::snprintf(b, sizeof b, ": %zu bones set, %zu with keys (%zu keys), %zu rest pose only; %zu key times, %d fps, "
                  "range 0 to %d; %zu scale and %zu float channels not yet; %zu sign flips",
                  bones, mitKeys, keys, ohneSpur, keyZahl, bildrate, ende, ss.skal, ss.floats, flips);
    bericht = fbdatei::Breit(c.name + b);
    Log("ANIM %s: %zu Bones, %zu mit Keys (%zu Keys), %zu nur Ruhelage, %d fps, Bereich 0..%d, %zu Vorzeichenwechsel",
        c.name.c_str(), bones, mitKeys, keys, ohneSpur, bildrate, ende, flips);
    return bones > 0;
}

// ------------------------------------------------------------
//  MAXScript aus C++ (fuer die Notizspur). Die Signatur hat sich mit
//  Max 2022 geaendert - dieselbe Weiche wie im XFBIN-Importer.
// ------------------------------------------------------------
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
bool FuehreSkript(const std::wstring& skript) {
    return ExecuteMAXScriptScript(skript.c_str(), MAXScript::ScriptSource::NonEmbedded, TRUE) != FALSE;
}
#else
bool FuehreSkript(const std::wstring& skript) {
    return ExecuteMAXScriptScript(skript.c_str(), TRUE) != FALSE;
}
#endif

std::wstring SkriptText(const std::string& s) {
    std::wstring w;
    for (wchar_t c : fbdatei::Breit(s)) {
        if (c == L'\\' || c == L'"') w += L'\\';
        w += c;
    }
    return w;
}

// ------------------------------------------------------------
//  Alle Clips als Folge in die Zeitleiste (0.39.0).
//
//  Nach dem Sequenzmodus des XFBIN-Importers (XfbinImport.mcr 2.1.6):
//   * Bild 0 haelt die Bindepose als Key, der erste Clip beginnt nach dem
//     Abstand, danach jeder weitere Clip nach Clipende + Abstand.
//   * Jede Sequenz steht fuer sich: ein Bone, den der Clip nicht bewegt,
//     bekommt an BEIDEN Enden einen Key (Ruhelage oder der konstante Wert
//     des Clips) - sonst interpoliert Max quer ueber den Abstand und der
//     vorige Clip laeuft in den naechsten hinein.
//   * Notizspur "animations" auf der SZENENWURZEL, nicht auf einem Bone -
//     dort suchen die Warcraft-3-Exporter, und dieselbe Form schreibt das
//     Animation Merge Tool: zwei Keys je Sequenz, am Anfang und am Ende,
//     beide mit dem Namen.
//   * Clips, die nicht zum Skelett passen, werden uebersprungen.
//   * Jeder Clip rechnet mit SEINER Bildrate in Ticks um; die Szene nimmt
//     die Rate des ersten Clips.
// ------------------------------------------------------------
bool MaxLiesSequenzen(std::vector<Sequenz>& aus);

bool MaxWendeAnFolge(const std::vector<fbanim::Clip>& clips, const std::vector<std::string>& namen, const std::vector<float>& fps,
                     int abstand, bool notizen, const Ruhelagen& ruhe, unsigned long rig, std::vector<Sequenz>& planAus,
                     std::wstring& bericht) {
    const auto t0 = std::chrono::steady_clock::now();
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) { bericht = L"no 3ds Max interface"; return false; }
    std::map<std::string, INode*> knoten;
    if (!SkelettKnoten(ip, ruhe, knoten, rig)) {
        bericht = L"No skeleton in the scene - import a character first.";
        Log("ANIM Folge abgebrochen: kein passendes Skelett in der Szene");          // 1.43.0: auch ins Protokoll
        return false;
    }
    const Ruhelagen ruheP = MitFigurPose(ruhe, knoten);

    std::vector<SpurSatz> spuren(clips.size());
    std::vector<size_t> passend;
    size_t unpassend = 0;
    for (size_t i = 0; i < clips.size(); ++i) {
        spuren[i] = Spuren(clips[i]);
        size_t gefunden = 0;
        if (Passt(spuren[i], knoten, gefunden)) passend.push_back(i);
        else ++unpassend;
    }
    // 1.28.0: Sagen, WELCHE Spuren nicht getroffen haben. Beim AT-TE bewegten
    // sich nur 8 von 129 Bones - so ist zu sehen, ob die Namen abweichen.
    // 1.32.0: Wie weit weicht der erste Key vom Ruhezustand ab? Bei einem
    // Steh- oder Laufclip sollten das wenige Grad sein. Grosse Werte heissen:
    // falsche Ruhelage, falsches Vorzeichen oder additiv/absolut vertauscht -
    // genau das, was den AT-TE kippen laesst.
    if (!clips.empty()) {
        // 1.41.0: in WELTLAGE vergleichen. Die lokale Messung (1.32.0-1.40.0)
        // meldete beim AT-AT "Hips 180 Grad" - dabei verteilen Skelett und
        // Clips dieselbe Drehung nur verschieden auf die Kette: in der
        // Ruhelage traegt Hips die 90 Grad (unter AITrajectory), in allen
        // absoluten Clips traegt sie AITrajectory, und Hips ist Identitaet
        // (gemessen mit castool --wurzel an ~300 Clips). Die Weltlage ist
        // gleich. Deshalb hier je Bone: lokal * Eltern-Welt, die Kette hinauf.
        const fbanim::Clip& c0 = clips[0];
        std::map<std::string, Matrix3> weltRuhe, weltKey;
        std::function<const Matrix3&(const std::string&, bool)> welt = [&](const std::string& name, bool key) -> const Matrix3& {
            std::map<std::string, Matrix3>& ziel = key ? weltKey : weltRuhe;
            const auto da = ziel.find(name);
            if (da != ziel.end()) return da->second;
            Matrix3 lokal;
            lokal.IdentityMatrix();
            const auto rl = ruhe.find(name);
            if (rl != ruhe.end()) {
                lokal = LageZuMatrix(rl->second);
                const auto sp = spuren[0].spuren.find(name);
                if (key && sp != spuren[0].spuren.end()) lokal = LokalBei(lokal, sp->second, 0, c0.additiv);
            }
            ziel[name] = lokal;                                 // vorlaeufig, gegen Kreise
            const auto kn = knoten.find(name);
            INode* eltern = (kn != knoten.end() && kn->second != nullptr) ? kn->second->GetParentNode() : nullptr;
            if (eltern != nullptr && !eltern->IsRootNode() && eltern->GetName() != nullptr) {
                const std::string en = fbdatei::Utf8(std::wstring(eltern->GetName()));
                if (knoten.count(en)) { const Matrix3 w = lokal * welt(en, key); ziel[name] = w; }
            }
            return ziel[name];
        };
        auto winkel = [](const Matrix3& a, const Matrix3& b) {
            double spur = 0.0;
            for (int r = 0; r < 3; ++r) {
                const Point3 x = a.GetRow(r), y = b.GetRow(r);
                spur += static_cast<double>(x.x) * y.x + static_cast<double>(x.y) * y.y + static_cast<double>(x.z) * y.z;
            }
            double c = (spur - 1.0) / 2.0;
            if (c > 1.0) c = 1.0;
            if (c < -1.0) c = -1.0;
            return std::acos(c) * 180.0 / 3.14159265358979;
        };
        double summe = 0, groesste = 0;
        size_t gezaehlt = 0;
        std::string schlimmste;
        std::vector<std::pair<double, std::string>> liste;
        for (const auto& sp : spuren[0].spuren) {
            if (knoten.find(sp.first) == knoten.end() || ruhe.find(sp.first) == ruhe.end()) continue;
            const double grad = winkel(welt(sp.first, false), welt(sp.first, true));
            summe += grad;
            ++gezaehlt;
            if (grad > groesste) { groesste = grad; schlimmste = sp.first; }
            liste.push_back({ grad, sp.first });
        }
        std::sort(liste.rbegin(), liste.rend());
        for (size_t k = 0; k < liste.size() && k < 3; ++k) Log("ANIM Ausreisser %zu (Weltlage): %-28s %6.1f Grad", k + 1, liste[k].second.c_str(), liste[k].first);
        if (gezaehlt > 0)
            Log("ANIM Abweichung erster Key zur Ruhelage (Weltlage): Mittel %.1f Grad, groesste %.1f Grad bei %s (%zu Spuren, additiv %s, Codec %s, Clip %s)",
                summe / static_cast<double>(gezaehlt), groesste, schlimmste.c_str(), gezaehlt, c0.additiv ? "ja" : "nein", c0.codec.c_str(),
                namen.empty() ? c0.name.c_str() : namen[0].c_str());
        // Die Wurzelkette des ersten Clips in Zahlen (Spieldaten, x y z w).
        for (const char* w : { "Reference", "AITrajectory", "Trajectory", "Connect", "Hips" }) {
            const auto sp = spuren[0].spuren.find(w);
            if (sp == spuren[0].spuren.end() || sp->second.q == nullptr || sp->second.q->werte.size() < 4) continue;
            const std::vector<float>& q = sp->second.q->werte;
            Log("ANIM Wurzel %-12s erster Key q %.3f %.3f %.3f %.3f%s", w, q[0], q[1], q[2], q[3], sp->second.q->konstant ? " (konstant)" : "");
        }
    }
    if (!clips.empty()) {
        std::vector<std::string> fehlen;
        size_t getroffen = 0;
        for (const auto& sp : spuren[0].spuren) {
            if (knoten.count(sp.first)) ++getroffen;
            else if (fehlen.size() < 12) fehlen.push_back(sp.first);
        }
        std::string z;
        for (const std::string& x : fehlen) z += " " + x;
        Log("ANIM Spuren des ersten Clips: %zu, davon in der Szene %zu; ohne Knoten:%s", spuren[0].spuren.size(), getroffen, z.c_str());
    }
    if (passend.empty()) {
        // 1.00.0: sagen, WARUM - die beste Trefferzahl und die Groesse beider Seiten.
        size_t beste = 0, besteSpuren = 0;
        for (size_t i = 0; i < clips.size(); ++i) {
            size_t g = 0;
            for (const auto& sp : spuren[i].spuren) if (knoten.count(sp.first)) ++g;
            if (g > beste) { beste = g; besteSpuren = spuren[i].spuren.size(); }
        }
        bericht = L"No clip of the selection fits this skeleton - nothing changed. Best match: " + std::to_wstring(beste) +
                  L" of " + std::to_wstring(besteSpuren) + L" clip tracks, scene has " + std::to_wstring(knoten.size()) + L" bones.";
        Log("ANIM Folge abgebrochen: kein Clip passt zum Skelett (bester %zu von %zu Spuren, Szene %zu Bones)", beste, besteSpuren, knoten.size());   // 1.43.0
        return false;
    }

    // Anhaengen (0.48.0): stehen schon Sequenzen in der Notizspur, kommen die
    // neuen HINTER die letzte - dieselbe Zeitleiste, das Rig bleibt stehen.
    std::vector<Sequenz> alt;
    MaxLiesSequenzen(alt);
    TimeValue altEnde = 0;
    for (const Sequenz& sq : alt) altEnde = std::max<TimeValue>(altEnde, static_cast<TimeValue>(sq.ende));
    const bool anhaengen = !alt.empty();
    const int bildrate = anhaengen ? GetFrameRate() : Bildrate(fps[passend[0]]);
    if (!anhaengen && bildrate > 0 && TIME_TICKSPERSEC % bildrate == 0 && GetFrameRate() != bildrate) SetFrameRate(bildrate);
    const TimeValue tpf = static_cast<TimeValue>(TIME_TICKSPERSEC / std::max(1, bildrate));

    // Zeitplan in Ticks.
    struct Platz { TimeValue start = 0, ende = 0; double ticksJeBild = 160.0; };
    std::vector<Platz> plan;
    TimeValue at = (anhaengen ? altEnde : 0) + static_cast<TimeValue>(abstand) * tpf;
    for (size_t i : passend) {
        const fbanim::Clip& c = clips[i];
        Platz pl;
        pl.ticksJeBild = static_cast<double>(TIME_TICKSPERSEC) / static_cast<double>(Bildrate(fps[i]));
        const double endeBild = c.endFrame > 0 ? static_cast<double>(c.endFrame)
                                               : (c.zeiten.empty() ? 1.0 : static_cast<double>(c.zeiten.back()));
        const TimeValue laenge = std::max(tpf, static_cast<TimeValue>(std::lround(endeBild * pl.ticksJeBild)));
        pl.start = at;
        pl.ende = at + laenge;
        plan.push_back(pl);
        at = pl.ende + static_cast<TimeValue>(abstand) * tpf;
    }

    const Matrix3 achsen = AchsenMatrix();
    const float mass = Massstab();
    size_t bones = 0, keys = 0, flips = 0;
    theHold.Suspend();
    ip->DisableSceneRedraw();
    SuspendAnimate();
    for (const auto& kv : knoten) {
        const auto r = ruheP.find(kv.first);
        if (r == ruheP.end()) continue;
        INode* n = kv.second;
        Control* rot = nullptr;
        Control* pos = nullptr;
        // Beim Anhaengen die vorhandenen Controller samt Keys behalten; nur
        // Bones ohne Keys bekommen frische Linear-Controller und die Bindepose.
        bool behalten = false;
        if (anhaengen && n->GetTMController() != nullptr) {
            Control* r0 = n->GetTMController()->GetRotationController();
            Control* p0 = n->GetTMController()->GetPositionController();
            if (r0 != nullptr && p0 != nullptr && (r0->NumKeys() > 0 || p0->NumKeys() > 0)) { rot = r0; pos = p0; behalten = true; }
        }
        if (!behalten && !FrischeController(ip, n, rot, pos)) continue;
        ++bones;
        const bool wurzel = n->GetParentNode() == nullptr || n->GetParentNode()->IsRootNode();
        const Matrix3 ruheM = LageZuMatrix(r->second);
        Quat qR;
        Point3 pR;
        ZuMax(ruheM, wurzel, achsen, mass, qR, pR);
        if (!behalten) {
            rot->SetValue(0, &qR, 1, CTRL_ABSOLUTE);
            pos->SetValue(0, &pR, 1, CTRL_ABSOLUTE);
        }

        AnimModus modus;
        Quat vorher = qR;
        auto setze = [&](TimeValue t, Quat q, Point3 p) {
            if ((q.x * vorher.x + q.y * vorher.y + q.z * vorher.z + q.w * vorher.w) < 0.0f) {
                q = Quat(-q.x, -q.y, -q.z, -q.w);
                ++flips;
            }
            vorher = q;
            rot->SetValue(t, &q, 1, CTRL_ABSOLUTE);
            pos->SetValue(t, &p, 1, CTRL_ABSOLUTE);
            ++keys;
        };
        if (!behalten) setze(0, qR, pR);                                     // Bindepose bei Bild 0
        for (size_t nr = 0; nr < passend.size(); ++nr) {
            const fbanim::Clip& c = clips[passend[nr]];
            const Platz& pl = plan[nr];
            const auto sp = spuren[passend[nr]].spuren.find(kv.first);
            Quat q;
            Point3 p;
            if (sp == spuren[passend[nr]].spuren.end() || !Bewegt(sp->second) || c.zeiten.empty()) {
                // Ruhe-Keys an beiden Enden.
                const Matrix3 m = (sp == spuren[passend[nr]].spuren.end()) ? ruheM : LokalBei(ruheM, sp->second, 0, c.additiv);
                ZuMax(m, wurzel, achsen, mass, q, p);
                setze(pl.start, q, p);
                setze(pl.ende, q, p);
                continue;
            }
            const size_t zahl = c.zeiten.size();
            if (c.zeiten.front() > 0.0f) {                    // Startwert, falls der erste Key spaeter liegt
                ZuMax(LokalBei(ruheM, sp->second, 0, c.additiv), wurzel, achsen, mass, q, p);
                setze(pl.start, q, p);
            }
            TimeValue letzte = pl.start;
            for (size_t i = 0; i < zahl; ++i) {
                const TimeValue t = pl.start + static_cast<TimeValue>(std::lround(static_cast<double>(c.zeiten[i]) * pl.ticksJeBild));
                if (t > pl.ende) break;
                ZuMax(LokalBei(ruheM, sp->second, i, c.additiv), wurzel, achsen, mass, q, p);
                setze(t, q, p);
                letzte = t;
            }
            if (letzte < pl.ende) {                           // Endwert halten
                ZuMax(LokalBei(ruheM, sp->second, zahl - 1, c.additiv), wurzel, achsen, mass, q, p);
                setze(pl.ende, q, p);
            }
        }
    }
    ResumeAnimate();
    ip->SetAnimRange(Interval(0, std::max(std::max(at, altEnde), tpf)));
    ip->EnableSceneRedraw();
    theHold.Resume();

    const double keySekunden = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // Notizspur ueber die C++-API (0.41.0) statt MAXScript: NewDefaultNoteTrack,
    // je Sequenz zwei NoteKeys (Anfang, Ende, beide mit dem Namen), auf die
    // SZENENWURZEL - dieselbe Form wie XFBIN und das Animation Merge Tool. Der
    // Name einer Notizspur ist laut Doku nur fuer MAXScript da und in Max
    // nicht sichtbar. Danach zurueckgelesen: wie viele Spuren und Keys stehen
    // wirklich auf der Wurzel.
    size_t notizKeys = 0, notizSpuren = 0;
    if (notizen) {
        INode* wurzelKnoten = ip->GetRootNode();
        if (wurzelKnoten != nullptr) {
            // Anhaengen: Keys in die vorhandene erste Spur; sonst frisch.
            DefNoteTrack* nt = nullptr;
            bool neueSpur = true;
            if (anhaengen && wurzelKnoten->NumNoteTracks() > 0) {
                nt = static_cast<DefNoteTrack*>(wurzelKnoten->GetNoteTrack(0));
                neueSpur = false;
            } else {
                while (wurzelKnoten->NumNoteTracks() > 0) wurzelKnoten->DeleteNoteTrack(wurzelKnoten->GetNoteTrack(0), TRUE);
                nt = static_cast<DefNoteTrack*>(NewDefaultNoteTrack());
            }
            if (nt != nullptr) {
                for (size_t nr = 0; nr < passend.size(); ++nr) {
                    const MSTR text = AusUtf8(namen[passend[nr]]);
                    NoteKey* k0 = new NoteKey(plan[nr].start, text, 0);
                    NoteKey* k1 = new NoteKey(plan[nr].ende, text, 0);
                    nt->keys.Append(1, &k0);
                    nt->keys.Append(1, &k1);
                }
                if (neueSpur) wurzelKnoten->AddNoteTrack(nt);
            }
            notizSpuren = static_cast<size_t>(wurzelKnoten->NumNoteTracks());
            for (int i2 = 0; i2 < wurzelKnoten->NumNoteTracks(); ++i2) {
                DefNoteTrack* d2 = static_cast<DefNoteTrack*>(wurzelKnoten->GetNoteTrack(i2));
                if (d2 != nullptr) notizKeys += static_cast<size_t>(d2->keys.Count());
            }
        }
    }
    // Custom Attributes wie WhiteoutDex (0.43.0): "NeoDexSequenceData" auf der
    // Szenenwurzel - Namen, Anfangs- und Endbild je Sequenz. Dieselbe
    // Definition wie in WhiteoutDexGlobals.ms (Parameter in derselben
    // Reihenfolge), damit WhiteoutDex und sein Exporter sie lesen. Ist
    // WhiteoutDex geladen, wird seine Definition (::NeoDexSequenceCA) benutzt.
    size_t caZahl = 0, caNotiz = 0;
    bool caOk = false;
    if (notizen) {
        const std::wstring status = swbf2ablage::Ordner() + L"\\sequenzen.txt";   // 1.43.0
        std::wstring namenL, startL, endeL, nlL, rarL, spL, extL, grL;
        // Alle Sequenzen: die schon vorhandenen (Notizspur) und die neuen.
        std::vector<std::string> caN;
        std::vector<TimeValue> caA, caE;
        for (const Sequenz& sq : alt) { caN.push_back(sq.name); caA.push_back(static_cast<TimeValue>(sq.start)); caE.push_back(static_cast<TimeValue>(sq.ende)); }
        for (size_t nr = 0; nr < passend.size(); ++nr) { caN.push_back(namen[passend[nr]]); caA.push_back(plan[nr].start); caE.push_back(plan[nr].ende); }
        for (size_t nr = 0; nr < caN.size(); ++nr) {
            const wchar_t* k = nr ? L"," : L"";
            namenL += k + (L"\"" + SkriptText(caN[nr]) + L"\"");
            startL += k + std::to_wstring(caA[nr] / std::max<TimeValue>(1, tpf));
            endeL += k + std::to_wstring(caE[nr] / std::max<TimeValue>(1, tpf));
            nlL += k + std::wstring(L"false");
            rarL += k + std::wstring(L"0.0");
            spL += k + std::wstring(L"0.0");
            extL += k + std::wstring(L"\"\"");
            grL += k + std::wstring(L"\"\"");
        }
        std::wstring sk =
            L"(\n local ca = undefined\n try (ca = ::NeoDexSequenceCA) catch (ca = undefined)\n"
            L" if (ca == undefined) do ca = attributes \"NeoDexSequenceData\" (\n  parameters main (\n"
            L"   seqNames type:#stringTab tabSizeVariable:true\n   startFrames type:#intTab tabSizeVariable:true\n"
            L"   endFrames type:#intTab tabSizeVariable:true\n   nonLooping type:#boolTab tabSizeVariable:true\n"
            L"   rarity type:#floatTab tabSizeVariable:true\n   moveSpeed type:#floatTab tabSizeVariable:true\n"
            L"   seqExtents type:#stringTab tabSizeVariable:true\n   sharedGroup type:#stringTab tabSizeVariable:true\n  )\n )\n"
            L" for i = custAttributes.count rootNode to 1 by -1 do if (custAttributes.get rootNode i).name == \"NeoDexSequenceData\" do custAttributes.delete rootNode i\n"
            L" custAttributes.add rootNode ca\n"
            L" rootNode.seqNames = #(" + namenL + L")\n rootNode.startFrames = #(" + startL + L")\n rootNode.endFrames = #(" + endeL +
            L")\n rootNode.nonLooping = #(" + nlL + L")\n rootNode.rarity = #(" + rarL + L")\n rootNode.moveSpeed = #(" + spL +
            L")\n rootNode.seqExtents = #(" + extL + L")\n rootNode.sharedGroup = #(" + grL + L")\n"
            L" local f = createFile \"" + SkriptText(fbdatei::Utf8(status)) + L"\"\n"
            L" format \"CA %\\n\" rootNode.seqNames.count to:f\n"
            L" format \"NOTIZ %\\n\" (if (numNoteTracks rootNode) > 0 then (getNoteTrack rootNode 1).keys.count else 0) to:f\n"
            L" close f\n OK\n)\n";
        _wremove(status.c_str());
        caOk = FuehreSkript(sk);
        std::vector<uint8_t> roh;
        std::string f2;
        if (fbdatei::LiesAlles(fbdatei::Utf8(status), roh, f2)) {
            const std::string t(roh.begin(), roh.end());
            std::sscanf(t.c_str(), "CA %zu\nNOTIZ %zu", &caZahl, &caNotiz);
        }
    }
    planAus = alt;                                            // vorhandene Sequenzen zuerst
    for (size_t nr = 0; nr < passend.size(); ++nr) {
        Sequenz sq;
        sq.name = namen[passend[nr]];
        sq.start = plan[nr].start;
        sq.ende = plan[nr].ende;
        sq.startBild = static_cast<double>(plan[nr].start) / static_cast<double>(tpf);
        sq.endeBild = static_cast<double>(plan[nr].ende) / static_cast<double>(tpf);
        planAus.push_back(sq);
    }
    ip->RedrawViews(ip->GetTime());

    const int bilder = static_cast<int>(at / std::max<TimeValue>(1, tpf));
    char b[400];
    std::snprintf(b, sizeof b, "%zu clips in the timeline (gap %d frames), %zu skipped (do not fit); %zu bones, %zu keys in %.1f s, "
                  "%d fps, range 0 to %d; note track %zu keys, sequence attributes %zu",
                  passend.size(), abstand, unpassend, bones, keys, keySekunden, bildrate, bilder, notizKeys, caZahl);
    bericht = fbdatei::Breit(b);
    Log("ANIM Folge: %zu Clips, %zu uebersprungen, %zu Bones, %zu Keys in %.2f s (%.0f Keys/s), %d fps, Bereich 0..%d, "
        "Notizspur %s: %zu Spur(en), %zu Keys zurueckgelesen (Soll %zu), %zu Vorzeichenwechsel",
        passend.size(), unpassend, bones, keys, keySekunden, keySekunden > 0 ? keys / keySekunden : 0.0, bildrate, bilder,
        notizen ? "an" : "aus", notizSpuren, notizKeys, notizen ? 2 * (alt.size() + passend.size()) : 0, flips);
    if (notizen) Log("ANIM Custom Attributes NeoDexSequenceData: MAXScript %s, %zu Sequenzen zurueckgelesen (Soll %zu); Notizspur laut MAXScript %zu Keys",
                     caOk ? "ok" : "FEHLGESCHLAGEN", caZahl, alt.size() + passend.size(), caNotiz);
    Log("ANIM Folge %s: %zu vorhandene Sequenzen, %zu neue ab Bild %d, Rig-Handle %lu", anhaengen ? "ANGEHAENGT" : "neu",
        alt.size(), passend.size(), plan.empty() ? 0 : static_cast<int>(plan[0].start / std::max<TimeValue>(1, tpf)), rig);
    return bones > 0;
}

// Sequenzen aus den Notizspuren der Szenenwurzel: je zwei aufeinander
// folgende Keys mit demselben Text sind eine Sequenz (Anfang, Ende).
bool MaxLiesSequenzen(std::vector<Sequenz>& aus) {
    aus.clear();
    Interface* ip = GetCOREInterface();
    if (ip == nullptr || ip->GetRootNode() == nullptr) return false;
    INode* w = ip->GetRootNode();
    const double tpf = static_cast<double>(std::max(1, GetTicksPerFrame()));
    for (int i = 0; i < w->NumNoteTracks(); ++i) {
        DefNoteTrack* nt = static_cast<DefNoteTrack*>(w->GetNoteTrack(i));
        if (nt == nullptr) continue;
        const int n = nt->keys.Count();
        for (int k = 0; k + 1 < n; ) {
            NoteKey* a = nt->keys[k];
            NoteKey* b = nt->keys[k + 1];
            if (a != nullptr && b != nullptr && std::wstring(a->note.data()) == std::wstring(b->note.data())) {
                Sequenz sq;
                sq.name = fbdatei::Utf8(std::wstring(a->note.data()));
                sq.start = a->time;
                sq.ende = b->time;
                sq.startBild = a->time / tpf;
                sq.endeBild = b->time / tpf;
                aus.push_back(sq);
                k += 2;
            } else {
                ++k;
            }
        }
    }
    Log("ANIM Sequenzen aus der Notizspur der Szenenwurzel: %zu", aus.size());
    return !aus.empty();
}

// Einen Bereich zeigen: Zeitbereich darauf, Zeitschieber an den Anfang.
void MaxZeigeBereich(int start, int ende) {
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) return;
    if (ende <= start) ende = start + std::max(1, GetTicksPerFrame());
    ip->SetAnimRange(Interval(start, ende));
    ip->SetTime(start, TRUE);
}

void AnimProtokoll(const char* zeile) { LogRoh(zeile); }
uint32_t AnimFarbe(int welche) { return static_cast<uint32_t>(GetCustSysColor(welche)); }

} // namespace

// Welche Figur steht in der Szene? Erst der Vermerk am obersten Bone (ab
// 0.38.1 beim Import gesetzt), sonst aus den Meshnamen geschaetzt.
namespace {

// Knotennamen der Szene OHNE das Skelett: Bonenamen wie FACIAL_L_Eye
// kaemen sonst hundertfach vor und verdraengten den Figurennamen.
void SammleNamen(INode* n, INode* ohne, std::vector<std::string>& aus, int tiefe) {
    if (n == nullptr || n == ohne || tiefe > 64) return;
    if (!n->IsRootNode() && n->GetName() != nullptr) aus.push_back(fbdatei::Utf8(std::wstring(n->GetName())));
    for (int i = 0; i < n->NumberOfChildren(); ++i) SammleNamen(n->GetChildNode(i), ohne, aus, tiefe + 1);
}

std::string FigurSchluessel(const std::string& quelle) {
    // 1.10.0: Fahrzeuge tragen ihren Namen direkt ("vehicle: ground/at_te").
    // Ohne das hielt das Animationsfenster den AT-TE fuer die Figur "left",
    // weil es aus den Knotennamen riet.
    if (quelle.compare(0, 9, "vehicle: ") == 0) {
        const std::string rest = quelle.substr(9);
        const size_t s2 = rest.find_last_of('/');
        return s2 == std::string::npos ? rest : rest.substr(s2 + 1);
    }
    // win32/characters/hero/anakin/anakin_01/vur_anakin_01_bpb -> anakin
    std::vector<std::string> teile;
    size_t a = 0;
    for (size_t i = 0; i <= quelle.size(); ++i) {
        if (i == quelle.size() || quelle[i] == '/') { teile.push_back(quelle.substr(a, i - a)); a = i + 1; }
    }
    for (size_t i = 0; i + 2 < teile.size(); ++i) {
        if (teile[i] == "characters") {
            std::string k = teile[i + 2];
            for (char& c : k) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return k;
        }
    }
    return std::string();
}

void FigurInSzene(Interface* ip, std::string& quelle, std::string& schluessel, std::string& herkunft) {
    INode* start = ip->GetSelNodeCount() > 0 ? ip->GetSelNode(0) : nullptr;
    if (start == nullptr) start = ip->GetINodeByName(_T("Reference"));
    if (start != nullptr) {
        while (start->GetParentNode() != nullptr && !start->GetParentNode()->IsRootNode()) start = start->GetParentNode();
        MSTR wert;
        if (start->GetUserPropString(MSTR(_T("swbf2_figur")), wert) && wert.data() != nullptr && wert.data()[0] != 0) {
            quelle = fbdatei::Utf8(std::wstring(wert.data()));
            schluessel = FigurSchluessel(quelle);
            herkunft = std::string("Vermerk am Bone ") + fbdatei::Utf8(std::wstring(start->GetName() != nullptr ? start->GetName() : _T("?")));
            if (!schluessel.empty()) return;
        }
    }
    // 1.30.0: Den Vermerk in der GANZEN Szene suchen, nicht nur am obersten
    // Bone der Auswahl. Beim AT-TE heisst kein Knoten "Reference", und wenn
    // nichts ausgewaehlt ist, gab es keinen Startpunkt - die Erkennung riet
    // dann "left" aus den Knotennamen, und damit kam die Ruhelage wieder vom
    // Menschenskelett.
    {
        std::vector<INode*> offen;
        INode* wurzel = ip->GetRootNode();
        for (int i = 0; wurzel != nullptr && i < wurzel->NumberOfChildren(); ++i) offen.push_back(wurzel->GetChildNode(i));
        size_t besucht = 0;
        while (!offen.empty() && besucht < 4000) {
            INode* n = offen.back();
            offen.pop_back();
            ++besucht;
            if (n == nullptr) continue;
            MSTR wert;
            if (n->GetUserPropString(MSTR(_T("swbf2_figur")), wert) && wert.data() != nullptr && wert.data()[0] != 0) {
                const std::string q = fbdatei::Utf8(std::wstring(wert.data()));
                const std::string k = FigurSchluessel(q);
                if (!k.empty()) {
                    quelle = q;
                    schluessel = k;
                    herkunft = std::string("Vermerk am Knoten ") + fbdatei::Utf8(std::wstring(n->GetName() != nullptr ? n->GetName() : _T("?")));
                    return;
                }
            }
            for (int i = 0; i < n->NumberOfChildren(); ++i) offen.push_back(n->GetChildNode(i));
        }
    }
    // Rueckfall fuer Szenen aus aelteren Fassungen: das haeufigste Wort in
    // den Knotennamen, das kein Allgemeinwort ist (anakin_01_mesh_lod0_s0_M_body,
    // heads_anakin_01_..., hair_anakin_01_... -> anakin).
    std::vector<std::string> namen;
    SammleNamen(ip->GetRootNode(), start, namen, 0);
    std::map<std::string, size_t> zahl;
    static const char* allgemein[] = { "mesh", "lod0", "lod1", "lod2", "lod3", "hair", "heads", "head", "body", "glove", "hand",
                                       "skirt", "sleeves", "flaps", "eyes", "teeth", "lambert1", "cap", "hairplanes", "reference",
                                       "mat", "bpb", "vur", "facial", nullptr };
    for (const std::string& n : namen) {
        std::string w;
        for (size_t i = 0; i <= n.size(); ++i) {
            const char c = i < n.size() ? n[i] : '_';
            if (c == '_' || c == '.' || c == ' ') {
                bool zif = !w.empty();
                for (char z : w) if (z < '0' || z > '9') zif = false;
                if (w.size() >= 3 && !zif && !(w[0] == 's' && w.size() <= 3)) {
                    std::string k = w;
                    for (char& x : k) if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
                    bool frei = true;
                    for (int j = 0; allgemein[j] != nullptr; ++j) if (k == allgemein[j]) frei = false;
                    if (frei) ++zahl[k];
                }
                w.clear();
            } else {
                w += c;
            }
        }
    }
    size_t beste = 0;
    for (const auto& kv : zahl) if (kv.second > beste) { beste = kv.second; schluessel = kv.first; }
    if (beste < 3) schluessel.clear();                   // zu unsicher
    herkunft = schluessel.empty() ? "keine Figur erkannt" : "aus den Knotennamen geschaetzt";
}

} // namespace

int OeffneAnimFenster() {
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) return -1;
    LogAuf();
    Log("SWBF2 Import %ls - Animationsfenster", SWBF2IMPORT_VERSION_STR);
    AnimBruecke b;
    b.hInstance = hInstance;
    b.eltern = ip->GetMAXHWnd();
    b.farbe = &AnimFarbe;
    b.protokoll = &AnimProtokoll;
    b.wendeAn = &MaxWendeAn;
    b.rigs = &MaxRigs;
    b.wendeAnFolge = &MaxWendeAnFolge;
    b.liesSequenzen = &MaxLiesSequenzen;
    b.zeigeBereich = &MaxZeigeBereich;
    b.ablage = swbf2ablage::Ordner();                     // 1.43.0
    b.version = SWBF2IMPORT_VERSION_STR;
    FigurInSzene(ip, b.figur, b.figurSchluessel, b.figurHerkunft);
    {
        // 1.43.0: Mesh-Namen der Szene fuer die Profilwahl im Fenster
        std::vector<INode*> offen;
        INode* wurzel = ip->GetRootNode();
        for (int i = 0; wurzel != nullptr && i < wurzel->NumberOfChildren(); ++i) offen.push_back(wurzel->GetChildNode(i));
        size_t besucht = 0;
        while (!offen.empty() && besucht < 4000) {
            INode* n = offen.back();
            offen.pop_back();
            ++besucht;
            if (n == nullptr) continue;
            const ObjectState os = n->EvalWorldState(ip->GetTime());
            if (os.obj != nullptr && os.obj->SuperClassID() == GEOMOBJECT_CLASS_ID && os.obj->ClassID() != BONE_OBJ_CLASSID && n->GetName() != nullptr) {
                std::string s = fbdatei::Utf8(std::wstring(n->GetName()));
                for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                b.szeneMeshes += s + "|";
            }
            for (int i = 0; i < n->NumberOfChildren(); ++i) offen.push_back(n->GetChildNode(i));
        }
    }
    Log("ANIM Figur in der Szene: %s (%s)%s%s", b.figurSchluessel.empty() ? "-" : b.figurSchluessel.c_str(), b.figurHerkunft.c_str(),
        b.figur.empty() ? "" : ", Quelle ", b.figur.c_str());
    const int r = ZeigeAnimFenster(b);
    Log("Animationsfenster geschlossen (%d)", r);
    LogZu();
    return r;
}

#ifndef IMPEXP_FAIL
#define IMPEXP_FAIL    0
#endif
#ifndef IMPEXP_SUCCESS
#define IMPEXP_SUCCESS 1
#endif
#ifndef IMPEXP_CANCEL
#define IMPEXP_CANCEL  2
#endif

int ImportiereEingang(const MCHAR* pfad, BOOL ohneRueckfragen) {
    if (pfad == nullptr) return IMPEXP_FAIL;
    const std::wstring p(pfad);
    // Endung ohne _wcsicmp pruefen - das kennt nur die Microsoft-Laufzeit,
    // und die Vorabpruefung laeuft auch ohne sie.
    auto klein = [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c; };
    const bool istToc = p.size() >= 4 && klein(p[p.size() - 4]) == L'.' && klein(p[p.size() - 3]) == L't' &&
                        klein(p[p.size() - 2]) == L'o' && klein(p[p.size() - 1]) == L'c';
    if (!istToc) return ImportiereDatei(pfad, ohneRueckfragen);
    const std::wstring ordner = SpielordnerAusDatei(p);
    if (ohneRueckfragen || ordner.empty()) {
        LogAuf();
        Log("SWBF2 Import %ls", SWBF2IMPORT_VERSION_STR);
        Log(ordner.empty() ? "ABBRUCH: %ls liegt in keinem Spielordner (kein Data\\layout.toc darueber)"
                           : "ABBRUCH: %ls - das Figurenfenster braucht Rueckfragen (#noPrompt)", pfad);
        LogZu();
        return IMPEXP_FAIL;
    }
    const int r = OeffneFigurenFenster(ordner);
    return r > 0 ? IMPEXP_SUCCESS : IMPEXP_CANCEL;
}

} // namespace swbf2
