#pragma once
// Attrappe fuer control.h (im echten SDK von istdplug.h eingebunden).
#include "max.h"

enum GetSetMethod { CTRL_RELATIVE, CTRL_ABSOLUTE };

class Control : public Animatable {
public:
    virtual void SetValue(TimeValue, void*, int = 1, GetSetMethod = CTRL_ABSOLUTE) {}
    virtual BOOL SetPositionController(Control*) { return TRUE; }
    virtual BOOL SetRotationController(Control*) { return TRUE; }
    virtual Control* GetPositionController() { return nullptr; }
    virtual int NumKeys() { return 0; }
    virtual Control* GetRotationController() { return nullptr; }
};
