#!/usr/bin/env python3
"""Extraction des sons d'un projet ZIMO (.zpp) -> WAV mono 16 bits 22050 Hz.

Structure du fichier (déduite par analyse, voir docs/FORMATS.md) :
  - blocs alignés sur des pages de 0x100 octets ; en-tête de 10 octets à page+0x80,
    données PCM à page+0x8A
  - en-tête = drapeau(1) | fin(3, LE) | boucle_début(3) | boucle_fin(3)
  - drapeau : bit 7 (0x80) → PCM 16 bits signé LE (projets v2 : CC 6500…)
              bit 7 absent → PCM 8 bits non signé centré à 128 (projets v1)
              bit 3 (0x08) → son en boucle
  - tous les sons sont à 22050 Hz quelle que soit la version
  - les 8 bits sont convertis en 16 bits signés : sample = (octet − 128) × 256
  - le NOM d'un son (octet longueur + texte) est rangé APRES ses données :
    le nom trouvé avant l'en-tête d'un bloc appartient au bloc PRÉCÉDENT
  - répertoire en fin de fichier : [00 slot 00 00 00 00][longueur][nom] répétés
Usage : python3 zpp16_extract.py fichier.zpp dossier_sortie
"""
import array, re, sys, wave, unicodedata
from pathlib import Path

SR  = 22050   # fréquence unique pour tous les sons
ENC = 'cp1252'


def read_directory(d):
    """Retourne (offset_debut_repertoire, [(nom, slot)]).
    Chaque entrée = [00 slot 00 00 00 00][longueur][nom] : le numéro de slot PRÉCÈDE le nom."""
    best = (0, [])
    for p in range(len(d) - 20000, len(d) - 10):
        q = p
        ents = []
        while q < len(d) - 8:
            pre = d[q - 6:q]
            if pre[0] != 0 or pre[2:] != b'\0\0\0\0':
                break
            L = d[q]
            nm = d[q + 1:q + 1 + L]
            if L == 0 or L > 60 or not all(c >= 32 for c in nm):
                break
            ents.append((nm.decode(ENC), pre[1]))
            q += 7 + L
        if len(ents) > len(best[1]):
            best = (p, ents)
    return best


def find_blocks(d, limit):
    blocks = []
    for h in range(0x180, limit, 0x100):
        if d[h - 4:h] != b'\0\0\0\0':
            continue
        end = int.from_bytes(d[h + 1:h + 4], 'little')
        ls = int.from_bytes(d[h + 4:h + 7], 'little')
        le = int.from_bytes(d[h + 7:h + 10], 'little')
        if h + 10 < end <= limit and ls <= limit and le <= limit and d[h + 3] < 0xF0 and end - h > 0x20:
            flag = d[h]
            blocks.append(dict(h=h, flag=flag, end=end, loop_start=ls, loop_end=le))
    out = []
    for i, b in enumerate(blocks):  # la fin d'un bloc doit précéder le bloc suivant
        nxt = blocks[i + 1]['h'] if i + 1 < len(blocks) else limit
        if b['end'] < nxt:
            out.append(b)
    return out


def name_positions(d, names, limit):
    pos = {}
    for n in names:
        b = n.encode(ENC)
        for m in re.finditer(re.escape(b), d[:limit]):
            if d[m.start() - 1] == len(b):
                pos[n] = m.start() - 1
                break
    return pos


def build(d):
    dstart, ents = read_directory(d)
    blocks = find_blocks(d, dstart)
    pos = name_positions(d, [n for n, s in ents], dstart)
    slots = dict(ents)
    for b in blocks:
        b['name'] = None
        b['slot'] = None
        b['loop'] = bool(b['flag'] & 0x08)
    for n, p in sorted(pos.items(), key=lambda x: x[1]):
        prev = [b for b in blocks if b['h'] < p]
        if prev and prev[-1]['name'] is None:
            prev[-1]['name'] = n
            prev[-1]['slot'] = slots[n]
    return dstart, ents, blocks


def pcm(d, b):
    s = b['h'] + 10
    raw = d[s:b['end']]
    if b['flag'] & 0x80:
        # PCM 16 bits signé little-endian : retourner tel quel (taille paire)
        return raw[:len(raw) & ~1]
    else:
        # PCM 8 bits non signé (centre = 128) → convertir en 16 bits signé
        out = array.array('h', ((x - 128) * 256 for x in raw))
        return out.tobytes()


def clean(s):
    s = s.replace('ö', 'oe').replace('ü', 'ue').replace('ä', 'ae')
    s = unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode()
    return re.sub(r'[^A-Za-z0-9_\-\.]+', '_', s).strip('_')


def write_wav(path, data):
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data)


if __name__ == '__main__':
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    d = src.read_bytes()
    dstart, ents, blocks = build(d)
    print(f'{len(ents)} entrées au répertoire, {len(blocks)} blocs audio, '
          f'{sum(1 for b in blocks if b["name"])} nommés')
    for i, b in enumerate(blocks, 1):
        base = clean(b['name']) if b['name'] else 'sans-nom' + ('_boucle' if b['loop'] else '')
        slot = f"s{b['slot']:02d}_" if b['slot'] is not None else ''
        write_wav(out / f'{i:02d}_{slot}{base}.wav', pcm(d, b))
