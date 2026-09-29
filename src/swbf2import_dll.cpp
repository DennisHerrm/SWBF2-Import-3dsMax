// ============================================================
//  SWBF2 Import - DLL-Einstieg
//
//  Zu den Exporten: die Funktionen sind NICHT extern "C",
//  deshalb exportiert __declspec(dllexport) den dekorierten
//  Namen. 3ds Max sucht den undekorierten. Die .def-Datei sorgt
//  dafuer, dass beide sauber herauskommen - sie darf nicht
//  entfernt werden, auch wenn es doppelt gemoppelt aussieht.
// ============================================================
#include "swbf2import.h"

#include <notify.h>
#include <iFnPub.h>

HINSTANCE hInstance = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

// ------------------------------------------------------------
//  Eintrag in Max' eigenem Import-Dialog
// ------------------------------------------------------------
class SWBF2SceneImport : public SceneImport {
public:
    // .fbmodel: Modelldump aus fbtools. .toc: eine Datei aus dem Spiel (am
    // besten Data\layout.toc) - oeffnet das Figurenfenster fuer dieses Spiel.
    int ExtCount() override           { return 2; }
    const MCHAR* Ext(int i) override  { return (i == 0) ? _T("fbmodel") : (i == 1 ? _T("toc") : _T("")); }

    const MCHAR* ShortDesc() override { return _T("Star Wars Battlefront II"); }
    const MCHAR* LongDesc() override {
        return _T("SWBF2 (2017): model dump (.fbmodel) or game (Data\\layout.toc -> character window)");
    }
    const MCHAR* AuthorName() override       { return _T("DennisH"); }
    const MCHAR* CopyrightMessage() override { return _T(""); }
    const MCHAR* OtherMessage1() override    { return _T(""); }
    const MCHAR* OtherMessage2() override    { return _T(""); }

    unsigned int Version() override   { return SWBF2IMPORT_VERSION; }
    void ShowAbout(HWND hWnd) override {
        MessageBox(hWnd,
                   _T("SWBF2 Import ") SWBF2IMPORT_VERSION_STR _T("\n\n")
                   _T("Characters straight from the game: menu EAfront Tool.\n")
                   _T("Model dumps (.fbmodel): File -> Import.\n")
                   _T("Skeleton, meshes, skinning, materials, weapons and animations."),
                   _T("SWBF2 Import"), MB_ICONINFORMATION);
    }

    int DoImport(const MCHAR* name, ImpInterface*, Interface*,
                 BOOL suppressPrompts) override {
        return swbf2::ImportiereEingang(name, suppressPrompts);
    }
};

class SWBF2SceneImportClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }

    // --------------------------------------------------------
    //  new, NICHT ein statisches Objekt.
    //
    //  Das Singleton-Muster der Autodesk-Doku gilt fuer
    //  UTILITY-Plugins. Bei SceneImport legt Max eine Instanz an,
    //  fragt ExtCount, Ext und die Beschreibungen ab und gibt den
    //  Speicher danach wieder frei. Ein statisches Objekt zu
    //  loeschen zerlegt den Heap - genau daran ist der
    //  XFBIN-Importer in Max 2027 beim blossen OEFFNEN des
    //  Import-Dialogs abgestuerzt.
    // --------------------------------------------------------
    void* Create(BOOL) override { return new SWBF2SceneImport(); }

    const MCHAR* ClassName() override { return _T("SWBF2 Import"); }
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    const MCHAR* NonLocalizedClassName() override { return _T("SWBF2 Import"); }
#endif
    SClass_ID SuperClassID() override { return SCENE_IMPORT_CLASS_ID; }
    Class_ID  ClassID() override      { return SWBF2IMPORT_SCENE_CLASS_ID; }
    const MCHAR* Category() override     { return _T("Import"); }
    const MCHAR* InternalName() override { return _T("SWBF2SceneImport"); }
    HINSTANCE HInstance() override       { return hInstance; }
};

static SWBF2SceneImportClassDesc theSceneImportClassDesc;

// ------------------------------------------------------------
//  MAXScript-Schnittstelle "Swbf2Cpp"
//
//      Swbf2Cpp.showDialog()   das Figurenfenster oeffnen
//      Swbf2Cpp.version()      Fassung der .dlu, fuer den Abgleich
//      Swbf2Cpp.showAnimDialog()  das Animationsfenster oeffnen (0.38.0)
//                              mit dem Skript (Lehre aus BAF 1.3.1)
//
//  Bis 0.33.0 hiess sie "SWBF2Import" und war in DHs Max nicht
//  erreichbar. Der Name glich dem Klassennamen "SWBF2 Import" bis auf das
//  Leerzeichen, und MAXScript macht aus dem Klassennamen selbst einen
//  globalen Namen (SDK, "Class Descriptors"). Ob das die Ursache war, ist
//  NICHT bewiesen - die Diagnose im MacroScript sagt es beim naechsten
//  Mal. Der neue Name folgt BafCpp und XfbinCpp, die beide laufen.
//
//  Eine Core-Schnittstelle meldet sich schon durch ihre
//  Deskriptor-Instanz an (SDK, "Core Interface Management").
//  Drei Stellen muessen zusammenpassen: das enum, FUNCTION_MAP und die
//  Eintraege im Deskriptor. Die Interface_ID ist einmalig gezogen und
//  bleibt - sie steht in Skripten.
// ------------------------------------------------------------
#define SWBF2IMPORT_FP_ID Interface_ID(0x6e2b7c41, 0x3d9a15f8)

class SWBF2ImportFP : public FPStaticInterface {
public:
    enum { fn_showDialog = 0, fn_version = 1, fn_showAnimDialog = 2 };

    BOOL showDialog() { return swbf2::OeffneFigurenFenster() > 0 ? TRUE : FALSE; }
    const MCHAR* version() { return SWBF2IMPORT_VERSION_STR; }
    BOOL showAnimDialog() { return swbf2::OeffneAnimFenster() > 0 ? TRUE : FALSE; }

    DECLARE_DESCRIPTOR(SWBF2ImportFP)
    BEGIN_FUNCTION_MAP
        FN_0(fn_showDialog, TYPE_BOOL, showDialog)
        FN_0(fn_version, TYPE_STRING, version)
        FN_0(fn_showAnimDialog, TYPE_BOOL, showAnimDialog)
    END_FUNCTION_MAP
};

static SWBF2ImportFP theSWBF2ImportFP(
    SWBF2IMPORT_FP_ID, _T("Swbf2Cpp"), 0, &theSceneImportClassDesc, FP_CORE,
    SWBF2ImportFP::fn_showDialog, _T("showDialog"), 0, TYPE_BOOL, 0, 0,
    SWBF2ImportFP::fn_version, _T("version"), 0, TYPE_STRING, 0, 0,
    SWBF2ImportFP::fn_showAnimDialog, _T("showAnimDialog"), 0, TYPE_BOOL, 0, 0,
    p_end);

__declspec(dllexport) const TCHAR* LibDescription() {
    return _T("SWBF2 Import ") SWBF2IMPORT_VERSION_STR
           _T(" - Star Wars Battlefront II (2017) Importer");
}
__declspec(dllexport) int        LibNumberClasses() { return 1; }
__declspec(dllexport) ClassDesc* LibClassDesc(int i) {
    return (i == 0) ? &theSceneImportClassDesc : nullptr;
}
__declspec(dllexport) ULONG LibVersion()   { return VERSION_3DSMAX; }

// ------------------------------------------------------------
//  LibInitialize - optional, Max ruft sie nach dem Laden (SDK: auch mehrfach,
//  also nur einmal arbeiten). Hier wird GEMESSEN, ob das Kerninterface
//  Swbf2Cpp beim Laden wirklich angemeldet wurde - in 0.33.0 und 0.33.1 war
//  es aus MAXScript nicht erreichbar, obwohl die .dlu geladen war (die
//  Importer-Klasse stand in importerPlugin.classes). Fehlt die Anmeldung,
//  wird sie mit RegisterCOREInterface nachgeholt (SDK, "Core Interface
//  Management"). Beides steht in start.log, das START.bat einsammelt.
// ------------------------------------------------------------
__declspec(dllexport) int LibInitialize() {
    static bool erledigt = false;
    if (erledigt) return TRUE;
    erledigt = true;
    FPInterface* const unseres = &theSWBF2ImportFP;
    FPInterface* const vorher = GetCOREInterface(SWBF2IMPORT_FP_ID);
    std::wstring text = std::wstring(L"SWBF2Import ") + SWBF2IMPORT_VERSION_STR +
                        L", gebaut fuer MAX_RELEASE " + std::to_wstring(MAX_RELEASE) + L"\r\n";
    text += L"Kerninterface Swbf2Cpp beim Laden: ";
    text += (vorher == unseres) ? L"angemeldet" : (vorher != nullptr ? L"ANDERES Objekt mit derselben ID" : L"NICHT angemeldet");
    text += L"\r\n";
    if (vorher == nullptr) {
        RegisterCOREInterface(unseres);
        FPInterface* const nachher = GetCOREInterface(SWBF2IMPORT_FP_ID);
        text += L"  nachgeholt mit RegisterCOREInterface: ";
        text += (nachher == unseres) ? L"ok" : L"gescheitert";
        text += L"\r\n";
    }
    text += L"Kerninterfaces insgesamt: " + std::to_wstring(NumCOREInterfaces()) + L"\r\n";
    swbf2::SchreibeStartprotokoll(hInstance, text);
    return TRUE;
}
__declspec(dllexport) int   CanAutoDefer() { return FALSE; }
