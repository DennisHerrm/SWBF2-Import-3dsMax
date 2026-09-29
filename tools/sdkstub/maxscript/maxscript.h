#pragma once
// Attrappe fuer maxscript/maxscript.h: ExecuteMAXScriptScript.
// Bis Max 2021 (MAX_RELEASE < 24000): (const MCHAR*, BOOL quietErrors, ...).
// Ab 2022: an zweiter Stelle MAXScript::ScriptSource - den Namensraum gab es
// vorher nicht. Die Attrappe bildet beide Staende ab, damit ein falscher
// Aufruf im jeweiligen Jahrgang auffaellt.
#include "../max.h"
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
namespace MAXScript { enum class ScriptSource { Embedded, NonEmbedded, Dynamic, NotSpecified }; }
inline BOOL ExecuteMAXScriptScript(const MCHAR*, MAXScript::ScriptSource, BOOL = FALSE, void* = nullptr, BOOL = TRUE) { return TRUE; }
#else
inline BOOL ExecuteMAXScriptScript(const MCHAR*, BOOL = FALSE, void* = nullptr) { return TRUE; }

#endif

// Die echte maxscript/kernel/value.h legt ein GLOBALES "ok" an (MAXScripts
// Rueckgabewert). Ein lokales "ok" im Plugin verdeckt es - MSVC meldet C4459
// (0.45.0 bei DH, 47-mal). Die Attrappe bildet das nach; PRUEFE_QUELLTEXT.sh
// uebersetzt das Plugin mit -Wshadow.
extern int ok;
