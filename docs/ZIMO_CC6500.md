# Projet ZIMO SNCF CC 6500 (16 bits) — documentation du projet

Projet ZIMO n° A215, décodeurs ZIMO MS (logiciel ≥ 4.250), adresse 3, modèle H0 LS Models, 16 bits / 22 kHz, 128 Mbit de mémoire son. Reset : CV 8 = 8.

## Touches
| Touche | Fonction | Slot du son |
|---|---|---|
| F0 | Fanaux blancs | – |
| F1 | Fanaux rouges | – |
| F2 | Klaxons courts (script 3) | groupe de klaxons |
| F3 | Klaxons longs (script 4) | groupe de klaxons |
| F4 | Sifflet chef de gare | 57 |
| F5 | Attelage / dételage | 29 |
| F6 | Demi-vitesse / manœuvre + fanaux | – |
| F7 | Grincement des voies (script 1, 2 sons) | 30 / 42 (probable) |
| F8 | Son on/off (standard ZIMO) | – |
| F9 | Atténuation du son | – |
| F10 | Purge d'air | 24 |
| F11 | Porte de cabine | 31 |
| F12 | Éclairage cabine | – |
| F13 | Porte compartiment machine | 50 |
| F14 | Compresseur | 32 |
| F15 | Compresseur auxiliaire | 52 |
| F16 | Essai freins | 56 |
| F17 | Fanaux rouges stationnement + frein à main (script 6) | 70 / 75 (probable) |
| F18 | Test fanaux | – |
| F19 | Code phares | – |
| F20 | Freinage d'urgence / détresse (script 5) | – |
| F21 | Pantographe | 62 |
| F22 | Frein rhéostatique | 69 |
| F23 | KVB | 58 |
| F24 | Tachymètre | 33 |
| F25 | Annonce | 23 |
| F26 | Sablage | 34 |
| F27 / F28 | Volume + / − | – |

Schéma des CV de sons (déduit des CV listées dans la doc et d'autres fiches ZIMO, à confirmer) : pour Fn avec n ≤ 19, numéro de son = CV `510+3n`, volume = `511+3n`, mode de boucle = `512+3n` (ex. F4 → CV 522, F16 → CV 558) ; pour n ≥ 20, numéro de son = CV `673+3(n−20)` (ex. F21 → CV 676, F26 → CV 691).
Sons automatiques : moteur électrique (slot 35), changement de sens (26), grincement de freins (19), desserrage (18), générateurs aléatoires Z1 = compresseur (slot 32, ~10 s) et Z2 = purge d'air (slot 41, ~2 min), graduateur `Schaltw_*` (CV 350–363).
Scripts : 1 grincement des courbes (2 sons), 2 extinction éclairage cabine, 3 klaxons courts, 4 klaxons longs, 5 freinage d'urgence, 6 frein de stationnement, 7 VACMA.

## Liste des fichiers son (slot : nom)
18 Bremse-lösen · 19 Bremsen · 23 Annonce TEE Mistral_jingle ancien_8-bit · 24 Luftzisch_kurz · 25 Bremsprobe_indir.-Br._an-lösen_kurz_02 · 26 Richtungswechsel _vor · 27 Klaxon_hoch-kurz_lmt · 29 An-Abkuppeln · 30 Grincement des voies · 31 Tür_auf-zu · 32 Compresseur_kurz · 33 Tachcro · 34 Sanden_kurz · 35 E-Motor_fade-in · 41 Zisch_02 · 42 Grincement_lent · 49 Silence · 50 Maschinenraumtür_auf-zu · 51 Klaxon_hoch_lmt · 52 Petit_cheval · 53 Klaxon_kurz-tief_lmt · 54 Klaxon_tief_lmt · 55 Klaxon_tief-hoch-kurz_lmt · 56 Bremsprobe_dir.-Br._an-lösen_kurz · 57 Sifflet_SNCF_2_echo · 58 KVB_start · 62 Panto-up-down_02 · 67 Annonce Corail Marseille_jingle84 · 68 annonce_Corail_jingle77 · 69 E-Bremse_kurz · 70 Handbremse_an_k · 71 Klaxon_hoch_sehr-kurz_lmt · 72 Klaxon_hoch-tief-hoch_lmt · 73 Klaxon_hoch-lang_lmt · 74 Klaxon_hoch-tief-hoch_lang_lmt · 75 Handbremse_lösen_k · 76 VACMA_Pedal-nicht-betätigt · 77 VACMA_zu-lange-gehalten.
Les numéros de slot lus dans le fichier .zpp par `tools/zpp16_extract.py` concordent avec cette liste.

Note : cette doc ne mentionne aucune touche pour les annonces Corail (slots 67, 68) ; seul le slot 23 (TEE Mistral) est sur F25.
