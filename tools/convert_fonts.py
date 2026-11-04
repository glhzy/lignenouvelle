#!/usr/bin/env python3
"""Convertit sncf.ttf (PoliceSNCF) et universltstd_2.otf (Univers LT Std 55) en WOFF pour la page web.
PoliceSNCF est codée en « symbole » (U+F020..U+F0FF) : on la remappe sur l'ASCII/Latin-1 et on retire
les glyphes de logo (! ? $ * + = £ §) ainsi que les glyphes plus larges que 1000 unités.
Usage : python3 convert_fonts.py dossier_polices_sources dossier_sortie   (nécessite fonttools)
"""
import sys
from pathlib import Path
from fontTools.ttLib import TTFont, newTable
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable
src, out = Path(sys.argv[1]), Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
t = TTFont(src / 'sncf.ttf'); old = t.getBestCmap(); hm = t['hmtx']
logo = set('!?$*+=£§'); new = {}
for k, g in old.items():
    c = k - 0xF000
    if 0x20 <= c <= 0xFF and chr(c) not in logo and hm[g][0] <= 1000 and not (0x7F <= c <= 0xBF):
        new[c] = g
new[0xA0] = old[0xF020]
cm = newTable('cmap'); cm.tableVersion = 0
st = CmapSubtable.newSubtable(4); st.platformID = 3; st.platEncID = 1; st.language = 0; st.cmap = new
cm.tables = [st]; t['cmap'] = cm
t['OS/2'].usFirstCharIndex = 0x20; t['OS/2'].usLastCharIndex = 0xFF
t.flavor = 'woff'; t.save(out / 'PoliceSNCF.woff')
u = TTFont(src / 'universltstd_2.otf'); u.flavor = 'woff'; u.save(out / 'UniversLTStd55.woff')
