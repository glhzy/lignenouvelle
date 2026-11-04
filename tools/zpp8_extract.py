#!/usr/bin/env python3
"""Extraction des sons du projet ZIMO 8 bits SNCF BB 16000 (.zpp) -> WAV 16 bits mono 22 050 Hz.
ATTENTION : l'association nom -> données N'EST PAS vérifiée pour ce fichier. Dans le projet
16 bits (CC 6500) les noms sont rangés après leurs données (décalage d'un bloc) ; ici le
script suppose le contraire (nom avant les données, en-tête de 0x5C octets). À contrôler à l'oreille.
Les échantillons sont des octets non signés centrés sur 128 (8 bits).
Usage : python3 zpp8_extract.py fichier.zpp dossier_sortie   (nécessite numpy)
"""
import re, sys, wave
from pathlib import Path
import numpy as np
NAMES = ["Motor tief","Motor hoch","Bremsenquietschen","Bahnhofsansage aktuell","Bahnhofsansage alt",
 "Horn hoch","Horn tief","Horn hoch-tief","Bahnhofsansage Jingle aktuell","Bahnhofsansage Jingle alt",
 "Kompressor","Kupplung On","Kupplung Off","Air train coupling","Door closing","Schaffnerpfiff",
 "Test KVB","Disjunctor","Bremse lösen"]
DIR_START = 0x22199c   # début du répertoire final de CE fichier
def first_smooth(d, a, b):
    seg = np.frombuffer(d[a:b], dtype=np.uint8).astype(int)
    for i in range(0, len(seg) - 32):
        w = seg[i:i + 32]
        if w.min() >= 0x20 and w.max() <= 0xE0 and np.abs(np.diff(w)).max() < 40:
            return a + i
    return a
if __name__ == '__main__':
    d = Path(sys.argv[1]).read_bytes(); out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
    starts = []
    for n in NAMES:
        b = n.encode('latin-1')
        h = [m.start() for m in re.finditer(re.escape(b), d) if m.start() < DIR_START and d[m.start() - 1] == len(b)][0]
        starts.append(h - 1)
    blocks = [('P039-BB16000_F8', 0x218A, starts[0])]
    for n, s, e in zip(NAMES, starts, starts[1:] + [DIR_START]):
        blocks.append((n, first_smooth(d, s + 1 + len(n), e), e))
    for i, (n, s, e) in enumerate(blocks, 1):
        while e > s and d[e - 1] == 0: e -= 1
        x = (np.frombuffer(d[s:e], dtype=np.uint8).astype(np.int16) - 128) * 256
        fn = re.sub(r'[^A-Za-z0-9_\-]+', '_', n.replace('ö', 'oe')) + '.wav'
        with wave.open(str(out / f'{i:02d}_{fn}'), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050); w.writeframes(x.astype('<i2').tobytes())
