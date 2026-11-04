#!/usr/bin/env python3
"""
Assemble le contenu de firmware/data/ (LittleFS) depuis sounds/.
À lancer AVANT : pio run -t uploadfs

    python3 tools/prepare_fs.py

Les WAV sont sur la microSD (tools/prepare_sd.py).
LittleFS ne contient que les JSON de métadonnées + l'interface web + les polices.

Locos non gérées par le moteur audio firmware (séquences uniquement) :
    LOCOS_NO_FIRMWARE = exclues de la liste visible dans l'interface.
"""

import os, shutil, json

# ── Locos non supportées par le moteur audio (ex : séquences TGV) ─────────────
LOCOS_NO_FIRMWARE = []  # toutes les locos sont disponibles

# ──────────────────────────────────────────────────────────────────────────────

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC   = os.path.join(ROOT, "sounds")
DATA  = os.path.join(ROOT, "firmware", "data")
LIMIT = 13 * 1024 * 1024

total = 0

def copy(src, dst):
    global total
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    size = os.path.getsize(dst)
    total += size
    print(f"  {size//1024:5d} Ko  {os.path.relpath(dst, DATA)}")

# Nettoyer les sons précédents pour éviter les fichiers obsolètes
sounds_dst = os.path.join(DATA, "sounds")
if os.path.isdir(sounds_dst):
    shutil.rmtree(sounds_dst)

# ── locos.json filtré (disponible=false pour les locos absentes de la flash) ──
print("=== Sons ===")
locos_src = os.path.join(SRC, "locos.json")
with open(locos_src, encoding="utf-8") as f:
    locos_all = json.load(f)

locos_out = []
n_dispo = 0
for loco in locos_all:
    entry = dict(loco)
    entry["disponible"] = loco["id"] not in LOCOS_NO_FIRMWARE
    if entry["disponible"]:
        n_dispo += 1
    locos_out.append(entry)

locos_dst = os.path.join(sounds_dst, "locos.json")
os.makedirs(sounds_dst, exist_ok=True)
with open(locos_dst, "w", encoding="utf-8") as f:
    json.dump(locos_out, f, ensure_ascii=False, indent=2)
size = os.path.getsize(locos_dst)
total += size
print(f"  {size//1024:5d} Ko  sounds/locos.json  ({n_dispo}/{len(locos_all)} locos disponibles)")

# ── JSON de toutes les locos (WAV sur SD, pas dans la flash) ──────────────────
for loco in locos_all:
    sons_rel = loco.get("sons")
    if not sons_rel:
        continue
    loco_dir = os.path.dirname(sons_rel)
    src_dir  = os.path.join(SRC, loco_dir)
    dst_dir  = os.path.join(sounds_dst, loco_dir)

    if not os.path.isdir(src_dir):
        print(f"  [AVERTISSEMENT] dossier manquant : {src_dir}")
        continue

    # Copier le(s) fichier(s) JSON de la loco (sons.json et/ou sons_player.json)
    for json_name in ["sons.json", "sons_player.json"]:
        src_json = os.path.join(src_dir, json_name)
        if os.path.exists(src_json):
            copy(src_json, os.path.join(dst_dir, json_name))

# ── Interface web ─────────────────────────────────────────────────────────────
print("\n=== Interface web ===")

idx = os.path.join(DATA, "www", "index.html")
if os.path.exists(idx):
    size = os.path.getsize(idx)
    total += size
    print(f"  {size//1024:5d} Ko  www/index.html")
else:
    print("  [AVERTISSEMENT] www/index.html manquant")

fonts_dst = os.path.join(DATA, "www", "fonts")
for woff in ["PoliceSNCF.woff", "UniversLTStd55.woff"]:
    src = os.path.join(ROOT, "fonts", woff)
    if os.path.exists(src):
        copy(src, os.path.join(fonts_dst, woff))
    else:
        print(f"  [AVERTISSEMENT] police manquante : {src}")

# ── Bilan ──────────────────────────────────────────────────────────────────────
locos_dispo = [l["id"] for l in locos_all if l["id"] not in LOCOS_NO_FIRMWARE]
print(f"\nLocos disponibles : {', '.join(locos_dispo)}")
print(f"Total : {total/1024/1024:.2f} Mo / ~13 Mo disponibles", end="")
if total > LIMIT:
    print(f"  ← DÉPASSEMENT ! ({(total-LIMIT)//1024} Ko en trop)")
    raise SystemExit(1)
else:
    print(f"  (marge {(LIMIT-total)//1024} Ko)")

print(f"\nfirmware/data/ prêt.")
print("Lance maintenant : pio run -t uploadfs")
