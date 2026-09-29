// Nur damit die Attrappe ein vollstaendiges Programm ergibt.
#include "max.h"
HoldStub theHold;
int main() { return 0; }

// Das Fenster ist reines Win32 und wird mit MinGW gegen echte Windows-Header
// geprueft (tools/PRUEFE_FENSTER.sh). Hier genuegt ein Platzhalter, damit die
// Attrappe binden kann.
#include "swbf2import_fenster.h"
namespace swbf2 {
int ZeigeFigurenFenster(const FensterBruecke&) { return 0; }
std::wstring SpielordnerAusDatei(const std::wstring&) { return std::wstring(); }
void SchreibeStartprotokoll(void*, const std::wstring&) {}
}
// Ebenso das Animationsfenster (0.38.0).
#include "swbf2import_animfenster.h"
namespace swbf2 {
int ZeigeAnimFenster(const AnimBruecke&) { return 0; }
}
