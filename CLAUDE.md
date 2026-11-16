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
- **Streaming depuis la SD pour toutes les locos** (v2, en cours). Les WAV ne sont jamais chargés en PSRAM : chaque voix active lit directement depuis la SD via un petit ring-buffer en PSRAM (8 Ko ≈ 185 ms). La PSRAM sert uniquement de buffer de lecture, pas de stockage complet. Supprime la limite 8 Mo et le délai de chargement. *(v1 archivée sur git : tag `v1-psram`.)*
- **Séquenceur intégré** (v2) : les sons sont organisés en séquences multi-étapes (`sons_player.json`). Les sons enchaînent automatiquement ; les boucles attendent une commande `next`. Toutes les locos utilisent ce format — locos sans `sons_player.json` : le JS convertit `sons.json` en séquences à une étape.
- **Automatismes : implémentés** (étape 11) — timer aléatoire par groupe (`annonce` / `ambiance`), interrupteur + fréquence dans l'UI, persistance NVS.
- **Mise à jour des sons via navigateur : reportée** à une version 2 (prévoir mot de passe si ajoutée).
- 10 locos disponibles (table §5). Le « disjoncteur » de la BB 16000 est **écarté** (association nom/son non vérifiable).
- Interface : style **tgv_player.html** (voir §6), polices PoliceSNCF + Univers LT Std embarquées. Boutons séquences avec barre d'étapes.
- **Sélecteur de locomotive** dans l'en-tête — changement instantané (plus de chargement PSRAM). Deux voies dans l'API ; les deux pointent sur la même loco pour l'instant.

## 4. État d'avancement (4 novembre 2026)
**v1 archivée (tag git `v1-psram`). v2 terminée : streaming SD + séquenceur + interface + NVS + automatismes.**

**Terminé (v1) :**
1. ✅ Sons extraits pour 10 locos ; `sounds/<loco>/sons.json` + `sounds/locos.json`.
2. ✅ Firmware PlatformIO, board `4d_systems_esp32s3_gen4_r8n16` (qio_opi, BOARD_HAS_PSRAM).
3. ✅ Moteur audio 6 voix PSRAM, groupes exclusifs, fondus, plafond 80 %, Core 1.
4. ✅ Wi-Fi + portail captif, mDNS `lignenouvelle.local`.
5. ✅ Serveur web + API REST + interface dynamique, sélecteur de loco.
6. ✅ Dépôts GitHub : public (`lignenouvelle`) + privé assets (`lignenouvelle-assets`).

**Terminé (v2) :**
7. ✅ Moteur audio streaming SD (ring-buffer par voix, `stream_refill_task`) — **4 nov. 2026**.
8. ✅ Séquenceur (machine d'état par voie, `sons_player.json`, API `seq`/`seq/next`) — **4 nov. 2026**.
9. ✅ Interface tgv_player style (boutons séquences + barre d'étapes, toutes locos) — **4 nov. 2026**.
10. ✅ Persistance NVS (volume + loco + automatismes au redémarrage) — **4 nov. 2026**.
11. ✅ Automatismes (timer aléatoire annonce/ambiance, interrupteur + fréquence) — **4 nov. 2026**.

**À faire :**
12. Tests longue durée (3 séquences simultanées, boucle longue, Wi-Fi sollicité) — à faire avec le matériel en main.

**API v2 complète :**
`GET /api/locos`, `GET /api/state` (séquences actives, volume, loco voie 1/2, état automatismes),
`POST /api/seq?voie=1&id=…` (démarrer séquence),
`POST /api/seq/next?voie=1` (avancer boucle → étape suivante),
`POST /api/stop`, `POST /api/volume?v=0..100`, `POST /api/loco?voie=1&id=…`,
`POST /api/auto?groupe=annonce&on=0|1&min=60&max=300` (automatisme par groupe).

## 5. Les 10 locos (`sounds/locos.json`)
Sons WAV sur microSD dans `sounds/<loco_id>/`. Metadata JSON dans LittleFS (`data/sounds/<loco_id>/sons.json`). Séquences dans `sons_player.json` si présent. Sections UI : `conduite`, `avertisseurs`, `equipements`, `annonce`.

En v2 streaming, **aucune limite de taille** : les WAV sont lus en continu depuis la SD, jamais intégralement en PSRAM.

| id | nom affiché | sons dispo | sons_player.json | note |
|---|---|---|---|---|
| CC6500 | CC 6500 | 15 | non (à créer) | Sons ZIMO S01 mfx |
| BB67400 | BB 67400 | 13 | non (à créer) | 3 sons trop longs désactivés (attelage, frein_main, tanken) |
| BB26000 | BB 26000 Sybic | 15 | non (à créer) | Petit-cheval emblématique |
| BB9400 | BB 9400 | 16 | non (à créer) | Annonces Limoges + Ambérieux |
| CC21000 | CC 21000 | 13 | non (à créer) | Dérivée CC6500 ; annonces absentes du ZPP |
| CC72000 | CC 72000 | 16 | non (à créer) | Diesel |
| BB9300 | BB 9300 | 15 | non (à créer) | Sons 8-bit re-extraits |
| BB16000 | BB 16000 | 13 | non (à créer) | Sons 8-bit re-extraits |
| 141R | 141 R | 13 | non (à créer) | Vapeur ; sifflets à vapeur |
| TGV | TGV PSE | 19 | **oui** (14,4 Mo total) | ESU ; `sons_firmware`=sons.json ; séquences multi-étapes |

**TGV** : source ESU LokProgrammer (captures vidéo, pas ZIMO). 45 entrées dans `sons.json` : 26 transitions/boucles de vitesse marquées `"disponible": false`, 19 disponibles. `sons_player.json` contient 19 séquences avec boucles multi-étapes (ex. `drive_cycle` = 7 étapes). En v2 streaming, les 44 WAV (14,4 Mo total) tiennent sans problème.

**Locos sans `sons_player.json`** : le JS de l'interface convertit automatiquement `sons.json` en séquences à une seule étape (comportement identique à la v1 pour l'utilisateur).

Groupes exclusifs : tous les groupes sauf `ambiance` — un seul son actif à la fois par groupe et par voie (`conduite`, `avertisseurs`, `equipements`, `annonce`). Le groupe `ambiance` est non exclusif (superposition autorisée).
Sons `"disponible": false` masqués dans l'UI et ignorés par le firmware.
Régénérer CC6500 : `python3 tools/prepare_sounds.py input/SNCF_CC6500_ZIMO_16-Bit_S01_mfx.zpp sounds`.

## 6. Interface v2 (`firmware/data/www/index.html`, polices dans `fonts/`)
Modèle de référence : `ui/tgv_player.html` (player TGV statique, validé visuellement).
Page mobile **dynamique**, une colonne (max 480 px). Palette : bleu nuit `#1B2A49`, papier `#F4F1E8`, gris `#D8D5CA`, orange TGV `#E8761A`, rouge signal `#C8102E`, encre `#14203A`. Couleurs sections : `--c-cond:#5C6BC0`, `--c-avert:#C8102E`, `--c-equip:#2D7D46`, `--c-ann:#E8761A`.
Polices : **PoliceSNCF** (titres, en-têtes de section, carrés numérotés, bouton Arrêt) ; **Univers LT Std 55** (libellés des boutons, volume). Un seul poids : ne pas mettre `font-weight:700`.

**Structure** : en-tête bleu (titre + sélecteur de loco) + filet orange → barre volume + bouton rouge **Arrêt** → panneau **Automatismes** → 4 sections numérotées → pied de page. *Plus de bannière de chargement (changement de loco instantané en v2).*

**Panneau Automatismes** (`.auto-panel`) : deux lignes (Annonces / Ambiances), chacune avec un bouton Off/On (`.auto-tog`) et un menu déroulant fréquence (`.auto-freq`, presets ~15 s → ~10 min). État synchronisé depuis `/api/state` au polling.

**Boutons séquences** : chaque séquence est une tuile (classe `.t`). Si séquence multi-étapes : barre d'étapes (`.seq-bar`) sous le bouton, pastilles colorées (`.step.cur/.past/.future`). Tuile active = fond orange (`.t.on`) ; boucle en attente = bord en pointillé. Tuile désactivée (`.t.busy`) = opacité 0,65.

**Classe JS `SeqPlayer`** (du tgv_player.html) : états `stopped/looping/once/stopping`, méthodes `press()` et `advance()`. À adapter pour appels REST (`POST /api/seq`, `POST /api/seq/next`).

**Locos sans `sons_player.json`** : le JS crée des séquences à 1 étape depuis `sons.json` → tuile simple, pas de barre d'étapes.

Cibles tactiles ≥ 44 px. Sons `disponible: false` filtrés côté JS.
Ne reprends **pas** le logo SNCF (marque déposée). PoliceSNCF contient des glyphes de logo : retirés par `tools/convert_fonts.py`.

## 7. Doutes à lever (sons non écoutés à l'oreille)
- **Klaxons CC6500** : « hoch/tief » peuvent être inversés (fréquences 657 Hz et 1 095 Hz).
- **Annonces Corail CC6500** (14 s et 15 s) très tonales : vérifier qu'il y a bien de la voix et pas seulement un jingle.
- **Essai des freins, frein à main, KVB (50 Hz ?), pantographe CC6500** : association par analyse spectrale, pas à l'oreille.
- **BB16000 / BB9300** : sons 8-bit non signés, re-extraits et convertis — à écouter pour confirmer la qualité.
- Fréquence d'échantillonnage 22 050 Hz : confirmée pour ZIMO 16 bits par la doc du projet CC 6500.

## 8. Licences et prudence
Les projets ZIMO/ESU ont leurs conditions d'usage (usage privé avec décodeurs ; vérifier avant toute redistribution). ESU interdit la rétro-ingénierie : le format `.esux` est chiffré et n'a **pas** été contourné. Les polices Univers LT Std et PoliceSNCF sont fournies par l'utilisateur (usage privé) ; ne pas les republier.

## 9. Architecture firmware v2 (`firmware/`)
PlatformIO, board `4d_systems_esp32s3_gen4_r8n16`, Arduino-ESP32 v3.

| fichier | rôle |
|---|---|
| `src/main.cpp` | setup/loop : SD → audio → Wi-Fi → webserver |
| `src/audio.cpp/.h` | mélangeur 6 voix streaming, ring-buffers, I2S IDF v5 |
| `src/sd_manager.cpp/.h` | SPI2/FSPI GPIO10-13, fallback LittleFS |
| `src/wifi_manager.cpp/.h` | portail captif + NVS |
| `src/webserver.cpp` + `ln_webserver.h` | API REST + fichiers statiques |
| `data/www/index.html` | interface web embarquée |
| `data/sounds/locos.json` | catalogue locos (LittleFS) |
| `data/sounds/<loco>/sons.json` | metadata sons (LittleFS) |
| `data/sounds/<loco>/sons_player.json` | séquences multi-étapes (LittleFS, TGV uniquement pour l'instant) |

**Tâches FreeRTOS (v2, implémentées) :**
- `mixer_task` — Core 1, priorité 5. Mélange 6 voix PCM → I2S. Lit depuis ring-buffers.
- `stream_refill_task` — Core 1, priorité 3. Vérifie toutes les 20 ms ; recharge les ring-buffers si `avail < 50 %`. Ferme les fichiers SD des voix passées IDLE.
- Wi-Fi / webserver — Core 0. Dépose commandes dans `s_cmd_queue` (file 16 msgs).
- `loco_load_task` **supprimé** (plus de chargement PSRAM).

**Ring-buffer SPSC par voix :** 8 Ko PSRAM (4 096 int16_t). Indices `ring_rd`/`ring_wr` en `volatile uint32_t` — lock-free, débordement naturel, masque `& RING_MASK (4095)`. La voix pre-fill (~5 ms) à l'ouverture dans `do_play`. Underrun = silence passager. Changement de loco : ferme fichiers, reset ring, recharge métadonnées (~20 ms, dans la fenêtre DMA).

**Structures audio :**
- `SoundDef` : `key`, `id`, `loco_id`, `groupe`, `wav_path`, `data_offset`, `data_size`, `base_level`, `loop` — **pas de pointeur PCM**.
- `Voice` : `def`, `vol/vol_target/fade_step`, `state`, `voie`, `ring_buf/ring_rd/ring_wr`, `sd_file`, `file_pos`, `stream_eof`.

**Chargement loco :** `parse_wav_header()` lit l'en-tête RIFF de chaque WAV (offset + taille du chunk `data`). Le chunk scanner v1 est conservé (gère `LIST` avant `data` sur TGV). Résultat : ~20 ms pour 20 fichiers au lieu de 10-25 s en v1. Mémoire PSRAM : 48 Ko (6 ring-buffers) au lieu de ~5 Mo.

**Points critiques :**
- `sons_firmware` dans locos.json : firmware utilise ce fichier plutôt que `sons` (TGV : `sons`=séquenceur JS, `sons_firmware`=liste plate pour le firmware).
- `ring_space()` retourne `RING_SAMPLES - ring_avail - 1` (évite l'ambiguïté plein/vide du SPSC).
- `alloc_voice()` ferme le fichier SD de la voix volée (non-boucle en cours).

**Séquenceur (étape 8) :**
- Structures : `SeqStep {son[32], loop}`, `SeqDef {id[32], groupe[16], steps[12], num_steps}`, `VoieSeq {state, seq_idx, step_idx, cur_son[32]}`.
- États : `SEQ_IDLE`, `SEQ_ONCE` (étape `once` en cours, auto-avance à la fin), `SEQ_LOOP` (étape `loop` en cours, attend `next`).
- `load_loco_seqs()` : tente `/sounds/<loco>/sons_player.json` ; sinon génère 1 séquence par son (fallback). Appelé après `load_loco_sounds()` dans `audio_begin` et `CMD_LOCO`.
- Auto-avance : vérifiée dans `mixer_task` après chaque bloc (23 ms). Si `SEQ_ONCE` et le son actif n'est plus en `VOICE_PLAYING` → `seq_advance_internal()`.
- Saut étape indisponible : `seq_play_step()` vérifie `find_sound()` ; si absent, passe à l'étape suivante (récursif, profondeur max = `MAX_STEPS`).
- Annulation séquence : `do_stop(son actif)` → annule ; `do_stop_all()` / `do_stop_voie()` → annule. Avance interne : `cur_son` vidé avant l'appel à `do_stop`, donc le séquenceur n'est pas annulé.
- Bascule : `CMD_SEQ_START` sur la même séquence déjà active → arrêt.

**Persistance NVS (étape 10) :**
- Namespace `"lignenouvelle"` (séparé des credentials Wi-Fi dans `"wifi"`).
- Clés : `loco` (String, défaut CC6500), `volume` (UChar, défaut 70), `a_annonce_on/mn/mx`, `a_ambiance_on/mn/mx`.
- Lecture dans `main.cpp` setup(), avant `audio_begin()`. Écriture dans `webserver.cpp` à chaque `POST /api/volume`, `POST /api/loco?voie=1`, `POST /api/auto`.
- Voie 2 non persistée (pointe sur la même loco que la voie 1 actuellement).

**Automatismes (étape 11) :**
- Struct `AutoMode {enabled, min_s, max_s, voie, next_ms}`, tableau `s_auto[2]` ([0]=annonce, [1]=ambiance).
- Timer vérifié dans `stream_refill_task` toutes les 20 ms (`auto_check()`). Au déclenchement : liste tous les sons du groupe dans `s_sounds[]`, tire un index au hasard (`esp_random()`), envoie `CMD_PLAY` dans la file. Planifie le prochain déclenchement : délai aléatoire dans `[min_s, max_s]`.
- Premier tick (next_ms == 0) : initialise le timer à un instant aléatoire dans [min, max] — évite la rafale au démarrage.
- `audio_auto_set(groupe, on, min_s, max_s, voie)` : écriture directe dans `s_auto[]` (pas de queue). Réinitialise `next_ms = 0`.
- `CMD_LOCO` ne remet pas les automatismes à zéro : ils continuent avec les sons de la nouvelle loco.

## 10. Contenu du dossier
`CLAUDE.md` (ce fichier) · `docs/` (PLAN, FORMATS, ZIMO_CC6500) · `tools/` (scripts testés, Python 3 + numpy + fonttools) · `sounds/` (10 locos WAV + locos.json) · `fonts/` (WOFF) · `ui/maquette.html` (maquette statique de référence) · `firmware/` (PlatformIO complet) · `input/` (fichiers sources : projets ZIMO, polices).
Installation Python : `pip install numpy fonttools`.

## 11. Développement sans ESP32 — serveur de maquette

`tools/mock_server.py` simule l'ESP32 côté logiciel : il sert l'interface web **et** répond à toutes les API REST avec un état en mémoire. Permet de tester l'UI sans matériel.

```bash
python3 tools/mock_server.py          # port 8080 par défaut
python3 tools/mock_server.py 9090     # port personnalisé
# ouvrir http://localhost:8080/
```

**Ce qu'on peut tester avec le mock :**
- Affichage de toutes les locos (depuis les vrais `locos.json` et `sons.json`)
- Navigation entre locos (sélecteur, chargement instantané des boutons)
- Boutons séquences : passage en orange, barre d'étapes TGV, avance au clic suivant
- Panneau Automatismes : bascule On/Off, mémorisation de la fréquence
- Slider volume, bouton Arrêt général
- Polling toutes les 2 s (synchronisation d'état)

**Ce qui nécessite le vrai boîtier :**
- Audio (le son sort du MAX98357A, pas du navigateur)
- Fondus, groupes exclusifs, underrun ring-buffer
- Streaming SD longue durée (étape 12)

**Architecture du mock :**
- `GET /api/state` → état mémoire (volume, loco, seq, auto)
- `GET /api/locos` → lit `firmware/data/sounds/locos.json`
- `GET /sounds/<loco>/sons.json` → lit `firmware/data/sounds/<loco>/sons.json`
- `GET /sounds/<loco>/sons_player.json` → idem si présent (TGV)
- `POST /api/seq` → simule la bascule start/stop, charge les étapes réelles depuis `sons_player.json`
- `POST /api/seq/next` → avance l'étape dans l'état mémoire
- `POST /api/volume`, `POST /api/loco`, `POST /api/stop`, `POST /api/auto` → mise à jour état mémoire

Le terminal affiche chaque commande reçue (`seq START`, `loco →`, `volume →`, etc.) pour suivre ce que fait l'interface.
