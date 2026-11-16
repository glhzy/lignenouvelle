// Moteur audio Lignenouvelle v2 — streaming depuis microSD
// Mélangeur 6 voix PCM 22050 Hz mono → I2S stéréo
// Chaque voix active lit depuis la SD via un ring-buffer PSRAM (8 Ko ≈ 185 ms)

#include "audio.h"
#include "sd_manager.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// ── Constantes ─────────────────────────────────────────────────────────────────

#define PIN_BCLK        5
#define PIN_WS          6
#define PIN_DOUT        4
#define SAMPLE_RATE     22050
#define NUM_VOICES      6
#define MAX_SOUNDS      48  // TGV a 45 sons au total (disponible + non-disponible)
#define BLOCK_SIZE      512
#define RING_SAMPLES    4096u               // = 8 Ko, doit être une puissance de 2
#define RING_MASK       (RING_SAMPLES - 1u) // = 4095
#define RING_REFILL_LO  2048u               // recharge quand avail < 50 %
#define MAX_SEQS        24                  // séquences par loco
#define MAX_STEPS       12                  // étapes par séquence
#define NUM_GROUPS      5                   // groupes de sons connus

static const char* const GROUP_NAMES[NUM_GROUPS] = {
    "traction", "roulement", "bord", "signaux", "annonce"
};
static int group_to_idx(const char* g) {
    for (int i = 0; i < NUM_GROUPS; i++)
        if (strcmp(GROUP_NAMES[i], g) == 0) return i;
    return 0; // fallback traction
}
#define FADE_IN_SAMP    (5   * SAMPLE_RATE / 1000)   // 110
#define FADE_OUT_SAMP   (15  * SAMPLE_RATE / 1000)   // 330
#define STOP_ALL_SAMP   (100 * SAMPLE_RATE / 1000)   // 2205
#define VOL_CEILING     0.80f

// ── Structures ─────────────────────────────────────────────────────────────────

struct SoundDef {
    char     key[64];         // "CC6500/klaxon_haut"
    char     id[32];
    char     loco_id[16];
    char     groupe[16];
    char     wav_path[80];    // "/sounds/CC6500/klaxon_haut.wav"
    uint32_t data_offset;     // offset du chunk "data" dans le fichier
    uint32_t data_size;       // taille du chunk "data" en octets
    float    base_level;
    bool     loop;
};

typedef enum { VOICE_IDLE = 0, VOICE_PLAYING } VoiceState;

struct Voice {
    const SoundDef*    def;
    float              vol;
    float              vol_target;
    float              fade_step;
    volatile VoiceState state;
    uint8_t            voie;

    // Ring-buffer SPSC (producteur = stream_refill_task, consommateur = mixer)
    int16_t*           ring_buf;       // RING_SAMPLES int16_t, alloué en PSRAM
    volatile uint32_t  ring_rd;        // index consommateur
    volatile uint32_t  ring_wr;        // index producteur
    File               sd_file;
    uint32_t           file_pos;       // octets lus dans le chunk data
    volatile bool      stream_eof;     // fin de données (non-loop)
};

// ── Séquenceur ─────────────────────────────────────────────────────────────────

struct SeqStep { char son[32]; bool loop; };

struct SeqDef {
    char    id[32];
    char    groupe[16];
    SeqStep steps[MAX_STEPS];
    uint8_t num_steps;
};

typedef enum { SEQ_IDLE = 0, SEQ_ONCE, SEQ_LOOP } SeqState;

struct VoieSeq {
    SeqState state;
    int8_t   seq_idx;  // -1 = aucune
    uint8_t  step_idx;
    char     cur_son[32];
};

// ── Automatismes ───────────────────────────────────────────────────────────────

struct AutoMode {
    bool     enabled;
    uint16_t min_s;    // intervalle minimum en secondes
    uint16_t max_s;    // intervalle maximum en secondes
    uint8_t  voie;
    uint32_t next_ms;  // millis() cible du prochain déclenchement (0 = initialiser)
};

static AutoMode          s_auto[2];   // [0]=annonce  [1]=ambiance
static const char* const AUTO_GROUPE[2] = {"annonce", "ambiance"};

typedef enum { CMD_PLAY, CMD_STOP, CMD_STOP_ALL, CMD_STOP_VOIE, CMD_VOLUME, CMD_LOCO,
               CMD_SEQ_START, CMD_SEQ_NEXT } CmdType;

struct AudioCmd {
    CmdType type;
    char    sound_id[32];
    char    loco_id[16];
    uint8_t voie;
    uint8_t volume;
};

// ── État global ─────────────────────────────────────────────────────────────────

static SoundDef          s_sounds[MAX_SOUNDS];
static int               s_num_sounds = 0;
static Voice             s_voices[NUM_VOICES];
static QueueHandle_t     s_cmd_queue  = NULL;
static i2s_chan_handle_t s_tx         = NULL;
static volatile float    s_global_vol = 0.49f; // 70 % par défaut
static char              s_loco[2][16] = {"CC6500", "CC6500"};
static SeqDef            s_seqs[MAX_SEQS];
static int               s_num_seqs   = 0;
static VoieSeq           s_voie_seq[2][NUM_GROUPS]; // [voie-1][group_idx]

static float   s_mix[BLOCK_SIZE];
static int16_t s_out[BLOCK_SIZE * 2];

// ── I2S ────────────────────────────────────────────────────────────────────────

static bool i2s_init() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    if (i2s_new_channel(&chan_cfg, &s_tx, NULL) != ESP_OK) return false;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_BCLK,
            .ws   = (gpio_num_t)PIN_WS,
            .dout = (gpio_num_t)PIN_DOUT,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    if (i2s_channel_init_std_mode(s_tx, &std_cfg) != ESP_OK) return false;
    return i2s_channel_enable(s_tx) == ESP_OK;
}

// ── Parser en-tête WAV ─────────────────────────────────────────────────────────
// Lit uniquement l'en-tête pour trouver l'offset et la taille du chunk "data".
// Parcourt les chunks (LIST, INFO…) jusqu'à trouver "data".

static bool parse_wav_header(const char* path, uint32_t* out_offset, uint32_t* out_size) {
    File f = sd_available() ? SD.open(path, FILE_READ) : File();
    if (!f) f = LittleFS.open(path, "r");
    if (!f) return false;

    uint8_t riff[12];
    if (f.read(riff, 12) < 12 ||
        memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) {
        f.close(); return false;
    }

    uint32_t wav_sr = 0;
    bool found_data = false;
    uint8_t ch[8];
    while (f.read(ch, 8) == 8) {
        uint32_t csz = ch[4] | (ch[5]<<8) | (ch[6]<<16) | (ch[7]<<24);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16] = {};
            uint32_t rd = csz < 16 ? csz : 16;
            f.read(fmt, rd);
            if (*(uint16_t*)fmt != 1) { f.close(); return false; } // non-PCM
            wav_sr = fmt[4] | (fmt[5]<<8) | (fmt[6]<<16) | (fmt[7]<<24);
            if (csz > 16) f.seek(f.position() + (csz - 16));
        } else if (memcmp(ch, "data", 4) == 0) {
            *out_offset = f.position();
            *out_size   = csz ? csz : (uint32_t)(f.size() - f.position());
            found_data  = true;
            break;
        } else {
            f.seek(f.position() + csz + (csz & 1));
        }
    }
    f.close();
    if (!found_data || wav_sr != SAMPLE_RATE) return false;
    return true;
}

// ── Chargement métadonnées loco ───────────────────────────────────────────────
// Parse sons.json et l'en-tête de chaque WAV (pas de lecture PCM).
// ~1 ms par fichier × 20 fichiers = ~20 ms total.

static void load_loco_sounds(const char* loco_id) {
    char sons_path[64] = {};
    {
        File f = LittleFS.open("/sounds/locos.json", "r");
        if (!f) { Serial.println("[audio] locos.json introuvable"); return; }
        JsonDocument doc;
        deserializeJson(doc, f);
        f.close();
        for (JsonObject loco : doc.as<JsonArray>()) {
            if (strcmp(loco["id"] | "", loco_id) == 0) {
                const char* rel = loco["sons_firmware"] | loco["sons"] | "";
                snprintf(sons_path, sizeof(sons_path), "/sounds/%s", rel);
                break;
            }
        }
    }
    if (!sons_path[0]) {
        Serial.printf("[audio] loco introuvable dans locos.json : %s\n", loco_id);
        return;
    }

    File f = LittleFS.open(sons_path, "r");
    if (!f) { Serial.printf("[audio] %s introuvable\n", sons_path); return; }
    JsonDocument doc;
    deserializeJson(doc, f);
    f.close();

    for (JsonObject son : doc.as<JsonArray>()) {
        // Charger tous les sons, y compris disponible=false : ces sons sont utilisés
        // dans les séquences sons_player.json. Le filtre "disponible" est une logique
        // d'affichage (UI/JS), pas une logique firmware.
        int slot = -1;
        for (int i = 0; i < MAX_SOUNDS; i++)
            if (s_sounds[i].key[0] == '\0') { slot = i; break; }
        if (slot < 0) { Serial.println("[audio] MAX_SOUNDS atteint"); break; }

        SoundDef& def = s_sounds[slot];
        strlcpy(def.id,      son["id"]     | "", sizeof(def.id));
        strlcpy(def.loco_id, loco_id,            sizeof(def.loco_id));
        strlcpy(def.groupe,  son["groupe"] | "", sizeof(def.groupe));
        // Copier id dans une variable locale pour éviter l'alias src/dst dans snprintf
        char id_copy[32];
        strlcpy(id_copy, def.id, sizeof(id_copy));
        snprintf(def.key,      sizeof(def.key),      "%s/%s", loco_id, id_copy);
        snprintf(def.wav_path, sizeof(def.wav_path), "/sounds/%s/%s.wav", loco_id, id_copy);
        def.base_level = son["niveau"] | 0.8f;
        def.loop       = son["boucle"] | false;

        if (parse_wav_header(def.wav_path, &def.data_offset, &def.data_size)) {
            Serial.printf("  %-32s  %5lu Ko\n",
                def.key, (unsigned long)(def.data_size / 1024));
            s_num_sounds++;
        } else {
            Serial.printf("  [ERREUR] %s\n", def.wav_path);
            memset(&def, 0, sizeof(def));
        }
    }
}

// ── Chargement séquences loco ─────────────────────────────────────────────────
// Tente /sounds/<id>/sons_player.json ; sinon génère une séquence 1-étape par son.

static void load_loco_seqs(const char* loco_id) {
    s_num_seqs = 0;
    memset(s_seqs, 0, sizeof(s_seqs));

    char path[64];
    snprintf(path, sizeof(path), "/sounds/%s/sons_player.json", loco_id);

    if (LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");
        if (!f) goto fallback;
        {
            JsonDocument doc;
            deserializeJson(doc, f);
            f.close();
            for (JsonObject obj : doc.as<JsonArray>()) {
                if (s_num_seqs >= MAX_SEQS) break;
                SeqDef& seq = s_seqs[s_num_seqs];
                strlcpy(seq.id,     obj["id"]     | "", sizeof(seq.id));
                strlcpy(seq.groupe, obj["groupe"] | "", sizeof(seq.groupe));
                seq.num_steps = 0;
                for (JsonObject e : obj["etapes"].as<JsonArray>()) {
                    if (seq.num_steps >= MAX_STEPS) break;
                    SeqStep& st = seq.steps[seq.num_steps];
                    strlcpy(st.son, e["son"] | "", sizeof(st.son));
                    // sons_player.json inclut l'extension .wav ; SoundDef.id n'en a pas
                    size_t slen = strlen(st.son);
                    if (slen > 4 && strcmp(st.son + slen - 4, ".wav") == 0)
                        st.son[slen - 4] = '\0';
                    st.loop = strcmp(e["type"] | "", "loop") == 0;
                    if (st.son[0]) seq.num_steps++;
                }
                if (seq.id[0] && seq.num_steps > 0) s_num_seqs++;
            }
        }
        Serial.printf("[seq] %s : %d séquences (sons_player.json)\n", loco_id, s_num_seqs);
        return;
    }

fallback:
    for (int i = 0; i < MAX_SOUNDS && s_num_seqs < MAX_SEQS; i++) {
        SoundDef& def = s_sounds[i];
        if (!def.key[0]) continue;
        SeqDef& seq = s_seqs[s_num_seqs];
        strlcpy(seq.id,          def.id,     sizeof(seq.id));
        strlcpy(seq.groupe,      def.groupe, sizeof(seq.groupe));
        strlcpy(seq.steps[0].son, def.id,    sizeof(seq.steps[0].son));
        seq.steps[0].loop = def.loop;
        seq.num_steps = 1;
        s_num_seqs++;
    }
    Serial.printf("[seq] %s : %d séquences (générées)\n", loco_id, s_num_seqs);
}

// ── Ring-buffer helpers ────────────────────────────────────────────────────────
// SPSC lock-free : ring_rd (consommateur) et ring_wr (producteur) sont des
// indices uint32_t volatiles qui s'incrémentent librement. L'index réel est
// obtenu par masquage (& RING_MASK). Les deux côtés ne se marchent jamais
// dessus tant que ring_avail >= 0 et ring_space >= 0.

static inline uint32_t ring_avail(const Voice& v) {
    return (v.ring_wr - v.ring_rd) & RING_MASK;
}
static inline uint32_t ring_space(const Voice& v) {
    return RING_SAMPLES - ring_avail(v) - 1u; // -1 : évite l'ambiguïté plein/vide
}

// Remplit le ring-buffer depuis le fichier SD jusqu'à ce qu'il soit plein.
// Pour les sons en boucle, repart au début du chunk data en fin de fichier.
// Appelé depuis voice_open (mixer) ou stream_refill_task.
static void refill_voice(Voice& v) {
    if (!v.sd_file || v.stream_eof) return;
    while (ring_space(v) > 0) {
        uint32_t wr   = v.ring_wr & RING_MASK;
        uint32_t lin  = RING_SAMPLES - wr;       // espace linéaire avant wrap
        uint32_t spc  = ring_space(v);
        uint32_t n    = (lin < spc) ? lin : spc; // ne pas croiser le bord du buffer
        if (n > 256) n = 256;                    // max 512 octets par itération SD

        uint32_t remaining = v.def->data_size - v.file_pos;
        size_t to_read = (size_t)n * 2;
        if (to_read > remaining) to_read = remaining;

        if (to_read == 0) {
            if (v.def->loop) {
                v.sd_file.seek(v.def->data_offset);
                v.file_pos = 0;
                continue;
            } else {
                v.stream_eof = true;
                break;
            }
        }

        size_t got = v.sd_file.read((uint8_t*)(v.ring_buf + wr), to_read);
        if (got == 0) {
            // La carte SD peut rendre 0 octets temporairement (gestion interne).
            // Ne déclarer EOF que si on a réellement consommé toutes les données.
            if (v.file_pos >= v.def->data_size) v.stream_eof = true;
            break; // réessayer au prochain cycle de recharge (20 ms)
        }

        v.file_pos += got;
        uint32_t samps = got / 2;
        // Mettre à jour ring_wr APRÈS l'écriture des données
        // (le consommateur voit les nouvelles données avant la mise à jour de ring_wr)
        v.ring_wr = v.ring_wr + samps; // débordement uint32_t intentionnel
    }
}

// ── Gestion des voix ──────────────────────────────────────────────────────────

static SoundDef* find_sound(const char* sound_id, uint8_t voie) {
    const char* loco = s_loco[voie - 1];
    for (int i = 0; i < MAX_SOUNDS; i++) {
        SoundDef& s = s_sounds[i];
        if (s.key[0] == '\0') continue;
        if (strcmp(s.id, sound_id) == 0 && strcmp(s.loco_id, loco) == 0)
            return &s;
    }
    return nullptr;
}

static void voice_fade_out(Voice& v, int fade_samp) {
    if (v.state == VOICE_IDLE) return;
    v.vol_target = 0.0f;
    v.fade_step  = (v.vol > 0.001f) ? (-v.vol / fade_samp) : (-0.001f);
}


static void do_play(const char* sound_id, uint8_t voie); // forward

// ── Helpers séquenceur (appelés depuis mixer_task) ────────────────────────────

static int find_seq(const char* seq_id) {
    for (int i = 0; i < s_num_seqs; i++)
        if (strcmp(s_seqs[i].id, seq_id) == 0) return i;
    return -1;
}

// Joue l'étape courante (step_idx) ; passe les sons indisponibles.
// depth protège contre une récursion infinie si toutes les étapes sont indisponibles.
static void seq_play_step(uint8_t voie, int gi, int depth = 0);

static void seq_advance_internal(uint8_t voie, int gi) {
    VoieSeq& vs = s_voie_seq[voie - 1][gi];
    if (vs.seq_idx < 0) return;

    // Arrêter la voix courante avant d'avancer (nécessaire pour les étapes loop encore actives).
    // No-op si la voix est déjà VOICE_IDLE (auto-avance après fin naturelle d'une étape once).
    if (vs.cur_son[0]) {
        for (int i = 0; i < NUM_VOICES; i++) {
            Voice& v = s_voices[i];
            if (v.state != VOICE_IDLE && v.voie == voie && v.def &&
                strcmp(v.def->id, vs.cur_son) == 0)
                voice_fade_out(v, FADE_OUT_SAMP);
        }
    }
    // Vider cur_son AVANT tout stop pour ne pas annuler la séquence dans do_stop
    vs.cur_son[0] = '\0';
    vs.step_idx++;
    seq_play_step(voie, gi);
}

static void seq_play_step(uint8_t voie, int gi, int depth) {
    if (depth > MAX_STEPS) return;
    VoieSeq& vs = s_voie_seq[voie - 1][gi];
    if (vs.seq_idx < 0 || vs.step_idx >= s_seqs[vs.seq_idx].num_steps) {
        vs.state   = SEQ_IDLE;
        vs.seq_idx = -1;
        return;
    }
    SeqDef&  seq  = s_seqs[vs.seq_idx];
    SeqStep& step = seq.steps[vs.step_idx];

    if (!find_sound(step.son, voie)) {
        // Son indisponible → passer à l'étape suivante
        vs.cur_son[0] = '\0';
        vs.step_idx++;
        seq_play_step(voie, gi, depth + 1);
        return;
    }
    do_play(step.son, voie);
    strlcpy(vs.cur_son, step.son, sizeof(vs.cur_son));
    vs.state = step.loop ? SEQ_LOOP : SEQ_ONCE;
}

static Voice* alloc_voice() {
    for (int i = 0; i < NUM_VOICES; i++)
        if (s_voices[i].state == VOICE_IDLE) return &s_voices[i];
    // Voler la première voix jouante non bouclée
    for (int i = 0; i < NUM_VOICES; i++) {
        Voice& v = s_voices[i];
        if (v.state == VOICE_PLAYING && v.def && !v.def->loop) {
            if (v.sd_file) v.sd_file.close();
            v.ring_rd = 0;
            v.ring_wr = 0;
            v.state   = VOICE_IDLE;
            return &v;
        }
    }
    return nullptr;
}

// Ouvre le fichier WAV et pré-remplit tout le ring-buffer.
// Appelé depuis do_play (tâche mélangeur, haute priorité).
static void voice_open(Voice& v, SoundDef* def) {
    v.def        = def;
    v.ring_rd    = 0;
    v.ring_wr    = 0;
    v.file_pos   = 0;
    v.stream_eof = false;

    if (!sd_available()) {
        Serial.printf("[audio] SD indisponible pour %s\n", def->wav_path);
        return;
    }
    v.sd_file = SD.open(def->wav_path, FILE_READ);
    if (!v.sd_file) {
        Serial.printf("[audio] Impossible d'ouvrir %s\n", def->wav_path);
        return;
    }
    v.sd_file.seek(def->data_offset);
    refill_voice(v); // pré-remplissage complet (~8 Ko, ~5 ms)
}

static void do_play(const char* sound_id, uint8_t voie) {
    SoundDef* def = find_sound(sound_id, voie);
    if (!def) {
        Serial.printf("[audio] Son inconnu : %s (voie %u / %s)\n",
                      sound_id, voie, s_loco[voie - 1]);
        return;
    }

    Voice* v = alloc_voice();
    if (!v) { Serial.println("[audio] Toutes les voix occupées"); return; }

    voice_open(*v, def);
    if (!v->sd_file) return;

    v->vol        = 0.0f;
    v->vol_target = def->base_level;
    v->fade_step  = def->base_level / FADE_IN_SAMP;
    v->state      = VOICE_PLAYING;
    v->voie       = voie;
}

static void do_stop(const char* sound_id, uint8_t voie) {
    for (int i = 0; i < NUM_VOICES; i++) {
        Voice& v = s_voices[i];
        if (v.state == VOICE_IDLE || v.voie != voie || !v.def) continue;
        if (strcmp(v.def->id, sound_id) == 0)
            voice_fade_out(v, FADE_OUT_SAMP);
    }
    // Annule le slot de séquence dont cur_son correspond (appel externe)
    for (int gi = 0; gi < NUM_GROUPS; gi++) {
        VoieSeq& vs = s_voie_seq[voie - 1][gi];
        if (vs.cur_son[0] && strcmp(vs.cur_son, sound_id) == 0) {
            vs.state      = SEQ_IDLE;
            vs.seq_idx    = -1;
            vs.cur_son[0] = '\0';
        }
    }
}

static void do_stop_all() {
    for (int i = 0; i < NUM_VOICES; i++)
        if (s_voices[i].state != VOICE_IDLE)
            voice_fade_out(s_voices[i], STOP_ALL_SAMP);
    for (int v = 0; v < 2; v++)
        for (int gi = 0; gi < NUM_GROUPS; gi++)
            s_voie_seq[v][gi] = {SEQ_IDLE, -1, 0, ""};
}

static void do_stop_voie(uint8_t voie) {
    for (int i = 0; i < NUM_VOICES; i++)
        if (s_voices[i].state != VOICE_IDLE && s_voices[i].voie == voie)
            voice_fade_out(s_voices[i], FADE_OUT_SAMP);
    for (int gi = 0; gi < NUM_GROUPS; gi++)
        s_voie_seq[voie - 1][gi] = {SEQ_IDLE, -1, 0, ""};
}

// ── Automatismes — timer aléatoire par groupe ─────────────────────────────────
// Appelé depuis stream_refill_task (Core 1).  Lit s_sounds sans verrou :
// la fenêtre de conflit avec CMD_LOCO est négligeable (pire cas : un son manqué).

static void auto_check() {
    if (!s_cmd_queue) return;
    uint32_t now = (uint32_t)millis();
    for (int i = 0; i < 2; i++) {
        AutoMode& am = s_auto[i];
        if (!am.enabled) continue;
        if (am.next_ms == 0) {
            // Premier tick : choisir un délai initial aléatoire dans [min, max]
            uint32_t range_ms = (uint32_t)(am.max_s - am.min_s) * 1000;
            am.next_ms = now + (uint32_t)am.min_s * 1000 +
                         (range_ms ? esp_random() % range_ms : 0);
            continue;
        }
        if (now < am.next_ms) continue;

        // Collecter les sons disponibles dans ce groupe (locos courantes)
        int indices[MAX_SOUNDS];
        int count = 0;
        for (int j = 0; j < s_num_sounds; j++) {
            if (s_sounds[j].id[0] &&
                strcmp(s_sounds[j].groupe, AUTO_GROUPE[i]) == 0)
                indices[count++] = j;
        }
        if (count > 0) {
            int pick = (int)(esp_random() % (uint32_t)count);
            AudioCmd cmd = {};
            cmd.type = CMD_PLAY;
            cmd.voie = am.voie;
            strlcpy(cmd.sound_id, s_sounds[indices[pick]].id, sizeof(cmd.sound_id));
            xQueueSend(s_cmd_queue, &cmd, 0); // non-bloquant
        }
        // Planifier le prochain déclenchement
        uint32_t range_ms = (uint32_t)(am.max_s - am.min_s) * 1000;
        uint32_t delay_ms = (uint32_t)am.min_s * 1000 +
                            (range_ms ? esp_random() % range_ms : 0);
        am.next_ms = now + delay_ms;
    }
}

// ── Tâche de recharge (Core 1, priorité 3) ────────────────────────────────────
// Surveille toutes les voix actives et recharge leur ring-buffer quand < 50 %.
// Ferme les fichiers SD des voix passées en VOICE_IDLE.

static void stream_refill_task(void*) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        auto_check();
        for (int i = 0; i < NUM_VOICES; i++) {
            Voice& v = s_voices[i];
            if (v.state == VOICE_IDLE) {
                if (v.sd_file) v.sd_file.close();
                continue;
            }
            if (!v.sd_file || v.stream_eof) continue;
            if (ring_avail(v) < RING_REFILL_LO) refill_voice(v);
        }
    }
}

// ── Tâche mélangeur (Core 1, priorité 5) ─────────────────────────────────────

static void mixer_task(void*) {
    AudioCmd cmd;
    for (;;) {
        // Traiter les commandes en attente
        while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case CMD_PLAY:     do_play(cmd.sound_id, cmd.voie); break;
                case CMD_STOP:     do_stop(cmd.sound_id, cmd.voie); break;
                case CMD_STOP_ALL: do_stop_all(); break;
                case CMD_STOP_VOIE:do_stop_voie(cmd.voie); break;
                case CMD_VOLUME:
                    s_global_vol = (float)cmd.volume * cmd.volume / 10000.0f;
                    break;
                case CMD_LOCO: {
                    if (strcmp(s_loco[0], cmd.loco_id) == 0) break; // déjà chargée
                    // Couper toutes les voix, séquences, et fermer les fichiers SD
                    for (int i = 0; i < NUM_VOICES; i++) {
                        s_voices[i].state = VOICE_IDLE;
                        if (s_voices[i].sd_file) s_voices[i].sd_file.close();
                        s_voices[i].ring_rd = 0;
                        s_voices[i].ring_wr = 0;
                    }
                    for (int v = 0; v < 2; v++)
                        for (int gi = 0; gi < NUM_GROUPS; gi++)
                            s_voie_seq[v][gi] = {SEQ_IDLE, -1, 0, ""};
                    // Effacer les métadonnées
                    memset(s_sounds, 0, sizeof(s_sounds));
                    s_num_sounds = 0;
                    memset(s_seqs, 0, sizeof(s_seqs));
                    s_num_seqs = 0;
                    // Charger les métadonnées de la nouvelle loco (~20 ms — dans la fenêtre DMA)
                    load_loco_sounds(cmd.loco_id);
                    load_loco_seqs(cmd.loco_id);
                    strlcpy(s_loco[0], cmd.loco_id, sizeof(s_loco[0]));
                    strlcpy(s_loco[1], cmd.loco_id, sizeof(s_loco[1]));
                    Serial.printf("[audio] Loco : %s (%d sons, %d séq)\n",
                                  cmd.loco_id, s_num_sounds, s_num_seqs);
                    break;
                }
                case CMD_SEQ_START: {
                    int idx = find_seq(cmd.sound_id);
                    if (idx < 0) {
                        Serial.printf("[seq] Inconnue : %s\n", cmd.sound_id);
                        break;
                    }
                    int gi = group_to_idx(s_seqs[idx].groupe);
                    VoieSeq& vs = s_voie_seq[cmd.voie - 1][gi];
                    // Bascule : même séquence déjà active → arrêt du groupe
                    if (vs.seq_idx == idx && vs.state != SEQ_IDLE) {
                        for (int i = 0; i < NUM_VOICES; i++) {
                            Voice& v = s_voices[i];
                            if (v.state != VOICE_IDLE && v.voie == cmd.voie && v.def &&
                                strcmp(v.def->groupe, s_seqs[idx].groupe) == 0)
                                voice_fade_out(v, FADE_OUT_SAMP);
                        }
                        vs = {SEQ_IDLE, -1, 0, ""};
                        break;
                    }
                    // Réinitialiser ce slot de groupe uniquement
                    vs.state      = SEQ_IDLE;
                    vs.cur_son[0] = '\0';
                    vs.seq_idx    = idx;
                    vs.step_idx   = 0;
                    seq_play_step(cmd.voie, gi);
                    break;
                }
                case CMD_SEQ_NEXT: {
                    // sound_id contient l'id de la séquence à avancer
                    int idx = find_seq(cmd.sound_id);
                    if (idx < 0) break;
                    int gi = group_to_idx(s_seqs[idx].groupe);
                    VoieSeq& vs = s_voie_seq[cmd.voie - 1][gi];
                    if (vs.state == SEQ_LOOP && vs.seq_idx == idx)
                        seq_advance_internal(cmd.voie, gi);
                    break;
                }
            }
        }

        // ── Mixage ──────────────────────────────────────────────────────────────
        memset(s_mix, 0, sizeof(s_mix));

        for (int vi = 0; vi < NUM_VOICES; vi++) {
            Voice& v = s_voices[vi];
            if (v.state == VOICE_IDLE) continue;

            for (int i = 0; i < BLOCK_SIZE; i++) {
                // Fondu
                if (v.fade_step != 0.0f) {
                    v.vol += v.fade_step;
                    if (v.fade_step > 0.0f && v.vol >= v.vol_target) {
                        v.vol = v.vol_target; v.fade_step = 0.0f;
                    } else if (v.fade_step < 0.0f && v.vol <= 0.0f) {
                        v.vol = 0.0f; v.state = VOICE_IDLE; break;
                    }
                }

                uint32_t avail = ring_avail(v);
                if (avail > 0) {
                    int16_t samp = v.ring_buf[v.ring_rd & RING_MASK];
                    v.ring_rd = v.ring_rd + 1u;
                    s_mix[i] += (float)samp * v.vol;
                } else if (v.stream_eof) {
                    // Fin naturelle du son : libérer la voix
                    v.state = VOICE_IDLE;
                    break;
                }
                // Underrun passager : silence (refill_task comblera au prochain cycle)
            }
        }

        // ── Auto-avance séquenceur ────────────────────────────────────────
        for (int vi = 0; vi < 2; vi++) {
            for (int gi = 0; gi < NUM_GROUPS; gi++) {
                VoieSeq& vs = s_voie_seq[vi][gi];
                if (vs.state != SEQ_ONCE || vs.cur_son[0] == '\0') continue;
                bool still = false;
                for (int i = 0; i < NUM_VOICES && !still; i++) {
                    if (s_voices[i].state == VOICE_PLAYING &&
                        s_voices[i].voie == (uint8_t)(vi + 1) &&
                        s_voices[i].def &&
                        strcmp(s_voices[i].def->id, vs.cur_son) == 0)
                        still = true;
                }
                if (!still) seq_advance_internal(vi + 1, gi);
            }
        }

        // ── Volume global + plafond + conversion stéréo ──────────────────────
        const float gain = s_global_vol * VOL_CEILING;
        for (int i = 0; i < BLOCK_SIZE; i++) {
            int32_t s = (int32_t)(s_mix[i] * gain);
            if      (s >  32767) s =  32767;
            else if (s < -32768) s = -32768;
            s_out[i * 2]     = (int16_t)s;
            s_out[i * 2 + 1] = (int16_t)s;
        }

        // ── Écriture I2S (bloquant — rythme naturel du DMA) ──────────────────
        size_t written;
        i2s_channel_write(s_tx, s_out, sizeof(s_out), &written, pdMS_TO_TICKS(200));
    }
}

// ── API publique ──────────────────────────────────────────────────────────────

void audio_begin(const char* loco_id) {
    Serial.println("[audio] Init v2 (streaming SD + séquenceur)...");
    memset(s_voices,    0, sizeof(s_voices));
    memset(s_sounds,    0, sizeof(s_sounds));
    memset(s_seqs,      0, sizeof(s_seqs));
    for (int v = 0; v < 2; v++)
        for (int gi = 0; gi < NUM_GROUPS; gi++)
            s_voie_seq[v][gi] = {SEQ_IDLE, -1, 0, ""};
    // Automatismes : désactivés par défaut, intervals typiques
    s_auto[0] = {false, 60,  300, 1, 0};  // annonce
    s_auto[1] = {false, 120, 600, 1, 0};  // ambiance

    // Allouer les ring-buffers en PSRAM (6 × 8 Ko = 48 Ko)
    for (int i = 0; i < NUM_VOICES; i++) {
        s_voices[i].ring_buf = (int16_t*)ps_malloc(RING_SAMPLES * sizeof(int16_t));
        if (!s_voices[i].ring_buf)
            Serial.printf("[audio] ERREUR ps_malloc ring[%d]\n", i);
    }

    strlcpy(s_loco[0], loco_id, sizeof(s_loco[0]));
    strlcpy(s_loco[1], loco_id, sizeof(s_loco[1]));

    if (!i2s_init()) { Serial.println("[audio] ERREUR I2S"); return; }
    Serial.println("[audio] I2S OK");

    Serial.println("[audio] Métadonnées :");
    load_loco_sounds(loco_id);
    load_loco_seqs(loco_id);
    Serial.printf("[audio] %d sons, %d séquences\n", s_num_sounds, s_num_seqs);

    s_cmd_queue = xQueueCreate(16, sizeof(AudioCmd));
    xTaskCreatePinnedToCore(mixer_task,        "audio_mix", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(stream_refill_task,"audio_ref", 4096, NULL, 3, NULL, 1);
    Serial.println("[audio] Mélangeur + recharge démarrés (Core 1)");
}

bool audio_play(const char* sound_id, uint8_t voie) {
    if (!s_cmd_queue) return false;
    AudioCmd cmd = {};
    cmd.type = CMD_PLAY;
    cmd.voie = voie;
    strlcpy(cmd.sound_id, sound_id, sizeof(cmd.sound_id));
    return xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

void audio_stop(const char* sound_id, uint8_t voie) {
    if (!s_cmd_queue) return;
    AudioCmd cmd = {};
    cmd.type = CMD_STOP;
    cmd.voie = voie;
    strlcpy(cmd.sound_id, sound_id, sizeof(cmd.sound_id));
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(10));
}

void audio_stop_all() {
    if (!s_cmd_queue) return;
    AudioCmd cmd = {};
    cmd.type = CMD_STOP_ALL;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(10));
}

void audio_set_volume(uint8_t pct) {
    if (!s_cmd_queue) {
        s_global_vol = (float)pct * pct / 10000.0f;
        return;
    }
    AudioCmd cmd = {};
    cmd.type   = CMD_VOLUME;
    cmd.volume = pct;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(10));
}

uint8_t audio_get_volume() {
    return (uint8_t)(sqrtf(s_global_vol) * 100.0f + 0.5f);
}

bool audio_is_playing(const char* sound_id, uint8_t voie) {
    for (int i = 0; i < NUM_VOICES; i++) {
        const Voice& v = s_voices[i];
        if (v.state == VOICE_IDLE || v.voie != voie || !v.def) continue;
        if (strcmp(v.def->id, sound_id) == 0) return true;
    }
    return false;
}

void audio_set_loco(uint8_t voie, const char* loco_id) {
    if (!s_cmd_queue) return;
    AudioCmd cmd = {};
    cmd.type = CMD_STOP_VOIE;
    cmd.voie = voie;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(10));
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CMD_LOCO;
    cmd.voie = voie;
    strlcpy(cmd.loco_id, loco_id, sizeof(cmd.loco_id));
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(10));
}

const char* audio_get_loco(uint8_t voie) {
    return s_loco[voie - 1];
}

int audio_get_playing(uint8_t voie, char out_ids[][32], int max_count) {
    int n = 0;
    for (int i = 0; i < NUM_VOICES && n < max_count; i++) {
        const Voice& v = s_voices[i];
        if (v.state == VOICE_IDLE || v.voie != voie || !v.def) continue;
        strlcpy(out_ids[n++], v.def->id, 32);
    }
    return n;
}

bool audio_seq_start(const char* seq_id, uint8_t voie) {
    if (!s_cmd_queue) return false;
    AudioCmd cmd = {};
    cmd.type = CMD_SEQ_START;
    cmd.voie = voie;
    strlcpy(cmd.sound_id, seq_id, sizeof(cmd.sound_id));
    return xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

void audio_seq_next(const char* seq_id, uint8_t voie) {
    if (!s_cmd_queue) return;
    AudioCmd cmd = {};
    cmd.type = CMD_SEQ_NEXT;
    cmd.voie = voie;
    strlcpy(cmd.sound_id, seq_id, sizeof(cmd.sound_id));
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(10));
}

int audio_seq_info_all(uint8_t voie, SeqInfo out_infos[], int max_count) {
    int n = 0;
    for (int gi = 0; gi < NUM_GROUPS && n < max_count; gi++) {
        const VoieSeq& vs = s_voie_seq[voie - 1][gi];
        if (vs.state == SEQ_IDLE || vs.seq_idx < 0) continue;
        strlcpy(out_infos[n].id, s_seqs[vs.seq_idx].id, 32);
        out_infos[n].step      = vs.step_idx;
        out_infos[n].num_steps = s_seqs[vs.seq_idx].num_steps;
        out_infos[n].loop      = (vs.state == SEQ_LOOP);
        n++;
    }
    return n;
}

// ── Automatismes ──────────────────────────────────────────────────────────────

void audio_auto_set(const char* groupe, bool on, uint16_t min_s, uint16_t max_s, uint8_t voie) {
    for (int i = 0; i < 2; i++) {
        if (strcmp(AUTO_GROUPE[i], groupe) == 0) {
            s_auto[i].enabled = on;
            s_auto[i].min_s   = min_s;
            s_auto[i].max_s   = max_s;
            s_auto[i].voie    = voie;
            s_auto[i].next_ms = 0; // réinitialise le timer au prochain tick
            return;
        }
    }
}

bool audio_auto_get(const char* groupe, bool* out_on, uint16_t* out_min, uint16_t* out_max) {
    for (int i = 0; i < 2; i++) {
        if (strcmp(AUTO_GROUPE[i], groupe) == 0) {
            *out_on  = s_auto[i].enabled;
            *out_min = s_auto[i].min_s;
            *out_max = s_auto[i].max_s;
            return true;
        }
    }
    return false;
}
