# Lignenouvelle

**Boîtier sonore Wi-Fi pour réseau miniature** — sons de locomotives SNCF commandés depuis un téléphone, sans application à installer.

> Projet DIY basé sur un ESP32-S3. L'ESP32 sert lui-même une page web : on ouvre `lignenouvelle.local` sur n'importe quel navigateur et on déclenche les sons à la volée.

---

## Démonstration

*(photos et vidéo à venir)*

---

## Fonctionnalités

- **10 locomotives** avec leurs sons authentiques : CC 6500, BB 67400, BB 26000 Sybic, BB 9400, CC 72000, CC 21000, BB 16000, BB 9300, 141 R vapeur, TGV PSE
- **Séquenceur multi-étapes** : enchaînement automatique démarrage → marche → arrêt, avec boucles pilotées manuellement
- **6 voix simultanées** — plusieurs sons superposables (klaxon + ambiance + annonce...)
- **Streaming depuis microSD** — aucune limite de taille, les WAV ne sont jamais chargés entièrement en RAM
- **Interface web responsive** — carousel de locos, boutons séquences avec barre d'étapes, slider de volume
- **Connexion Wi-Fi** à votre box (portail captif à la première utilisation pour saisir le SSID/mot de passe)
- **Persistance** : loco active, volume et automatismes mémorisés entre les redémarrages (NVS)

---

## Matériel (~25 €)

| Composant | Référence | Prix indicatif |
|---|---|---|
| Microcontrôleur | ESP32-S3-DevKitC-1 N16R8 (16 Mo flash, 8 Mo PSRAM) | ~9 € |
| Amplificateur | MAX98357A (I2S, classe D) | ~3 € |
| Haut-parleur | 4 Ω 3 W (ex. CQRobot 70×31 mm) | ~5 € |
| Stockage | Adaptateur microSD SPI + carte 32 Go | ~5 € |
| Divers | Fils Dupont F-F, câble USB-C données | ~3 € |

---

## Câblage

### ESP32-S3 → MAX98357A (I2S)

| ESP32-S3 | MAX98357A | Fil |
|---|---|---|
| GPIO 4 | DIN | orange |
| GPIO 5 | BCLK | marron |
| GPIO 6 | LRC | gris |
| 3V3 | Vin | rouge |
| 3V3 | **SD** (obligatoire) | rouge |
| GND | GND | noir |

> ⚠️ La broche SD doit être reliée à 3V3. Laissée flottante, l'ampli entre en mode shutdown et ne produit aucun son.

Le haut-parleur se branche sur le bornier vert du MAX98357A.

### ESP32-S3 → lecteur microSD (SPI)

| ESP32-S3 | microSD | Fil |
|---|---|---|
| GPIO 10 | CS | jaune |
| GPIO 11 | MOSI | bleu |
| GPIO 12 | CLK | blanc |
| GPIO 13 | MISO | vert |
| 3V3 | 3V3 | rouge |
| GND | GND | noir |

> ⚠️ Ne pas utiliser les GPIO 35, 36, 37 sur la variante N16R8.

---

## Installation

### Prérequis

- [PlatformIO](https://platformio.org/) (extension VS Code ou CLI)
- Python 3 + `pip install numpy fonttools` (pour les scripts `tools/`)

### Compilation et flash

```bash
git clone https://github.com/glhzy/lignenouvelle.git
cd lignenouvelle/firmware

# Compiler et flasher le firmware
pio run -t upload

# Uploader l'interface web + les fichiers JSON (LittleFS)
pio run -t uploadfs
```

### Première utilisation

Au premier démarrage, l'ESP32 crée un point d'accès Wi-Fi `Lignenouvelle`. Connectez-vous, saisissez votre SSID et mot de passe. L'ESP32 redémarre et rejoint votre réseau. Ouvrez ensuite `http://lignenouvelle.local/` (ou l'IP affichée dans les logs série).

---

## Sons

Les fichiers WAV sont à placer sur la microSD dans `sounds/<loco_id>/`. Les métadonnées JSON sont dans `firmware/data/sounds/<loco_id>/sons.json` (chargées dans la flash LittleFS du firmware).

Les sons de ce dépôt proviennent de projets décodeurs ZIMO et ESU (usage privé avec les décodeurs correspondants). **Ils ne sont pas redistribués ici.** Voir `tools/prepare_sounds.py` pour extraire vos propres fichiers ZPP ZIMO.

Structure sur la microSD :

```
/sounds/
  CC6500/    ← 15 WAV (klaxons, annonces Corail, ambiances...)
  BB67400/   ← 13 WAV
  TGV/       ← 45 WAV (séquences de marche, freins, annonces gare...)
  ...
```

---

## Architecture

```
firmware/
  src/
    main.cpp          — setup/loop, initialisation dans l'ordre SD → audio → Wi-Fi → web
    audio.cpp/.h      — moteur audio 6 voix, ring-buffers PSRAM, I2S, séquenceur
    sd_manager.cpp/.h — accès SPI microSD
    wifi_manager.cpp/.h — portail captif + NVS
    webserver.cpp     — API REST + fichiers statiques
  data/
    www/index.html    — interface web embarquée
    sounds/           — métadonnées JSON (locos.json + sons.json par loco)
```

**Tâches FreeRTOS :**
- `mixer_task` (Core 1, prio 5) — mixage 6 voix PCM → I2S
- `stream_refill_task` (Core 1, prio 3) — recharge les ring-buffers depuis la microSD
- Serveur web (Core 0) — dépose les commandes dans une queue FreeRTOS

**API REST :**

| Méthode | Endpoint | Rôle |
|---|---|---|
| GET | `/api/locos` | Liste des locos |
| GET | `/api/state` | État complet (séquences, volume, loco, automatismes) |
| POST | `/api/seq?voie=1&id=…` | Démarrer / arrêter une séquence |
| POST | `/api/seq/next?voie=1&id=…` | Avancer d'une étape |
| POST | `/api/stop` | Arrêt général |
| POST | `/api/volume?v=0..100` | Régler le volume |
| POST | `/api/loco?voie=1&id=…` | Changer de locomotive |

---

## Tester sans matériel

Un serveur de simulation permet de tester l'interface web sans ESP32 :

```bash
python3 tools/mock_server.py   # port 8080 par défaut
# ouvrir http://localhost:8080/
```

---

## Notes

- Sons ZIMO 16 bits signés à 22 050 Hz. Les projets BB 16000 et BB 9300 utilisent des sons 8 bits non signés (convertis automatiquement par `tools/zpp16_extract.py`).
- La loco TGV PSE utilise un fichier `sons_player.json` avec 20 séquences multi-étapes. Les autres locos génèrent leurs séquences automatiquement depuis `sons.json`.
- Testé sur ESP32-S3-DevKitC-1 N16R8 (Heemol). D'autres variantes ESP32-S3 avec PSRAM octale devraient fonctionner avec quelques ajustements dans `platformio.ini`.
