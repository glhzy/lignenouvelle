#!/usr/bin/env python3
"""
Copie tous les fichiers WAV du dossier sounds/ vers la carte SD.
La SD doit être formatée FAT32 et insérée dans le Mac (slot ou adaptateur USB).

Usage :
    python3 tools/prepare_sd.py [chemin_volume]

    Sans argument : liste les volumes disponibles.
    Avec argument : copie les sons vers <chemin_volume>/sounds/
"""

import sys
import os
import shutil
import subprocess
from pathlib import Path

SOUNDS_DIR = Path(__file__).parent.parent / "sounds"


def list_volumes():
    """Liste les volumes montés (hors macOS system)."""
    vols = [Path("/Volumes") / v for v in os.listdir("/Volumes")]
    return [v for v in vols if v.is_dir() and not v.name.startswith(".")]


def copy_sounds(target_root: Path):
    target_sounds = target_root / "sounds"
    target_sounds.mkdir(parents=True, exist_ok=True)

    total_files = 0
    total_bytes = 0

    for wav in sorted(SOUNDS_DIR.rglob("*.wav")):
        rel = wav.relative_to(SOUNDS_DIR)
        # Ignorer les sous-dossiers Not_Used/
        if any(p.name == "Not_Used" for p in rel.parents):
            continue

        dest = target_sounds / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(wav, dest)
        size = wav.stat().st_size
        total_bytes += size
        total_files += 1
        print(f"  {rel}  ({size // 1024} Ko)")

    print(f"\n{total_files} fichiers copiés — {total_bytes // 1024} Ko au total")


def main():
    if len(sys.argv) >= 2:
        target = Path(sys.argv[1])
        if not target.exists():
            print(f"Erreur : {target} n'existe pas.")
            sys.exit(1)
        print(f"Copie vers {target}/sounds/ …\n")
        copy_sounds(target)
        return

    # Pas d'argument : afficher les volumes disponibles
    vols = list_volumes()
    if not vols:
        print("Aucun volume externe trouvé dans /Volumes/.")
        print("Insérez la microSD dans votre Mac, puis relancez :")
        print("  python3 tools/prepare_sd.py /Volumes/<NomSD>")
        return

    print("Volumes disponibles :")
    for v in vols:
        try:
            stat = shutil.disk_usage(v)
            print(f"  {v.name:20s}  {stat.free // 1024 // 1024} Mo libres sur {stat.total // 1024 // 1024} Mo")
        except Exception:
            print(f"  {v.name}")

    print("\nUsage : python3 tools/prepare_sd.py /Volumes/<NomSD>")

    # Calcul de la taille totale à copier
    total = sum(
        f.stat().st_size
        for f in SOUNDS_DIR.rglob("*.wav")
        if not any(p.name == "Not_Used" for p in f.relative_to(SOUNDS_DIR).parents)
    )
    print(f"\nTaille à copier : ~{total // 1024 // 1024} Mo")


if __name__ == "__main__":
    main()
