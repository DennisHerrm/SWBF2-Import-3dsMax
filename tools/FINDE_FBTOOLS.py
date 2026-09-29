#!/usr/bin/env python3
"""Gibt den Pfad zum fbtools-Ordner aus - oder nichts."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location(
    "V", os.path.join(os.path.dirname(os.path.abspath(__file__)), "VERGLEICHE.py"))
V = importlib.util.module_from_spec(spec)
spec.loader.exec_module(V)
p, _ = V.finde_fbtools(sys.argv[1] if len(sys.argv) > 1 else None)
if p:
    print(p)
