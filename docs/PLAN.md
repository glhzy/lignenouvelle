# Plan : boîtier sonore Wi-Fi « Lignenouvelle »

> Document mis à jour au 4 novembre 2026 — v2 terminée. La v1 (sons en PSRAM) est archivée sur le tag git `v1-psram`.

## 1. Architecture générale

```
microSD (WAV)
    │ streaming via ring-buffers PSRAM
    ▼
ESP32-S3 N16R8  ──I2S──►  MAX98357A  ──►  HP 4 Ω 3 W
    │
    │  Wi-Fi (rejoint la box)
    ▼
Téléphone (navigateur web, aucune appli à installer)
```

- **ESP32-S3** : sert la page web + mélange les sons. **MAX98357A** : ampli I2S classe D → haut-parleur 4 Ω 3 W.
- **Flash 16 Mo** : programme (~3 Mo) + LittleFS (interface web, polices, métadonnées JSON).
- **microSD 32 Go** : fichiers WAV de toutes les locos (taille illimitée en v2 streaming).
- **PSRAM 8 Mo** : uniquement les ring-buffers de lecture (6 × 8 Ko = 48 Ko). Jamais les WAV complets.

## 2. Wi-Fi

**Mode normal** : l'ESP32 rejoint la box domestique. Adresse `lignenouvelle.local` (mDNS) + IP affichée au démarrage sur la console série. Recommander de réserver une IP fixe dans la box (le `.local` ne marche pas sur tous les téléphones).

**1re utilisation** : l'ESP32 crée un réseau temporaire `Lignenouvelle-Config`. L'utilisateur s'y connecte, ouvre `192.168.4.1`, saisit le SSID et le mot de passe de la box. Mémorisés en NVS (namespace `"wifi"`). Redémarrage automatique.

## 3. Moteur audio v2 — streaming SD

### Tâches FreeRTOS

| Tâche | Cœur | Priorité | Rôle |
|---|---|---|---|
| `mixer_task` | 1 | 5 | Mélange 6 voix PCM → I2S. Traite la file de commandes. |
| `stream_refill_task` | 1 | 3 | Recharge les ring-buffers toutes les 20 ms. Déclenche les automatismes. |
| Wi-Fi + webserver | 0 | — | Dépose des `AudioCmd` dans `s_cmd_queue` (16 messages). |

### Ring-buffers SPSC

4 096 int16_t PSRAM par voix = 8 Ko ≈ 185 ms. Indices `ring_rd`/`ring_wr` en `volatile uint32_t` — lock-free, débordement naturel 32 bits, masque `& 4095`. Pré-remplissage complet (~5 ms) à l'ouverture du fichier. Underrun = silence passager.

### Chargement loco

`parse_wav_header()` lit uniquement l'en-tête RIFF (offset + taille du chunk `data`). Scanner de chunks conservé pour gérer les fichiers TGV avec chunks `LIST` avant `data`. Durée : **~20 ms** pour 20 fichiers (au lieu de 10-25 s en v1). Changement instantané, dans la fenêtre DMA.

### Mélange

Bloc de 512 échantillons (≈ 23 ms). Chaque voix lit dans son ring-buffer, applique son fondu. Somme flottante → volume global (courbe quadratique 0–100 %) → plafond 80 % → conversion int16 → I2S stéréo (même canal G et D). Silence continu au repos (horloge I2S jamais coupée).

### Fondus et groupes

- Fondu entrant : 5 ms. Fondu sortant : 15 ms. Arrêt général : 100 ms.
- Groupes exclusifs par voie : tous les groupes sauf `ambiance` — un seul son actif à la fois par groupe (`conduite`, `avertisseurs`, `equipements`, `annonce`). Le groupe `ambiance` est non exclusif (superposition autorisée).
- Voix saturées : la plus ancienne voix jouante non bouclée est volée (`alloc_voice()`).

## 4. Séquenceur

Chaque son présenté dans l'interface est en réalité une **séquence** :

| Type de fichier | Source | Comportement |
|---|---|---|
| `sons_player.json` présent | TGV (et futures locos) | Séquences multi-étapes définies explicitement |
| Pas de `sons_player.json` | Toutes les autres locos | JS génère automatiquement une séquence à 1 étape depuis `sons.json` |

**Format `sons_player.json`** : tableau de séquences `{id, label, groupe, etapes:[{son, type:"once"/"loop", lbl}]}`. Le champ `son` est l'id sans extension.

**Machine d'état par voie** (`VoieSeq`) :
- `SEQ_IDLE` → aucune séquence
- `SEQ_ONCE` → étape `once` en cours, auto-avance dès que la voix passe IDLE (vérifiée dans `mixer_task` après chaque bloc)
- `SEQ_LOOP` → étape `loop` en boucle, attend `POST /api/seq/next`

**Saut d'étape indisponible** : `seq_play_step()` vérifie `find_sound()` ; si son absent, passe à l'étape suivante (récursif, limite = MAX_STEPS).

**Annulation propre** : `do_stop(son actif)` annule la séquence. Pour l'avance interne, `cur_son` est vidé *avant* l'appel à `do_stop` → pas d'annulation parasite.

## 5. Automatismes

Lectures aléatoires périodiques de sons d'un groupe (`annonce` ou `ambiance`) sans intervention de l'utilisateur.

**Struct `AutoMode`** : `{enabled, min_s, max_s, voie, next_ms}`. Deux instances : `s_auto[0]` = annonce, `s_auto[1]` = ambiance.

**Déclenchement** (dans `stream_refill_task`, toutes les 20 ms) :
1. Si `next_ms == 0` → initialiser le timer à un instant aléatoire dans `[min_s, max_s]` (évite la rafale au démarrage).
2. Si `millis() >= next_ms` → collecter tous les sons du groupe dans `s_sounds[]`, tirer un index aléatoire (`esp_random() % count`), envoyer `CMD_PLAY`.
3. Planifier le prochain déclenchement : `next_ms = now + random(min_s, max_s) * 1000`.

**Changement de loco** : les automatismes ne sont pas réinitialisés — ils continuent avec les sons de la nouvelle loco.

**Persistance NVS** : clés `a_annonce_on/mn/mx` et `a_ambiance_on/mn/mx`, namespace `"lignenouvelle"`.

## 6. Persistance NVS

Namespace **`"lignenouvelle"`** (séparé de `"wifi"` utilisé par le gestionnaire Wi-Fi) :

| Clé | Type | Défaut | Écrite par |
|---|---|---|---|
| `loco` | String (16) | `CC6500` | `POST /api/loco?voie=1` |
| `volume` | UChar | 70 | `POST /api/volume` |
| `a_annonce_on` | Bool | false | `POST /api/auto?groupe=annonce` |
| `a_annonce_mn` | UShort | 60 | idem |
| `a_annonce_mx` | UShort | 300 | idem |
| `a_ambiance_on` | Bool | false | `POST /api/auto?groupe=ambiance` |
| `a_ambiance_mn` | UShort | 120 | idem |
| `a_ambiance_mx` | UShort | 600 | idem |

Lecture dans `main.cpp` setup(), avant `audio_begin()`. Voie 2 non persistée (même loco que voie 1 actuellement).

## 7. API REST v2 (complète)

| Endpoint | Méthode | Description |
|---|---|---|
| `/api/state` | GET | Volume, loco voie 1/2, séquences actives, état automatismes |
| `/api/locos` | GET | Catalogue locos (`locos.json`) |
| `/api/seq?voie=1&id=X` | POST | Démarrer/basculer une séquence |
| `/api/seq/next?voie=1` | POST | Avancer boucle → étape suivante |
| `/api/stop` | POST | Arrêt général (fondus 100 ms) |
| `/api/volume?v=0..100` | POST | Volume général |
| `/api/loco?voie=1&id=X` | POST | Changer de loco (instantané) |
| `/api/auto?groupe=annonce&on=0\|1&min=60&max=300` | POST | Configurer un automatisme |
| `/api/play?voie=1&sound=X` | POST | Jouer un son brut (sans séquenceur) |

**Format `/api/state`** :
```json
{
  "volume": 70,
  "voie1": {
    "loco": "CC6500",
    "playing": ["compresseur"],
    "seq": {"id": "drive_cycle", "step": 2, "steps": 7, "loop": false}
  },
  "voie2": { "loco": "CC6500", "playing": [], "seq": null },
  "auto": {
    "annonce":  {"on": true,  "min": 120, "max": 360},
    "ambiance": {"on": false, "min": 120, "max": 600}
  }
}
```

## 8. Interface web (`firmware/data/www/index.html`)

Page mobile, une colonne (max 480 px), polices PoliceSNCF + Univers LT Std 55.

**Structure** :
1. En-tête bleu : titre + sélecteur de loco (`<select class="loco-sel">`)
2. Filet orange
3. Volume (slider) + Arrêt général (bouton rouge)
4. **Panneau Automatismes** : 2 lignes (Annonces / Ambiances), bouton Off/On + menu fréquence
5. 4 sections numérotées : Conduite, Avertisseurs, Équipements de bord, Annonces en gare
6. Pied de page : nombre de sons · nom de loco · état actif

**Tuile séquence** (`.t`) :
- Simple (1 étape) : tuile demi-largeur, triangle play à droite
- Multi-étapes : tuile pleine largeur (`.t.w`), barre d'étapes (`.seq-bar`)
- Active → fond orange (`.t.on`) ; étape `once` en cours → opacité 0,65 (`.t.busy`)
- Barre d'étapes : pastilles `.step.cur/.past/.future`, séparateurs `.step-sep`

**Classe JS `SeqPlayer`** :
- États : `stopped / once / looping / stopping`
- `press()` : si `stopped` → `_start()` ; si `looping` → `_advance()`
- `syncFrom(seqInfo)` : appelé toutes les 2 s depuis `/api/state` pour resynchroniser
- `loadSeqs(loco)` : essaie `sons_player.json`, sinon génère des séquences 1-étape depuis `sons.json`

## 9. Préparation des sons

Sons WAV mono 16 bits 22 050 Hz, normalisés (RMS 0,10, pic ≤ 0,95), fondus 8 ms. Générés par `tools/prepare_sounds.py` depuis les fichiers `.zpp` ZIMO.

```bash
python3 tools/prepare_sounds.py input/SNCF_CC6500_ZIMO_16-Bit_S01_mfx.zpp sounds
```

Les fichiers WAV vont sur la **microSD** dans `sounds/<loco_id>/`. Les métadonnées JSON (`sons.json`, `sons_player.json`) vont dans **LittleFS** (`firmware/data/sounds/<loco_id>/`).

Pour les locos avec sons 8 bits non signés (BB16000, BB9300), une passe de conversion supplémentaire est appliquée lors de l'extraction.

## 10. Ordre de réalisation (historique v2)

| Étape | Contenu | Date |
|---|---|---|
| 7 | Moteur audio streaming SD (ring-buffers, `stream_refill_task`) | 4 nov. 2026 |
| 8 | Séquenceur (`sons_player.json`, machine d'état, auto-avance) | 4 nov. 2026 |
| 9 | Interface tgv_player (SeqPlayer JS, barre d'étapes, REST) | 4 nov. 2026 |
| 10 | Persistance NVS (volume + loco + automatismes) | 4 nov. 2026 |
| 11 | Automatismes (timer aléatoire, panneau UI, NVS) | 4 nov. 2026 |
| 12 | Tests longue durée | à faire avec le matériel |

## 11. Développement sans ESP32 — `tools/mock_server.py`

Simule l'ESP32 côté logiciel : sert l'interface web et répond à toutes les API REST avec un état en mémoire. Pas de dépendance externe (stdlib Python uniquement).

```bash
# Depuis la racine du projet :
python3 tools/mock_server.py        # http://localhost:8080/
python3 tools/mock_server.py 9090   # port personnalisé
```

**Données servies depuis le disque :**
- `firmware/data/sounds/locos.json` → `/api/locos`
- `firmware/data/sounds/<loco>/sons.json` → `/sounds/<loco>/sons.json`
- `firmware/data/sounds/<loco>/sons_player.json` → idem si présent
- `firmware/data/www/` → tous les fichiers statiques (HTML, polices)

**État simulé en mémoire :** volume, loco courante, séquence active (avec vraies étapes depuis `sons_player.json`), automatismes on/off.

**Limites :** aucun son ne sort (l'audio nécessite l'ESP32 + MAX98357A). Utiliser `afplay sounds/<loco>/<fichier>.wav` pour écouter les sons bruts.

## 12. Tests à réaliser (étape 12, avec l'ESP32)

- **Son au démarrage** : loco et volume corrects restaurés depuis NVS.
- **3 séquences simultanées** : pas de saturation, fondus propres.
- **Boucle longue** (compresseur ou ambiance) : pas de dérive ni de coupure au bout de 30+ min.
- **Wi-Fi très sollicité** (refresh rapide de la page) : pas de saccades audio (les deux cœurs sont séparés).
- **Changement rapide de loco** : pas de crash, sons de l'ancienne loco coupés proprement.
- **Automatismes** : vérifier que les sons se déclenchent dans l'intervalle configuré, pas de doublon simultané dans le groupe exclusif `annonce`.
- **Redémarrage NVS** : loco, volume, état automatismes restaurés correctement.
