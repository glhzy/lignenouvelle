# Projet « Lignenouvelle » : boîtier sonore Wi-Fi pour décor ferroviaire

> Fichier de reprise pour Claude Code. Lis-le en entier, puis `docs/` au besoin.
> Langue de travail : **français**, tutoiement. L'utilisateur est modéliste ferroviaire, pas développeur.
> Ne code rien de lourd avant d'avoir confirmé l'étape en cours avec lui (il a demandé « décris tes plans, ne code rien » à une étape).

## 1. Objectif
Un boîtier indépendant, caché dans un décor de réseau miniature, qui joue des sons de locomotives/gare
(klaxons, annonces SNCF, portes, freins, ambiances) commandés **depuis un téléphone par le Wi-Fi**, sans rien installer :
l'ESP32 sert lui-même une page web. Le nom du boîtier est **Lignenouvelle** (adresse `lignenouvelle.local`).

## 2. Matériel (reçu fin octobre 2026)
| Élément | Choix |
|---|---|
| Carte | **ESP32-S3-DevKitC-1 N16R8** (16 Mo flash, 8 Mo PSRAM octale), lot de 2 (Heemol, 18,98 €). Module WROOM-1 (antenne imprimée, pas « U ») |
| Ampli | **MAX98357A** (I2S, classe D), lot de 3 (AYWHP). Livré avec bornier à vis vert et barrette jaune à souder |
| Haut-parleur | CQRobot fermé **4 Ω 3 W, 70×31×16 mm** (fiche Dupont 2.54 à couper, fils nus au bornier) |
| À prévoir | fer à souder, fils Dupont F-F, câble USB-C **de données**, chargeur USB 5 V |

Câblage ESP32-S3 → MAX98357A (broches I2S configurables ; **GPIO 35, 36, 37 interdits** sur variante R8 ; les GPIO 22-26 de l'ESP32 classique n'existent pas sur S3) :
`BCLK=GPIO5` (fil **marron**), `LRC=GPIO6` (fil **gris**), `DIN=GPIO4` (fil **orange**), `Vin=3V3` (fil **rouge**), `GND=GND` (fil **noir**), **`SD=3V3`** (obligatoire : laissée flottante = shutdown). `GAIN` non connectée (gain 9 dB par défaut). Haut-parleur sur le bornier vert (+/− marqués).

Câblage ESP32-S3 → lecteur microSD (SPI2/FSPI) : `CS=GPIO10` (fil **jaune**), `MOSI=GPIO11` (fil **bleu**), `CLK=GPIO12` (fil **blanc**), `MISO=GPIO13` (fil **vert**), `3V3` (fil **rouge**), `GND` (fil **noir**).
À la réception : vérifier que la flash détectée fait bien 16 Mo (`esptool flash_id`) ; certaines annonces « N16R8 » sont des 8 Mo.

## 3. Décisions prises
- Wi-Fi **option B** : l'ESP32 rejoint la box. 1re utilisation : réseau temporaire de configuration (portail captif) pour saisir SSID/mot de passe, mémorisés en NVS. Afficher aussi l'adresse IP (le `.local` ne marche pas partout ; conseiller de réserver une IP fixe dans la box).
- **Moteur audio maison** (pas ESP8266Audio) : mélangeur PCM 16 bits mono 22 050 Hz, 6 voix, sortie I2S stéréo (même échantillon G/D). Voir `docs/PLAN.md`.
- **Une seule loco en PSRAM à la fois** (8 Mo max). Les WAV sont sur microSD (SPI2/FSPI : CS=GPIO10, MOSI=GPIO11, CLK=GPIO12, MISO=GPIO13), les JSON metadata dans LittleFS (~0,08 Mo). Chargement asynchrone (FreeRTOS `loco_load_task` sur Core 1, priorité 1) pour ne pas bloquer le serveur web.
- **Automatismes : oui** (annonces/ambiances aléatoires, interrupteur + fréquence sur la page, comme les générateurs Z1/Z2 ZIMO) — **à faire étape 7**.
- **Mise à jour des sons via navigateur : reportée** à une version 2 (prévoir mot de passe si ajoutée).
- 10 locos disponibles (table §5), chacune avec son propre `sons.json`. Le « disjoncteur » de la BB 16000 est **écarté** (association nom/son non vérifiable).
- Interface : style signalétique **SNCF années 80** (voir §6), polices PoliceSNCF + Univers LT Std embarquées. **Opérationnelle** (maquette statique portée en page dynamique).
- **Sélecteur de locomotive** dans l'en-tête — une loco à la fois (limite PSRAM). Changer de loco déclenche un chargement async ; bannière orange + boutons grisés pendant le transfert SD (~10-25 s). Deux voies dans l'API mais les deux pointent sur la même loco (deux locos simultanées = futur).

## 4. État d'avancement (3 novembre 2026)
**Étapes 1-6 TERMINÉES. Firmware fonctionnel sur matériel réel.**

Boot confirmé : PSRAM OK (8188 Ko), LittleFS OK, SD OK, sons déclenchés depuis l'interface. Wi-Fi connecté à `Freebox-31E765` (2,4 GHz), IP 192.168.1.150, mDNS `http://lignenouvelle.local/`.

**Fait :**
1. ✅ Sons extraits et sélectionnés pour 10 locos (table §5).
2. ✅ `sounds/<loco_id>/sons.json` + `sounds/locos.json`.
3. ✅ Firmware PlatformIO, board `4d_systems_esp32s3_gen4_r8n16` (qio_opi, BOARD_HAS_PSRAM).
4. ✅ Moteur audio 6 voix, groupes exclusifs, fondus, plafond 80 %, Core 1.
5. ✅ Wi-Fi + portail captif, mDNS `lignenouvelle`.
6. ✅ Serveur web + API REST + interface dynamique, sélecteur de loco, bannière de chargement.

**À faire, dans l'ordre :**
7. Automatismes (annonces/ambiances aléatoires, interrupteur + fréquence sur la page).
8. Persistance NVS (volume + loco sélectionnée au redémarrage).
9. Tests longue durée (3 sons simultanés, boucle longue, Wi-Fi sollicité).

API opérationnelle : `GET /api/locos`, `GET /api/state` (`loading`, sons en cours, volume, loco voie 1/2), `POST /api/play?voie=1&sound=…`, `POST /api/loco?voie=1&id=…`, `POST /api/stop`, `POST /api/volume?v=0..100`, `POST /api/auto?on=0|1&min=…&max=…` (stub, étape 7).

## 5. Les 10 locos (`sounds/locos.json`)
Sons WAV sur microSD dans `sounds/<loco_id>/`, metadata JSON dans LittleFS (`/sounds/<loco_id>/sons.json`). Sections UI : `conduite`, `avertisseurs`, `equipements`, `annonce`.

| id | nom affiché | sons dispo | PSRAM (~Mo) | note |
|---|---|---|---|---|
| CC6500 | CC 6500 | 15 | ~4,7 | Sons ZIMO S01 mfx |
| BB67400 | BB 67400 | 13 | ~5,7 | 3 sons trop longs désactivés (attelage, frein_main, tanken) |
| BB26000 | BB 26000 Sybic | 15 | ~4,7 | Petit-cheval emblématique |
| BB9400 | BB 9400 | 16 | ~3,7 | Annonces Limoges + Ambérieux |
| CC21000 | CC 21000 | 13 | ~2,8 | Dérivée CC6500 ; annonces absentes du ZPP |
| CC72000 | CC 72000 | 16 | ~6,0 | Diesel ; loco la plus lourde (hors TGV) |
| BB9300 | BB 9300 | 15 | ~2,8 | Sons 8-bit re-extraits |
| BB16000 | BB 16000 | 13 | ~1,8 | Sons 8-bit re-extraits |
| 141R | 141 R | 13 | ~5,5 | Vapeur ; sifflets à vapeur |
| TGV | TGV PSE | 19 | ~7,3 | ESU (captures vidéo) ; `sons_firmware` = sons.json |

**TGV** : source ESU LokProgrammer (captures vidéo, pas ZIMO). 45 entrées dans `sons.json` : 26 transitions/boucles de vitesse marquées `"disponible": false` (trop lourdes ou inutiles en mode manuel), 19 restantes (~7,3 Mo) pour le firmware. Le champ `sons_firmware` dans `locos.json` pointe vers `sons.json` (firmware) au lieu de `sons_player.json` (séquenceur web).

Groupes exclusifs : un seul `avertisseurs` à la fois, une seule `annonce` à la fois.
Sons `"disponible": false` masqués dans l'UI et ignorés par le firmware.
Régénérer CC6500 : `python3 tools/prepare_sounds.py input/SNCF_CC6500_ZIMO_16-Bit_S01_mfx.zpp sounds`.

## 6. Interface (`firmware/data/www/index.html`, polices dans `fonts/`)
Page mobile **dynamique**, une colonne (max 480 px). Palette : bleu nuit `#1B2A49`, papier `#F4F1E8`, gris `#D8D5CA`, orange TGV `#E8761A`, rouge signal `#C8102E`, encre `#14203A`.
Polices : **PoliceSNCF** (titres, en-têtes de section, carrés numérotés, bouton Arrêt, étiquette « En cours ») ; **Univers LT Std 55** (libellés des boutons, volume, Wi-Fi). Un seul poids : ne pas mettre `font-weight:700`.
Structure : **bannière orange** (chargement en cours) → en-tête bleu (titre + sélecteur de loco) + filet orange → barre volume + bouton rouge **Arrêt** → 4 sections : 1 Conduite, 2 Avertisseurs et portières, 3 Équipements de bord, 4 Annonces en gare → pied de page (nombre de sons + id loco).
Tuile = bouton blanc, bordure bleue 2 px, triangle play ; active = fond orange « EN COURS » ; boucle = fond bleu « ↺ clic pour arrêter ». Cibles tactiles ≥ 44 px.
Pendant chargement loco : bannière orange visible + boutons grisés (`.sections-loading`) + polling 500 ms ; hors chargement : polling 2 s.
Sons `disponible: false` et `type: sequence` filtrés côté JS.
Ne reprends **pas** le logo SNCF (marque déposée). PoliceSNCF contient des glyphes de logo : ils sont retirés par `tools/convert_fonts.py`.

## 7. Doutes à lever (sons non écoutés à l'oreille)
- **Klaxons CC6500** : « hoch/tief » peuvent être inversés (fréquences 657 Hz et 1 095 Hz).
- **Annonces Corail CC6500** (14 s et 15 s) très tonales : vérifier qu'il y a bien de la voix et pas seulement un jingle.
- **Essai des freins, frein à main, KVB (50 Hz ?), pantographe CC6500** : association par analyse spectrale, pas à l'oreille.
- **BB16000 / BB9300** : sons 8-bit non signés, re-extraits et convertis — à écouter pour confirmer la qualité.
- Fréquence d'échantillonnage 22 050 Hz : confirmée pour ZIMO 16 bits par la doc du projet CC 6500.

## 8. Licences et prudence
Les projets ZIMO/ESU ont leurs conditions d'usage (usage privé avec décodeurs ; vérifier avant toute redistribution). ESU interdit la rétro-ingénierie : le format `.esux` est chiffré et n'a **pas** été contourné. Les polices Univers LT Std et PoliceSNCF sont fournies par l'utilisateur (usage privé) ; ne pas les republier.

## 9. Architecture firmware (`firmware/`)
PlatformIO, board `4d_systems_esp32s3_gen4_r8n16`, Arduino-ESP32 v3.

| fichier | rôle |
|---|---|
| `src/main.cpp` | setup/loop : SD → audio → Wi-Fi → webserver |
| `src/audio.cpp/.h` | mélangeur 6 voix, chargement async, I2S IDF v5 |
| `src/sd_manager.cpp/.h` | SPI2/FSPI GPIO10-13, fallback LittleFS |
| `src/wifi_manager.cpp/.h` | portail captif + NVS |
| `src/webserver.cpp` + `ln_webserver.h` | API REST + fichiers statiques |
| `data/www/index.html` | interface web embarquée |
| `data/sounds/locos.json` | catalogue locos (LittleFS) |
| `data/sounds/<loco>/sons.json` | metadata sons (LittleFS, WAV sur SD) |

**Points critiques :**
- WAV : parser RIFF complet (chunk scanner) — les WAV TGV ont un chunk `LIST` avant `data` ; l'offset fixe ne marche pas.
- `sons_firmware` dans locos.json : si présent, le firmware utilise ce fichier au lieu de `sons` (utile pour TGV dont `sons` = `sons_player.json`).
- Chargement asynchrone : `loco_load_task` priorité 1, `mixer_task` priorité 5, tous deux sur Core 1 ; `s_load_queue` (2 messages). `taskYIELD()` tous les 16 Ko pendant la lecture SD.
- PSRAM : toujours libérer intégralement avant de charger une nouvelle loco.

## 10. Contenu du dossier
`CLAUDE.md` (ce fichier) · `docs/` (PLAN, FORMATS, ZIMO_CC6500) · `tools/` (scripts testés, Python 3 + numpy + fonttools) · `sounds/` (10 locos WAV + locos.json) · `fonts/` (WOFF) · `ui/maquette.html` (maquette statique de référence) · `firmware/` (PlatformIO complet) · `input/` (fichiers sources : projets ZIMO, polices).
Installation Python : `pip install numpy fonttools`.
