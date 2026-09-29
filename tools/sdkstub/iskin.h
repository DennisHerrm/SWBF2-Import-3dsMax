#pragma once
// Attrappe fuer iskin.h: nur die Teile, die der Import benutzt.
#include "max.h"
#define I_SKIN           0x00010000
#define I_SKINIMPORTDATA 0x00020000
#define SKIN_CLASSID     Class_ID(9815843, 87654)
class ISkinContextData {
public:
    virtual ~ISkinContextData() {}
    int GetNumPoints() { return 0; }
    int GetNumAssignedBones(int) { return 0; }
    int GetAssignedBone(int, int) { return 0; }
    float GetBoneWeight(int, int) { return 0.0f; }
};
class ISkin {
public:
    virtual ~ISkin() {}
    int GetNumBones() { return 0; }
    INode* GetBone(int) { return nullptr; }
    ISkinContextData* GetContextInterface(INode*) { return nullptr; }
};
class ISkinImportData {
public:
    virtual ~ISkinImportData() {}
    BOOL AddBoneEx(INode*, BOOL) { return TRUE; }
    BOOL AddWeights(INode*, int, Tab<INode*>&, Tab<float>&) { return TRUE; }
};
