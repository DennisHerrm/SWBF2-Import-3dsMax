#pragma once
// Attrappe fuer bitmap.h: BitmapInfo mit eigener Gamma.
#include "max.h"
#define BMM_CUSTOM_GAMMA ((DWORD)(1 << 0))
class BitmapInfo {
public:
    void SetName(const MCHAR*) {}
    void SetCustomGamma(float) {}
    void SetCustomFlag(DWORD) {}
};
