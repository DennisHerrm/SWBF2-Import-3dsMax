#pragma once
// Attrappe fuer modstack.h - hier und NUR hier wie im echten SDK.
#include "max.h"
class IDerivedObject : public Object {
public:
    virtual void AddModifier(Modifier*, ModContext* = nullptr, int = 0) {}
    virtual int NumModifiers() { return 0; }
    virtual Modifier* GetModifier(int) { return nullptr; }
};
inline IDerivedObject* CreateDerivedObject(Object* = nullptr) { return nullptr; }
