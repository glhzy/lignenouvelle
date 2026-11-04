# Formats de fichiers : ce qui a été découvert

Tout ce qui suit vient d'une analyse binaire (aucune documentation officielle). Niveau de confiance indiqué.

## 1. Projet ZIMO 16 bits (`SNCF_CC6500_ZIMO_16-Bit_S01_mfx.zpp`, ~15 Mo) — confiance élevée
- Échantillons : **PCM 16 bits little-endian, mono, 22 050 Hz** (la doc du projet : « 16 bits, 22 kHz »).
- Début du fichier : en-tête `SP…`, puis une grande zone de paramètres (≈ 0x80–0x20000, entrées de 26 octets) **non décodée** (affectations de touches, volumes, boucles ; les CV sont dans la doc PDF du projet).
- **Blocs audio** alignés sur des pages de 0x100 octets. À `page+0x80` : en-tête de 10 octets
  `drapeau(1) | fin(3, LE) | boucle_début(3, LE) | boucle_fin(3, LE)` ; données à `page+0x8A` jusqu'à l'adresse absolue `fin`.
  Drapeaux observés : `0x04/0x84/0x00` = sans boucle, `0x8C/0xCC` = boucle (début/fin distincts). Une zone de 0x84 octets sépare la fin des données du bloc suivant.
- **Répertoire final** (≈ offset 0xE6A47A pour ce fichier) : entrées `[00][slot][00 00 00 00][longueur][nom]` ; **le numéro de slot précède le nom**. Noms en cp1252. 59 entrées. Ces slots = « soundnumber » des CV de touches (ex. F10 → CV 540).
- **Le nom d'un son est rangé APRÈS ses données** (il se trouve dans la zone de 0x84 octets qui suit la fin du bloc) : le nom situé juste avant l'en-tête d'un bloc appartient au bloc **précédent**.
  Preuves spectrales : « Bremsen » = bloc tonal à 4,8 kHz (grincement) ; « Sifflet_SNCF_2_echo » = tonal 2,8 kHz ; « Zisch_02 » = bruit blanc 7 kHz ; « Sanden_kurz » = bruit à 6,7 kHz ; « E-Motor_fade-in » = ronflement 220 Hz ; « Klaxon_* » = tons purs ; « VACMA » = bips. Et les slots lus (18, 19, 24, 26, 57, 58, 62…) concordent avec la doc du projet.
- 80 blocs : 38 nommés ; 42 sans nom = engrenages du graduateur `Schaltw_1…20` (≈ 0,65 s chacun), `Silence`, boucles du moteur électrique (non identifiables un par un).
- Script : `tools/zpp16_extract.py` (extraction complète) et `tools/prepare_sounds.py` (sélection du décor).

## 2. Projet ZIMO 8 bits (`ZIMO_SNCF_BB16000.zpp`, 2,2 Mo) — confiance moyenne
- Échantillons **8 bits non signés centrés sur 128**, 22 050 Hz supposé. Source : base ZIMO (diffusé par YouChoos, gratuit).
- Bloc : octet longueur + nom, puis en-tête d'environ 0x5C octets (`04 | fin(3) | 0a xx xx | fin(3)`), puis données ; répertoire final vers 0x22199C ; premier bloc `P039-BB16000_F8` (données dès 0x218A).
- **L'association nom → données n'est pas vérifiée** (même décalage possible que pour le projet 16 bits ; le sifflet « Schaffnerpfiff » semble correct sans décalage, les klaxons ambigus). Écouter avant d'utiliser. Script : `tools/zpp8_extract.py`.

## 3. Projet ESU LokSound V4.0 (`56401-LSV4_0-Elok-SNCF-TGV-V1-R1.esux`, 4,1 Mo) — extraction impossible
- Conteneur : en-tête de 12 octets (`ESU `…), puis blocs `type(2, LE) | taille(4, LE) | charge`.
  `0x10` = métadonnées **XML en clair** (carte des slots et touches ; ex. annonces « Bahnhofsdurchsage #1–#5 »), `0x11` = image BMP de la locomotive, puis pour chaque son un bloc `0x20` (nom du type `slot1/horn-high.wav`) suivi d'un bloc de données `0x21`/`0x22`.
- Les données (y compris les XML internes) ont une entropie ≈ 7,99 bits/octet, sans zlib/bz2/lzma : **probablement chiffrées**. Non contourné (la licence ESU interdit la rétro-ingénierie).
- Voie légale : ouvrir le fichier dans **LokProgrammer** (Windows, gratuit sans boîtier) ; export WAV non confirmé ; sinon enregistrer la sortie audio. Sur Mac Apple Silicon : VM Windows 11 ARM avec UTM (non testé avec LokProgrammer).
- Banque ESU : `projects.esu.eu` (projets TGV V4.0, TGV LokSound 5 S0201, BB 7200 S0210).

## 4. Polices
- `universltstd_2.otf` = Univers LT Std 55 Roman (Linotype/Adobe), poids unique.
- `sncf.ttf` = « PoliceSNCF » v1.00 (Type-Designer 2.5), condensée, codée en symbole U+F020–U+F0FF ; glyphes de logo à `! ? $ * + = £ §`. Conversion : `tools/convert_fonts.py`.

## 5. Sources de sons libres (alternative aux projets de décodeurs)
BigSoundBank.com (passages de TGV, trains en gare, bips de portes, jingles SNCF ; libre de droits). ElevenLabs : sons générés par IA. Freesound.org : licences variables, non vérifié.
