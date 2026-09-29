#pragma once
// Attrappe fuer stdmat.h: Standardmaterial und Bitmap-Textur.
#include "max.h"
#include "bitmap.h"
#define ID_DI 1
#define ID_OP 6
#define ID_BU 8
class StdMat2 : public Mtl {
public:
    virtual long StdIDToChannel(long id) { return id; }
    virtual void EnableMap(int, BOOL) {}
    virtual void SetTexmapAmt(int, float, int) {}
    virtual void SetTwoSided(BOOL) {}
};
class BitmapTex : public Texmap {
public:
    virtual void SetMapName(const MCHAR*, bool = false) {}
    virtual void SetBitmapInfo(const BitmapInfo&) {}
};
inline StdMat2* NewDefaultStdMat() { return nullptr; }
inline BitmapTex* NewDefaultBitmapTex() { return nullptr; }
