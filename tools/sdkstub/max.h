// ============================================================
//  sdkstub - eine ATTRAPPE des Max-SDK, nur zum Uebersetzen.
//
//  Warum das hier liegt: das echte SDK gibt es nur auf einem
//  Windows-Rechner mit installiertem 3ds Max. Ohne Gegenprobe
//  faellt jeder Tippfehler und jede falsche Signatur erst beim
//  Bau auf - und das kostet jedes Mal eine Runde hin und her.
//  Genau das ist zweimal passiert (fehlender Includepfad,
//  MSTR aus char*).
//
//  Diese Attrappe bildet NUR die Teile des SDK nach, die das
//  Plugin benutzt, und zwar mit denselben Signaturen. Damit
//  laesst sich der Quelltext auf jedem Rechner uebersetzen.
//
//  WAS SIE NICHT LEISTET: sie beweist nicht, dass das Plugin in
//  Max laeuft. Sie faengt Tippfehler, falsche Typen und falsche
//  Argumentzahlen - mehr nicht. Der echte Bau bleibt die
//  eigentliche Probe.
// ============================================================
#pragma once
#include <vector>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

// ---- Windows-Grundtypen ------------------------------------
typedef int            BOOL;
typedef unsigned long  ULONG;
typedef unsigned int   UINT;
typedef void*          HINSTANCE;
typedef void*          HWND;
typedef void*          LPVOID;
typedef unsigned long  DWORD;
typedef wchar_t        WCHAR;
typedef wchar_t        TCHAR;
typedef wchar_t        MCHAR;

#define TRUE  1
#define FALSE 0
#define WINAPI
#define DLL_PROCESS_ATTACH 1
#define MB_ICONERROR       0x10
#define MB_ICONINFORMATION 0x40
#define _T(x) L##x

inline int MessageBox(HWND, const MCHAR*, const MCHAR*, UINT) { return 0; }
inline BOOL DisableThreadLibraryCalls(HINSTANCE) { return TRUE; }
#include "win_shim.h"   // _wfopen, nur fuer die Vorabpruefung

#define _sntprintf swprintf

#ifndef __declspec
#define __declspec(x)
#endif

// ---- Zeichenketten -----------------------------------------
class WStr {
public:
    WStr() {}
    WStr(const wchar_t* s) : s_(s ? s : L"") {}
    WStr(const WStr& o) : s_(o.s_) {}
    WStr& operator=(const WStr& o) { s_ = o.s_; return *this; }
    // Seit Max 2013 gibt es KEINEN Konstruktor aus char* mehr.
    // Die Attrappe laesst ihn deshalb ebenfalls weg - sonst
    // wuerde sie genau den Fehler durchlassen, den sie fangen soll.
    static WStr FromUTF8(const char* s) {
        WStr w;
        if (s) { while (*s) { w.s_ += static_cast<wchar_t>(static_cast<unsigned char>(*s)); ++s; } }
        return w;
    }
    static WStr FromACP(const char* s) { return FromUTF8(s); }
    const wchar_t* data() const { return s_.c_str(); }
    int length() const { return static_cast<int>(s_.size()); }
private:
    std::wstring s_;
};
typedef WStr MSTR;
typedef WStr TSTR;

// ---- Mathematik --------------------------------------------
class Point3 {
public:
    float x = 0, y = 0, z = 0;
    Point3() {}
    Point3(float a, float b, float c) : x(a), y(b), z(c) {}
};

class Matrix3 {
public:
    Matrix3() { IdentityMatrix(); }
    explicit Matrix3(BOOL) { IdentityMatrix(); }
    void IdentityMatrix() {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 3; ++c) m_[r][c] = (r == c) ? 1.0f : 0.0f;
    }
    void SetRow(int i, const Point3& p) { m_[i][0] = p.x; m_[i][1] = p.y; m_[i][2] = p.z; }
    Point3 GetRow(int i) const { return Point3(m_[i][0], m_[i][1], m_[i][2]); }
    Matrix3 operator*(const Matrix3& b) const {
        Matrix3 r;
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < 3; ++k) {
                float v = 0.0f;
                for (int j = 0; j < 3; ++j) v += m_[i][j] * b.m_[j][k];
                if (i == 3) v += b.m_[3][k];
                r.m_[i][k] = v;
            }
        return r;
    }
private:
    float m_[4][3];
};

// maxtypes.h
typedef int TimeValue;
#define TIME_TICKSPERSEC 4800

// quat.h: Quaternion (Linke-Hand-Regel der API); Quat(Matrix3) nimmt die Drehung.
class Quat {
public:
    float x = 0, y = 0, z = 0, w = 1;
    Quat() {}
    Quat(float X, float Y, float Z, float W) : x(X), y(Y), z(Z), w(W) {}
    explicit Quat(const Matrix3&) {}
};
// interval.h
class Interval {
public:
    Interval() {}
    Interval(TimeValue s, TimeValue e) : s_(s), e_(e) {}
    TimeValue Start() const { return s_; }
    TimeValue End() const { return e_; }
private:
    TimeValue s_ = 0, e_ = 0;
};
// maxapi.h: Bildrate und Ticks
inline int GetFrameRate() { return 30; }
inline void SetFrameRate(int) {}
inline int GetTicksPerFrame() { return 160; }
class Control;

// ---- Klassen-IDs -------------------------------------------
class Class_ID {
public:
    Class_ID() {}
    Class_ID(unsigned long a, unsigned long b) : a_(a), b_(b) {}
    bool operator==(const Class_ID& o) const { return a_ == o.a_ && b_ == o.b_; }
private:
    unsigned long a_ = 0, b_ = 0;
};
typedef unsigned long SClass_ID;
// plugapi.h: Oberklassen und Klassen-IDs der Standard-Controller
#define CTRL_POSITION_CLASS_ID ((SClass_ID)0x9004)
#define CTRL_ROTATION_CLASS_ID ((SClass_ID)0x9005)
#define LININTERP_POSITION_CLASS_ID 0x2002
#define LININTERP_ROTATION_CLASS_ID 0x2003

#define GEOMOBJECT_CLASS_ID   ((SClass_ID)0x00000010)
#define SCENE_IMPORT_CLASS_ID ((SClass_ID)0x00000A00)
#define UTILITY_CLASS_ID      ((SClass_ID)0x00000FF0)
#define BONE_OBJ_CLASS_ID     0x8a63c0
#define BONE_OBJ_CLASSID      Class_ID(BONE_OBJ_CLASS_ID, 0)
#define HELPER_CLASS_ID       ((SClass_ID)0x00000FC0)
#define POINTHELP_CLASS_ID    0x00000004
#define VERSION_3DSMAX        ((ULONG)29000)
#ifndef MAX_RELEASE
#define MAX_RELEASE           29000
#endif

// ---- Einheiten ----------------------------------------------
#define UNITS_INCHES      0
#define UNITS_FEET        1
#define UNITS_MILES       2
#define UNITS_MILLIMETERS 3
#define UNITS_CENTIMETERS 4
#define UNITS_METERS      5
#define UNITS_KILOMETERS  6
// Beide Namen, damit die Attrappe beide Zweige uebersetzen kann.
inline double GetMasterScale(int) { return 0.0254; }
inline double GetSystemUnitScale(int) { return 0.0254; }

// ---- Parameterbloecke ---------------------------------------
class Texmap;
class IParamBlock2 {
public:
    virtual ~IParamBlock2() {}
    int SetValue(int, int, float) { return 1; }
    int SetValue(int, int, Texmap*, int = 0) { return 1; }
};

template <class T> class Tab {
public:
    int Append(int n, T* el, int = 0) { for (int i = 0; i < n; ++i) v_.push_back(el[i]); return static_cast<int>(v_.size()) - n; }
    int Count() const { return static_cast<int>(v_.size()); }
    T& operator[](int i) { return v_[static_cast<size_t>(i)]; }
private:
    std::vector<T> v_;
};

class Animatable {
public:
    virtual ~Animatable() {}
    virtual void* GetInterface(ULONG) { return nullptr; }
    virtual IParamBlock2* GetParamBlockByID(int) { return nullptr; }
    virtual IParamBlock2* GetParamBlock(int) { return nullptr; }
};

// ---- Szene --------------------------------------------------
class Object;

class INode {
public:
    virtual ~INode() {}
    void SetName(const MCHAR*) {}
    const MCHAR* GetName() const { return L"stub"; }
    // Bis Max 2021 nimmt SetNodeTM eine NICHT-konstante Referenz (C2664 bei
    // DH mit 0.43.0), ab 2022 geht auch const.
#if defined(MAX_RELEASE) && (MAX_RELEASE < 24000)
    void SetNodeTM(int, Matrix3&) {}
#else
    void SetNodeTM(int, const Matrix3&) {}
#endif
    Matrix3 GetNodeTM(int) const { return Matrix3(); }
    void AttachChild(INode*, int = 1) {}
    INode* GetParentNode() const { return nullptr; }
    BOOL IsRootNode() const { return FALSE; }
    void ShowBone(int) {}
    void SetBoneNodeOnOff(BOOL, int) {}
    void SetBoneAutoAlign(BOOL) {}
    void SetBoneFreezeLen(BOOL) {}
    void SetRenderable(BOOL) {}
    Object* GetObjectRef() { return nullptr; }
    void SetObjectRef(Object*) {}
    void SetMtl(class Mtl*) {}
    int EvalWorldState(int, BOOL = TRUE) { return 0; }
    Control* GetTMController() { return nullptr; }
    ULONG GetHandle() { return 0; }
    // Animatable (Notizspuren)
    int NumNoteTracks() { return 0; }
    class NoteTrack* GetNoteTrack(int) { return nullptr; }
    void DeleteNoteTrack(class NoteTrack*, BOOL = TRUE) {}
    void AddNoteTrack(class NoteTrack*) {}
    void SetUserPropString(const MSTR&, const MSTR&) {}
    BOOL GetUserPropString(const MSTR&, MSTR&) { return FALSE; }
    int NumberOfChildren() { return 0; }
    INode* GetChildNode(int) { return nullptr; }
};

// ---- Mesh ---------------------------------------------------
class Face {
public:
    DWORD v[3] = {0,0,0};
    DWORD smGroup = 0;
    void setVerts(int a, int b, int c) { v[0]=a; v[1]=b; v[2]=c; }
    void setEdgeVisFlags(int, int, int) {}
    void setSmGroup(DWORD g) { smGroup = g; }
    void setMatID(unsigned short) {}
};
class TVFace {
public:
    DWORD t[3] = {0,0,0};
    void setTVerts(int a, int b, int c) { t[0]=a; t[1]=b; t[2]=c; }
};
class Mesh {
public:
    Face*   faces = nullptr;
    TVFace* tvFace = nullptr;
    void setNumVerts(int) {}
    void setNumFaces(int) {}
    void setVert(int, const Point3&) {}
    void setVert(int, float, float, float) {}
    void setNumTVerts(int) {}
    void setTVert(int, float, float, float) {}
    void setNumTVFaces(int) {}
    // 1.26.0: zweiter UV-Satz als Map-Kanal (im echten SDK in Mesh)
    void setMapSupport(int, BOOL) {}
    void setNumMapVerts(int, int) {}
    void setMapVert(int, int, const Point3&) {}
    void setNumMapFaces(int, int) {}
    TVFace* mapFaces(int) { static TVFace f[3]; return f; }
    void InvalidateGeomCache() {}
    void InvalidateTopologyCache() {}
    void buildNormals() {}
};

class Object : public Animatable {
public:
    Object* FindBaseObject() { return this; }
};
class TriObject : public Object {
public:
    Mesh& GetMesh() { return mesh_; }
private:
    Mesh mesh_;
};
inline TriObject* CreateNewTriObject() { return nullptr; }
#define OSM_CLASS_ID ((SClass_ID)0x00000810)
#define TEXMAP_CLASS_ID ((SClass_ID)0x00000c10)
// imtl.h (von max.h eingebunden): Materialien und Texturen.
#define MTL_TEX_DISPLAY_ENABLED (1 << 3)
class MtlBase : public Animatable {
public:
    void SetName(const MSTR&) {}
    void SetMtlFlag(int, BOOL = TRUE) {}
};
class Texmap : public MtlBase {};
class Mtl : public MtlBase {
public:
    virtual void SetSubTexmap(int, Texmap*) {}
};
class Modifier : public Animatable {};
class ModContext;
// Wie im echten SDK (inode.h, maxapi.h): hier NUR vorwaerts erklaert. Die
// Klasse selbst und CreateDerivedObject stehen in modstack.h. 0.34.0 hatte
// beides in dieser Attrappe von max.h - deshalb fiel das fehlende
// #include <modstack.h> erst bei DH auf, in allen zwoelf Jahrgaengen.
class IDerivedObject;

class ImpInterface { public: virtual ~ImpInterface() {} };

class Interface {
public:
    virtual ~Interface() {}
    void* CreateInstance(SClass_ID, Class_ID) { return nullptr; }
    INode* CreateObjectNode(Object*) { return nullptr; }
    INode* GetRootNode() { return nullptr; }
    HWND GetMAXHWnd() { return nullptr; }
    int GetTime() { return 0; }
    void RedrawViews(int) {}
    int GetSelNodeCount() { return 0; }
    INode* GetSelNode(int) { return nullptr; }
    INode* GetINodeByName(const MCHAR*) { return nullptr; }
    INode* GetINodeByHandle(ULONG) { return nullptr; }
    void SetAnimRange(Interval) {}
    void SetTime(TimeValue, BOOL = TRUE) {}
    void DisableSceneRedraw() {}
    void EnableSceneRedraw() {}
    virtual void PutMtlToMtlEditor(MtlBase*, int = -1) {}
    void ActivateTexture(MtlBase*, Mtl*, int = -1) {}
};
inline Interface* GetCOREInterface() { return nullptr; }

// Interface7 (maxapi.h) und GetCOREInterface7 (GetCOREInterface.h, von
// max.h eingebunden): AddModifier ist der dokumentierte Weg fuer Modifikatoren.
class InterfaceMtl {
public:
    virtual ~InterfaceMtl() {}
};
class Interface7 : public Interface {
public:
    enum ResCode { kRES_INTERNAL_ERROR = -3, kRES_MOD_NOT_FOUND = -2, kRES_MOD_NOT_APPLICABLE = -1, kRES_SUCCESS = 0 };
    virtual ResCode AddModifier(INode&, Modifier&, int = 0) { return kRES_SUCCESS; }
};
inline Interface7* GetCOREInterface7() { return nullptr; }
// Interface13 (maxapi.h): Modus des Material-Editors.
class Interface13 : public Interface7 {
public:
    enum MtlDlgMode { mtlDlgMode_Basic = 0, mtlDlgMode_Advanced = 1 };
    virtual int GetMtlDlgMode() { return 0; }
    virtual void SetMtlDlgMode(int) {}
    virtual BOOL IsMtlDlgShowing(int) { return FALSE; }
    virtual void OpenMtlDlg(int) {}
};
inline Interface13* GetCOREInterface13() { return nullptr; }

// ---- Animationsschalter -------------------------------------
inline void SuspendAnimate() {}
inline void ResumeAnimate() {}
inline void AnimateOn() {}
inline void AnimateOff() {}
inline BOOL Animating() { return FALSE; }

struct HoldStub {
    void Suspend() {}
    void Resume() {}
    void Begin() {}
    void Accept(const MCHAR*) {}
    void Cancel() {}
};
extern HoldStub theHold;

// ---- Pluginhuellen ------------------------------------------
class ClassDesc {
public:
    virtual ~ClassDesc() {}
};

class ClassDesc2 : public ClassDesc {
public:
    virtual int IsPublic() { return 0; }
    virtual void* Create(BOOL loading = FALSE) { (void)loading; return nullptr; }
    virtual const MCHAR* ClassName() { return L""; }
    virtual const MCHAR* NonLocalizedClassName() { return L""; }
    virtual SClass_ID SuperClassID() { return 0; }
    virtual Class_ID ClassID() { return Class_ID(); }
    virtual const MCHAR* Category() { return L""; }
    virtual const MCHAR* InternalName() { return L""; }
    virtual HINSTANCE HInstance() { return nullptr; }
};

class SceneImport {
public:
    virtual ~SceneImport() {}
    virtual int ExtCount() { return 0; }
    virtual const MCHAR* Ext(int) { return L""; }
    virtual const MCHAR* LongDesc() { return L""; }
    virtual const MCHAR* ShortDesc() { return L""; }
    virtual const MCHAR* AuthorName() { return L""; }
    virtual const MCHAR* CopyrightMessage() { return L""; }
    virtual const MCHAR* OtherMessage1() { return L""; }
    virtual const MCHAR* OtherMessage2() { return L""; }
    virtual unsigned int Version() { return 0; }
    virtual void ShowAbout(HWND) {}
    virtual int DoImport(const MCHAR*, ImpInterface*, Interface*, BOOL) { return 0; }
};
