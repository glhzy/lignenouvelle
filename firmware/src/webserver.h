#pragma once
// Shim : ce nom entre en collision avec <WebServer.h> sur macOS (FS insensible à la casse).
// #include_next saute au répertoire suivant pour trouver la bibliothèque Arduino réelle.
#include_next <WebServer.h>
