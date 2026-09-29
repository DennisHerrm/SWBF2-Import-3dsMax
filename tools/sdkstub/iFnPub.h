#pragma once
// Attrappe fuer Function Publishing - nur so viel, dass Namen, Typen und
// Argumentzahlen der Plugin-Schnittstelle geprueft werden. Die Makros
// pruefen vor allem: jede Funktion aus FUNCTION_MAP existiert und laesst
// sich ohne Argumente aufrufen (FN_0).
#include "max.h"
#include <cstdarg>
class Interface_ID {
public:
    Interface_ID(unsigned long a = 0, unsigned long b = 0) : a_(a), b_(b) {}
    unsigned long PartA() const { return a_; }
    unsigned long PartB() const { return b_; }
private:
    unsigned long a_, b_;
};
typedef int StringResID;
class FPInterface { public: virtual ~FPInterface() {} };
class FPInterfaceDesc : public FPInterface {};
class FPStaticInterface : public FPInterfaceDesc {};
#define FP_CORE 0x0004
enum { TYPE_BOOL = 1, TYPE_STRING = 2, TYPE_INT = 3 };
enum { p_end = -1 };
#define DECLARE_DESCRIPTOR(cls) \
public: \
    cls() {} \
    cls(Interface_ID, const MCHAR*, StringResID, ClassDesc*, unsigned short, ...) {}
#define BEGIN_FUNCTION_MAP public: int StubDispatch(int fid) { switch (fid) {
#define FN_0(fid, rtype, fn) case fid: (void)fn(); return 0;
#define END_FUNCTION_MAP } return -1; }
inline FPInterface* GetCOREInterface(Interface_ID) { return nullptr; }
inline void RegisterCOREInterface(FPInterface*) {}
inline int NumCOREInterfaces() { return 0; }
