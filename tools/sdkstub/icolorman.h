#pragma once
// Attrappe: im echten SDK sind das Makros ueber ColorMan()->CustSysColor().
#include "max.h"
inline DWORD GetCustSysColor(int) { return 0; }
inline void* GetCustSysColorBrush(int) { return nullptr; }
