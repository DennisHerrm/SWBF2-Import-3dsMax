#!/usr/bin/env python3
"""Ausgabe gleichzeitig ins Fenster und ans START.log.

Bisher lief das ueber eine Hilfsroutine in der START.bat: Ausgabe in eine
Zwischendatei, dann zweimal "type". Das ist beim zweiten und dritten Aufruf
gescheitert ("Der Prozess kann nicht auf die Datei zugreifen, da sie von
einem anderen Prozess verwendet wird") - und mit ihm sind die Ergebnisse der
Container- und der MeshSet-Probe verschwunden.

cmd kennt kein "tee", also macht es Python selbst: liegt der Pfad in der
Umgebungsvariablen SWBF2_LOG, wird jede Zeile zusaetzlich dort angehaengt.
Keine Zwischendatei, keine Sperre.
"""
import os
import sys


class _Doppelt:
    def __init__(self, strom, datei):
        self.strom = strom
        self.datei = datei

    def write(self, text):
        self.strom.write(text)
        try:
            self.datei.write(text)
        except Exception:
            pass

    def flush(self):
        self.strom.flush()
        try:
            self.datei.flush()
        except Exception:
            pass


def anschalten():
    """Einmal am Anfang aufrufen. Ohne SWBF2_LOG passiert nichts."""
    pfad = os.environ.get("SWBF2_LOG")
    if not pfad:
        return
    try:
        datei = open(pfad, "a", encoding="utf-8", errors="replace")
    except OSError:
        return
    sys.stdout = _Doppelt(sys.stdout, datei)
    sys.stderr = _Doppelt(sys.stderr, datei)
