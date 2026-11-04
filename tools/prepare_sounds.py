#!/usr/bin/env python3
"""Prépare les 15 sons du décor à partir du projet CC 6500 (.zpp 16 bits).
Recadrage du silence, retrait du décalage continu, normalisation RMS (0,10) limitée
à un pic de 0,95, fondus de 8 ms, boucle enchaînée sur 120 ms (compresseur).
Sortie : WAV mono 16 bits 22 050 Hz + sons.json (table lue par la page web et le firmware).
Usage : python3 prepare_sounds.py fichier.zpp dossier_sortie   (nécessite numpy)
"""
import json, sys, wave
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).parent))
import zpp16_extract as Z

# id, nom du son dans le projet ZIMO, libellé affiché, groupe, boucle, niveau de base (0..1)
SEL = [
 ('klaxon_long',      'Klaxon_hoch-lang_lmt',                       'Klaxon long',              'klaxon',  False, 0.85),
 ('klaxon_court',     'Klaxon_hoch-kurz_lmt',                       'Klaxon court',             'klaxon',  False, 0.85),
 ('klaxon_bitonal',   'Klaxon_hoch-tief-hoch_lmt',                  'Klaxon bitonal',           'klaxon',  False, 0.85),
 ('sifflet',          'Sifflet_SNCF_2_echo',                        'Sifflet chef de gare',     'klaxon',  False, 0.80),
 ('annonce_marseille','Annonce Corail Marseille_jingle84',          'Annonce Corail Marseille', 'annonce', False, 1.00),
 ('annonce_jingle',   'annonce_Corail_jingle77',                    'Annonce Corail, jingle',   'annonce', False, 1.00),
 ('annonce_mistral',  'Annonce TEE Mistral_jingle très ancien_8-bit','Annonce TEE Mistral',     'annonce', False, 0.90),
 ('porte_cabine',     'Tür_auf-zu',                                 'Porte de cabine',          'porte',   False, 0.90),
 ('porte_machines',   'Maschinenraumtür_auf-zu',                    'Porte salle des machines', 'porte',   False, 0.90),
 ('essai_freins',     'Bremsprobe_dir.-Br._an-lösen_kurz',          'Essai des freins',         'frein',   False, 0.80),
 ('frein_main',       'Handbremse_an_k',                            'Frein à main',             'frein',   False, 0.90),
 ('compresseur',      'Compresseur_kurz',                           'Compresseur',              'ambiance',True,  0.45),
 ('purge_air',        'Luftzisch_kurz',                             "Purge d'air",              'ambiance',False, 0.70),
 ('pantographe',      'Panto-up-down_02',                           'Pantographe',              'ambiance',False, 0.70),
 ('kvb',              'KVB_start',                                  'Signal KVB',               'ambiance',False, 0.70),
]
SR = Z.SR

def zero_cross(x, i, step):
    j = i
    while 0 < j < len(x) - 1 and x[j] * x[j + 1] > 0:
        j += step
    return j

def process(x, loop):
    x = x - x.mean()
    idx = np.where(np.abs(x) > 0.002)[0]
    if len(idx):
        x = x[max(0, idx[0] - int(.01 * SR)): idx[-1] + int(.05 * SR)]
    rms = np.sqrt((x ** 2).mean())
    g = min(0.10 / max(rms, 1e-6), 0.95 / np.abs(x).max())
    x = x * g
    if loop:
        N = int(.12 * SR)
        a = zero_cross(x, len(x) // 50, 1)
        b = zero_cross(x, len(x) - N - 1, -1)
        body = x[a:b + N].copy()
        n = len(body) - N
        w = np.linspace(0, 1, N)
        body[:N] = body[:N] * w + body[n:n + N] * (1 - w)
        x = body[:n]
    else:
        f = int(.008 * SR)
        x[:f] *= np.linspace(0, 1, f)
        x[-f:] *= np.linspace(1, 0, f)
    return np.clip(x * 32767, -32767, 32767).astype('<i2')

if __name__ == '__main__':
    src, out = Path(sys.argv[1]), Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
    d = src.read_bytes()
    _, _, blocks = Z.build(d)
    byname = {b['name']: b for b in blocks if b['name']}
    table, total = [], 0
    for sid, name, label, grp, loop, lvl in SEL:
        b = byname[name]
        x = np.frombuffer(Z.pcm(d, b), dtype='<i2').astype(np.float64) / 32768
        pcm = process(x, loop)
        Z.write_wav(out / f'{sid}.wav', pcm.tobytes())
        size = len(pcm) * 2 + 44; total += size
        table.append(dict(id=sid, file=f'{sid}.wav', label=label, group=grp, loop=loop,
                          level=lvl, duration=round(len(pcm) / SR, 2), bytes=size, source=name))
    (out / 'sons.json').write_text(json.dumps(table, ensure_ascii=False, indent=1), encoding='utf-8')
    print(f'{len(table)} sons, {total/1e6:.2f} Mo')
