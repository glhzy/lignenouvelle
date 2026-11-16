#pragma once
#include <stdint.h>
#include <stdbool.h>

// Moteur audio Lignenouvelle v2 — streaming depuis microSD
//
// Appeler audio_begin() dans setup(), après LittleFS.begin() et sd_begin().
// Toutes les fonctions sont thread-safe (file interne vers le mélangeur).

// Initialise I2S, charge les métadonnées de la loco de démarrage, démarre
// le mélangeur et la tâche de recharge. Les WAV ne sont jamais chargés en PSRAM
// en intégralité : chaque voix lit en continu depuis la SD via un ring-buffer.
void audio_begin(const char* loco_id);

// Joue un son. voie = 1 ou 2.
// Groupes exclusifs par voie (tous sauf "ambiance") : coupe le groupe avant de jouer.
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
bool audio_is_playing(const char* sound_id, uint8_t voie = 1);

// Remplit out_ids avec les IDs des sons actifs sur la voie. Retourne le nombre.
int audio_get_playing(uint8_t voie, char out_ids[][32], int max_count);

// Change la loco d'une voie. Synchrone et instantané (pas de chargement PSRAM).
void audio_set_loco(uint8_t voie, const char* loco_id);
const char* audio_get_loco(uint8_t voie);

// Séquenceur — démarrer / avancer une séquence sons_player.json.
// Bascule : appeler audio_seq_start avec la même séquence l'arrête.
bool audio_seq_start(const char* seq_id, uint8_t voie);
// Avance la séquence identifiée par seq_id sur la voie donnée.
void audio_seq_next(const char* seq_id, uint8_t voie);

struct SeqInfo {
    char id[32];
    int  step;
    int  num_steps;
    bool loop;
};
// Retourne toutes les séquences actives sur la voie (tous groupes confondus).
// Remplit out_infos, retourne le nombre de séquences actives.
int audio_seq_info_all(uint8_t voie, SeqInfo out_infos[], int max_count);

// Automatismes — lecture aléatoire périodique d'un groupe de sons.
// groupe : "annonce" ou "ambiance".  min_s/max_s : intervalle en secondes.
void audio_auto_set(const char* groupe, bool on, uint16_t min_s, uint16_t max_s, uint8_t voie = 1);
bool audio_auto_get(const char* groupe, bool* out_on, uint16_t* out_min, uint16_t* out_max);
