#pragma once
#include "max.h"
#define NOTIFY_SYSTEM_STARTUP 1
inline int RegisterNotification(void (*)(void*, void*), void*, int) { return 1; }
inline int UnRegisterNotification(void (*)(void*, void*), void*, int) { return 1; }
