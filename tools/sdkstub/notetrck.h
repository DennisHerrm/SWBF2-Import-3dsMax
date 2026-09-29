#pragma once
// Attrappe fuer notetrck.h (NoteTrack, NoteKey, DefNoteTrack, NewDefaultNoteTrack).
#include "max.h"

class NoteKey {
public:
    TimeValue time = 0;
    MSTR note;
    DWORD flags = 0;
    NoteKey(TimeValue t, const MSTR& n, DWORD f = 0) : time(t), note(n), flags(f) {}
};

class NoteKeyTab : public Tab<NoteKey*> {};

class NoteTrack {
public:
    virtual ~NoteTrack() {}
};

class DefNoteTrack : public NoteTrack {
public:
    NoteKeyTab keys;
};

inline NoteTrack* NewDefaultNoteTrack() { return new DefNoteTrack(); }
