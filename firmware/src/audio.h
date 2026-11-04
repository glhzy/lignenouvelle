#pragma once
#include <stdint.h>
#include <stdbool.h>

// Moteur audio Lignenouvelle — API publique
//
// Appeler audio_begin() dans setup(), après LittleFS.begin().
// Toutes les autres fonctions sont thread-safe (file interne).

// Initialise I2S, charge la loco de démarrage en PSRAM, démarre le mélangeur.
// Une seule loco en PSRAM à la fois (max ~8 Mo).
void audio_begin(const char* loco_id);

// Joue un son. voie = 1 ou 2.
// Groupes exclusifs par voie : un seul "klaxon" et une seule "annonce" actifs à la fois.
// Retourne false si le son est inconnu ou si toutes les voix sont occupées.
bool audio_play(const char* sound_id, uint8_t voie = 1);

// Arrête un son avec fondu court (~15 ms).
void audio_stop(const char* sound_id, uint8_t voie = 1);

// Arrête tout avec fondu long (100 ms) — bouton Arrêt général.
void audio_stop_all();

// Volume général 0–100.
void audio_set_volume(uint8_t pct);
uint8_t audio_get_volume();

// Vrai si le son est en cours de lecture sur la voie indiquée.
// Lecture non verrouillée : valeur indicative pour l'UI.
bool audio_is_playing(const char* sound_id, uint8_t voie = 1);

// Remplit out_ids avec les IDs des sons actifs sur la voie. Retourne le nombre.
// Lecture non verrouillée — pour /api/state uniquement.
int audio_get_playing(uint8_t voie, char out_ids[][32], int max_count);

// Change la loco d'une voie (arrête les sons de la voie en cours).
// Le chargement est asynchrone : audio_is_loading() passe à true pendant la durée.
void audio_set_loco(uint8_t voie, const char* loco_id);
const char* audio_get_loco(uint8_t voie);

// Vrai si un chargement de loco est en cours.
bool audio_is_loading();
