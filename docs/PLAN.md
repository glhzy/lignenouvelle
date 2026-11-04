# Plan : boîtier sonore Wi-Fi « Lignenouvelle »

## 1. Architecture
- **ESP32-S3** : sert la page web, lit les sons, les mélange. **MAX98357A** : DAC + ampli classe D (I2S 3 fils BCLK/LRC/DIN) → haut-parleur 4 Ω.
- **Flash 16 Mo** : programme (~3 Mo) + LittleFS (~12 Mo) pour sons, page web, polices. Aucun logiciel côté téléphone.

## 2. Wi-Fi (option B : rejoindre la box)
1re utilisation : l'ESP32 crée un réseau temporaire de configuration ; l'utilisateur saisit SSID et mot de passe depuis le téléphone ; mémorisés (NVS) ; redémarrage. Ensuite connexion automatique. Adresse `lignenouvelle.local` (mDNS ; pas universel) + affichage de l'IP au démarrage + conseil de réserver une IP fixe dans la box.

## 3. Page web
Une page responsive, gros boutons, volume + « Arrêt », 4 sections (Klaxons et sifflet, Annonces, Portes et freins, Ambiances en boucle). Bouton allumé pendant la lecture ; ambiances en bascule marche/arrêt. Détails visuels : `CLAUDE.md` §6 et `ui/maquette.html`.

## 4. Automatismes
Annonces à intervalles aléatoires réglables ; ambiances aléatoires (compresseur, purge d'air) façon générateurs Z1/Z2 ; interrupteur sur la page.

## 5. Préparation des sons
15 sons (WAV mono 16 bits 22 050 Hz), recadrés, normalisés (RMS 0,10, pic ≤ 0,95), fondus 8 ms, boucle enchaînée sur 120 ms pour le compresseur. 4,6 Mo / 103 s au total. `tools/prepare_sounds.py`.

## 6. Moteur audio (détail)
1. **Mélangeur maison** (pas de bibliothèque de décodage : WAV bruts). Pilote I2S d'Arduino-ESP32, version fixée dans le projet (API I2S différente entre v2 et v3).
2. Chaîne : lecture/validation du WAV → **voix** (position, volume, boucle, fondu) → somme des voix toutes les ~10 ms → volume général → limiteur → I2S stéréo (même échantillon à G et D).
3. **Tous les sons en PSRAM** au démarrage (1–2 s) ; repli : lecture depuis la flash pour les sons trop longs.
4. **6 voix** ; groupes exclusifs (un klaxon, une annonce) ; ambiances en fond ; si saturation, couper la plus ancienne non bouclée ; atténuation automatique des ambiances pendant une annonce.
5. **Anti-claquements** : fondus 5–10 ms, 100 ms sur « Arrêt » ; boucles coupées aux passages par zéro ; flux de silence continu au repos (horloge I2S jamais coupée).
6. **Volume** : 0–100 % courbe douce ; plafond ≈ 80 % (haut-parleur 2 W nominal / 3 W max) ; mémorisé ; niveau de base par son (`sons.json`).
7. **Cœurs** : un cœur dédié au mélangeur (priorité élevée) ; l'autre au Wi-Fi/web ; les requêtes web déposent des commandes dans une file.
8. **Table des sons** (`sons.json`) lue par la page web et les automatismes : ajouter/retirer un son sans toucher au code.
9. **Tests** : son au démarrage ; 3 sons simultanés sans saturation ; boucle longue sans dérive ; Wi-Fi très sollicité sans saccade (risque principal).
Ouverts : broche `SD` de l'ampli (veille/choix du canal, vérifier la fiche du module) ; taille exacte de la sélection vs PSRAM.

## 7. Ordre de réalisation
Test matériel → moteur audio → Wi-Fi/configuration → page web et boutons → automatismes/réglages → (v2) mise à jour des sons depuis le navigateur.

## 8. Sélecteur de locomotives (voie 1 / voie 2)

### Concept
La page de contrôle gère deux locomotives simultanément : une sur la **voie 1**, une sur la **voie 2**. Chaque voie a son propre sélecteur de loco (menu déroulant) et son propre panneau de boutons Fx.

### Locos disponibles
Issues des ZPP + PDFs dans `Zpp Extracts 20261024/`. À valider loco par loco avant extraction :
- CC 6500 (sons actuels déjà extraits — référence)
- BB 26000 Sybic (analysée)
- BB 9400
- BB 67400
- CC 72000
- CC 21000
- 141-R (vapeur)
- BB 16000 (format 8 bits, extraction différente)
- BB 9300

### Structure des données : locos.json
Nouveau fichier `sounds/locos.json` : une entrée par loco avec ses fonctions sonores issues du PDF ZIMO.

```json
[
  {
    "id": "cc6500",
    "label": "CC 6500",
    "functions": [
      { "key": "F2", "label": "Klaxon court", "sound_id": "klaxon_court", "group": "klaxon", "loop": false },
      { "key": "F3", "label": "Klaxon long",  "sound_id": "klaxon_long",  "group": "klaxon", "loop": false }
    ]
  },
  {
    "id": "bb26000",
    "label": "BB 26000 Sybic",
    "functions": [
      { "key": "F2",  "label": "Klaxon bas-haut",       "sound_id": "bb26000_klaxon_bas_haut",  "group": "klaxon",   "loop": false },
      { "key": "F3",  "label": "Klaxon haut",            "sound_id": "bb26000_klaxon_haut",      "group": "klaxon",   "loop": false },
      { "key": "F4",  "label": "Klaxon bas-haut-bas",    "sound_id": "bb26000_klaxon_bhb",       "group": "klaxon",   "loop": false },
      { "key": "F5",  "label": "Sifflet chef de gare",   "sound_id": "bb26000_sifflet",          "group": "klaxon",   "loop": false },
      { "key": "F7",  "label": "Attelage / dételage",    "sound_id": "bb26000_attelage",         "group": "porte",    "loop": false },
      { "key": "F11", "label": "Frein rhéostatique",     "sound_id": "bb26000_frein_rheo",       "group": "frein",    "loop": false },
      { "key": "F18", "label": "Porte de cabine",        "sound_id": "bb26000_porte",            "group": "porte",    "loop": false },
      { "key": "F19", "label": "KVB démarrage",          "sound_id": "bb26000_kvb",              "group": "ambiance", "loop": false },
      { "key": "F20", "label": "Compresseur",            "sound_id": "bb26000_compresseur",      "group": "ambiance", "loop": true  },
      { "key": "F21", "label": "Petit cheval",           "sound_id": "bb26000_petit_cheval",     "group": "ambiance", "loop": false },
      { "key": "F25", "label": "Pantographe 1500 V DC",  "sound_id": "bb26000_panto",            "group": "ambiance", "loop": false }
    ]
  }
]
```

Sons de chaque loco dans `sounds/<loco_id>/` (ex. `sounds/bb26000/`). Les sons strictement identiques entre locos peuvent être partagés (ex. sifflet SNCF commun).

### Interface (UI)
Deux blocs « voie » côte à côte sur desktop, empilés sur mobile (< 480 px) :

```
┌──────────────────────┐  ┌──────────────────────┐
│  VOIE 1              │  │  VOIE 2              │
│  [ CC 6500      ▼ ]  │  │  [ BB 26000     ▼ ]  │
├──────────────────────┤  ├──────────────────────┤
│  F2  — Klaxon court  │  │  F2  — Klaxon bas-haut│
│  F3  — Klaxon long   │  │  F3  — Klaxon haut   │
│  F4  — Bitonal       │  │  F4  — Klaxon b-h-b  │
│  F5  — Sifflet       │  │  F5  — Sifflet       │
│  …                   │  │  F20 — Compresseur   │
└──────────────────────┘  └──────────────────────┘
```

Chaque bouton : format **«Fx — Libellé»**, style tuile SNCF (blanc, bordure bleue, fond orange si actif). N'afficher que les fonctions ayant un son (pas les fonctions feux/lumière). Les groupes exclusifs restent par voie (un klaxon actif à la fois par voie).

### Modifications de l'API
| Endpoint | Changement |
|---|---|
| `GET /api/locos` | Nouveau : retourne `locos.json` complet |
| `POST /api/loco?voie=1&id=cc6500` | Nouveau : mémorise la loco choisie (NVS) |
| `POST /api/play?voie=1&sound=<sound_id>` | `voie` s'ajoute (1 ou 2 ; défaut = 1 pour compatibilité) |
| `GET /api/state` | Ajoute `voie1_loco` et `voie2_loco` dans la réponse |

### Ordre de réalisation pour cette fonctionnalité
1. Analyser les PDF des locos restantes ; établir leur table Fx → son
2. Choisir les sons à garder par loco (en cours)
3. Extraire les sons dans `sounds/<loco_id>/` via `zpp16_extract.py` puis `prepare_sounds.py`
4. Écrire `sounds/locos.json`
5. Ajouter `GET /api/locos` et `POST /api/loco`
6. Modifier la page web : deux sélecteurs + grilles Fx dynamiques chargées depuis `/api/locos`
