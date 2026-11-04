// Moteur audio Lignenouvelle
// Mélangeur 6 voix PCM 22050 Hz mono → I2S stéréo, sons en PSRAM.

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

// ── Constantes ────────────────────────────────────────────────────────────────

#define PIN_BCLK        5
#define PIN_WS          6
#define PIN_DOUT        4
#define SAMPLE_RATE     22050
#define NUM_VOICES      6
#define MAX_SOUNDS      32
#define BLOCK_SIZE      512         // ~23 ms par trame
#define FADE_IN_SAMP    (5   * SAMPLE_RATE / 1000)   // 110
#define FADE_OUT_SAMP   (15  * SAMPLE_RATE / 1000)   // 330
#define STOP_ALL_SAMP   (100 * SAMPLE_RATE / 1000)   // 2205
#define VOL_CEILING     0.80f

// ── Structures internes ───────────────────────────────────────────────────────

struct SoundDef {
    char     key[64];       // "CC6500/klaxon_haut"
    char     id[32];
    char     loco_id[16];
    char     groupe[16];
    int16_t* samples;       // alloué en PSRAM
    uint32_t num_samples;
    float    base_level;
    bool     loop;
};

typedef enum { VOICE_IDLE = 0, VOICE_PLAYING } VoiceState;

struct Voice {
    const SoundDef* def;
    uint32_t        pos;
    float           vol;
    float           vol_target;
    float           fade_step;
    VoiceState      state;
    uint8_t         voie;
};

typedef enum {
    CMD_PLAY,
    CMD_STOP,
    CMD_STOP_ALL,
    CMD_STOP_VOIE,
    CMD_VOLUME,
    CMD_LOCO
} CmdType;

struct LoadReq { uint8_t voie; char loco_id[16]; };

struct AudioCmd {
    CmdType type;
    char    sound_id[32];
    char    loco_id[16];
    uint8_t voie;
    uint8_t volume;
};

// ── État global ───────────────────────────────────────────────────────────────

static SoundDef          s_sounds[MAX_SOUNDS];
static int               s_num_sounds = 0;
static Voice             s_voices[NUM_VOICES];
static QueueHandle_t     s_cmd_queue  = NULL;
static QueueHandle_t     s_load_queue = NULL;
static i2s_chan_handle_t s_tx         = NULL;
static volatile float    s_global_vol = 0.49f; // 70% par défaut
static char              s_loco[2][16] = {"CC6500", "CC6500"};
static volatile bool     s_loading    = false;

// Buffers de mixage : globaux, utilisés exclusivement par la tâche mélangeur (Core 1).
static float   s_mix[BLOCK_SIZE];
static int16_t s_out[BLOCK_SIZE * 2]; // stéréo

// ── I2S ───────────────────────────────────────────────────────────────────────

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

// ── Chargement PSRAM ──────────────────────────────────────────────────────────

static bool load_wav_to_psram(SoundDef& def, const char* path) {
    // SD en premier, fallback LittleFS
    File f = sd_available() ? SD.open(path, FILE_READ) : File();
    if (!f) f = LittleFS.open(path, "r");
    if (!f) return false;

    // En-tête RIFF/WAVE (12 octets)
    uint8_t riff[12];
    if (f.read(riff, 12) < 12 || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) {
        f.close(); return false;
    }

    // Scan des chunks jusqu'à trouver "data"
    // Certains fichiers ont des chunks LIST (métadonnées) avant "data"
    uint32_t wav_sr = 0, data_size = 0;
    bool found_data = false;
    uint8_t ch[8];
    while (f.read(ch, 8) == 8) {
        uint32_t csz = ch[4] | (ch[5]<<8) | (ch[6]<<16) | (ch[7]<<24);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16] = {};
            uint32_t rd = csz < 16 ? csz : 16;
            f.read(fmt, rd);
            if (*(uint16_t*)fmt != 1) {          // PCM = 1
                Serial.printf("[audio] %s : non-PCM\n", path);
                f.close(); return false;
            }
            wav_sr = fmt[4] | (fmt[5]<<8) | (fmt[6]<<16) | (fmt[7]<<24);
            if (csz > 16) f.seek(f.position() + (csz - 16));
        } else if (memcmp(ch, "data", 4) == 0) {
            data_size = csz;
            found_data = true;
            break;
        } else {
            // Chunk inconnu (LIST, INFO, …) — sauter
            f.seek(f.position() + csz + (csz & 1));
        }
    }

    if (!found_data) {
        Serial.printf("[audio] %s : chunk data introuvable\n", path);
        f.close(); return false;
    }
    if (wav_sr != SAMPLE_RATE) {
        Serial.printf("[audio] %s : sample rate %lu\n", path, (unsigned long)wav_sr);
        f.close(); return false;
    }
    if (data_size == 0) data_size = (uint32_t)(f.size() - f.position());

    def.samples = (int16_t*)ps_malloc(data_size);
    if (!def.samples) {
        Serial.printf("[audio] ps_malloc(%lu) échoué : %s\n", (unsigned long)data_size, path);
        f.close(); return false;
    }

    // Lecture par tranches de 16 Ko + yield pour laisser le webserver répondre
    const size_t CHUNK = 16384;
    size_t got = 0;
    while (got < data_size) {
        size_t n = f.read((uint8_t*)def.samples + got,
                          (data_size - got) < CHUNK ? (data_size - got) : CHUNK);
        if (n == 0) break;
        got += n;
        taskYIELD();
    }
    f.close();
    if (got != data_size) { free(def.samples); def.samples = nullptr; return false; }
    def.num_samples = data_size / 2;
    return true;
}

static void load_loco_sounds(const char* loco_id) {
    // Trouver sons.json via locos.json
    char sons_path[64] = {};
    {
        File f = LittleFS.open("/sounds/locos.json", "r");
        if (!f) { Serial.println("[audio] locos.json introuvable"); return; }
        JsonDocument doc;
        deserializeJson(doc, f);
        f.close();
        for (JsonObject loco : doc.as<JsonArray>()) {
            if (strcmp(loco["id"] | "", loco_id) == 0) {
                // Préférer sons_firmware (ex. TGV) puis sons
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
        if (!son["disponible"].as<bool>()) continue;

        // Chercher un slot libre
        int slot = -1;
        for (int i = 0; i < MAX_SOUNDS; i++)
            if (s_sounds[i].key[0] == '\0') { slot = i; break; }
        if (slot < 0) {
            Serial.println("[audio] MAX_SOUNDS atteint !");
            break;
        }

        SoundDef& def = s_sounds[slot];
        strlcpy(def.id,      son["id"]     | "", sizeof(def.id));
        strlcpy(def.loco_id, loco_id,            sizeof(def.loco_id));
        strlcpy(def.groupe,  son["groupe"] | "", sizeof(def.groupe));
        snprintf(def.key, sizeof(def.key), "%s/%s", loco_id, def.id);
        def.base_level = son["niveau"] | 0.8f;
        def.loop       = son["boucle"] | false;
        def.samples    = nullptr;

        char wav[80];
        snprintf(wav, sizeof(wav), "/sounds/%s/%s.wav", loco_id, def.id);

        if (load_wav_to_psram(def, wav)) {
            Serial.printf("  %-32s %5lu Ko\n", def.key, (unsigned long)(def.num_samples * 2 / 1024));
            s_num_sounds++;
        } else {
            Serial.printf("  [ERREUR] %s\n", wav);
            memset(&def, 0, sizeof(def)); // Libérer le slot en cas d'échec
        }
    }
}

static void unload_loco_sounds(const char* loco_id) {
    int freed = 0;
    for (int i = 0; i < MAX_SOUNDS; i++) {
        SoundDef& def = s_sounds[i];
        if (def.key[0] == '\0' || strcmp(def.loco_id, loco_id) != 0) continue;
        if (def.samples) { free(def.samples); }
        memset(&def, 0, sizeof(def));
        freed++;
        s_num_sounds--;
    }
    Serial.printf("[audio] Loco %s déchargée (%d sons)\n", loco_id, freed);
}

// ── Gestion des voix (appelé depuis la tâche mélangeur uniquement) ───────────

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

static void stop_exclusive_group(const char* groupe, uint8_t voie) {
    for (int i = 0; i < NUM_VOICES; i++) {
        Voice& v = s_voices[i];
        if (v.state == VOICE_IDLE) continue;
        if (v.voie != voie) continue;
        if (strcmp(v.def->groupe, groupe) == 0)
            voice_fade_out(v, FADE_OUT_SAMP);
    }
}

static Voice* alloc_voice() {
    for (int i = 0; i < NUM_VOICES; i++)
        if (s_voices[i].state == VOICE_IDLE) return &s_voices[i];
    // Voler la première voix jouante non bouclée
    for (int i = 0; i < NUM_VOICES; i++)
        if (s_voices[i].state == VOICE_PLAYING && !s_voices[i].def->loop) {
            s_voices[i].state = VOICE_IDLE;
            return &s_voices[i];
        }
    return nullptr;
}

static void do_play(const char* sound_id, uint8_t voie) {
    SoundDef* def = find_sound(sound_id, voie);
    if (!def) {
        Serial.printf("[audio] Son inconnu : %s (voie %u / %s)\n",
                      sound_id, voie, s_loco[voie - 1]);
        return;
    }

    // Groupes exclusifs : klaxon et annonce (pas ambiance)
    if (strcmp(def->groupe, "ambiance") != 0 && def->groupe[0] != '\0')
        stop_exclusive_group(def->groupe, voie);

    Voice* v = alloc_voice();
    if (!v) { Serial.println("[audio] Toutes les voix occupées"); return; }

    v->def        = def;
    v->pos        = 0;
    v->vol        = 0.0f;
    v->vol_target = def->base_level;
    v->fade_step  = def->base_level / FADE_IN_SAMP;
    v->state      = VOICE_PLAYING;
    v->voie       = voie;
}

static void do_stop(const char* sound_id, uint8_t voie) {
    for (int i = 0; i < NUM_VOICES; i++) {
        Voice& v = s_voices[i];
        if (v.state == VOICE_IDLE) continue;
        if (v.voie != voie) continue;
        if (strcmp(v.def->id, sound_id) == 0)
            voice_fade_out(v, FADE_OUT_SAMP);
    }
}

static void do_stop_all() {
    for (int i = 0; i < NUM_VOICES; i++)
        if (s_voices[i].state != VOICE_IDLE)
            voice_fade_out(s_voices[i], STOP_ALL_SAMP);
}

static void do_stop_voie(uint8_t voie) {
    for (int i = 0; i < NUM_VOICES; i++)
        if (s_voices[i].state != VOICE_IDLE && s_voices[i].voie == voie)
            voice_fade_out(s_voices[i], FADE_OUT_SAMP);
}

// ── Tâche de chargement asynchrone (Core 1, priorité basse) ──────────────────

static void loco_load_task(void*) {
    LoadReq req;
    for (;;) {
        if (xQueueReceive(s_load_queue, &req, portMAX_DELAY) != pdTRUE) continue;
        s_loading = true;
        Serial.printf("[audio] Chargement async %s...\n", req.loco_id);
        load_loco_sounds(req.loco_id);
        // Activer les deux voies sur la même loco
        strlcpy(s_loco[0], req.loco_id, sizeof(s_loco[0]));
        strlcpy(s_loco[1], req.loco_id, sizeof(s_loco[0]));
        Serial.printf("[audio] → %s OK (%d sons)\n", req.loco_id, s_num_sounds);
        s_loading = false;
    }
}

// ── Tâche mélangeur (Core 1, priorité haute) ──────────────────────────────────

static void mixer_task(void*) {
    AudioCmd cmd;

    for (;;) {
        // Traiter les commandes en attente avant chaque trame
        while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case CMD_PLAY:
                    do_play(cmd.sound_id, cmd.voie);
                    break;
                case CMD_STOP:
                    do_stop(cmd.sound_id, cmd.voie);
                    break;
                case CMD_STOP_ALL:
                    do_stop_all();
                    break;
                case CMD_STOP_VOIE:
                    do_stop_voie(cmd.voie);
                    break;
                case CMD_VOLUME:
                    // Courbe quadratique : vol = (pct/100)^2
                    s_global_vol = (float)cmd.volume * cmd.volume / 10000.0f;
                    break;
                case CMD_LOCO: {
                    const char* new_loco = cmd.loco_id;
                    if (strcmp(s_loco[0], new_loco) == 0) break; // déjà chargée

                    // Couper toutes les voix
                    for (int i = 0; i < NUM_VOICES; i++)
                        s_voices[i].state = VOICE_IDLE;

                    // Effacer les deux voies pendant le chargement
                    s_loco[0][0] = '\0';
                    s_loco[1][0] = '\0';

                    // Libérer TOUTE la PSRAM (une seule loco à la fois)
                    for (int i = 0; i < MAX_SOUNDS; i++) {
                        if (!s_sounds[i].key[0]) continue;
                        if (s_sounds[i].samples) free(s_sounds[i].samples);
                        memset(&s_sounds[i], 0, sizeof(s_sounds[i]));
                    }
                    s_num_sounds = 0;

                    // Envoyer à la tâche de chargement asynchrone
                    LoadReq req;
                    req.voie = 1;
                    strlcpy(req.loco_id, new_loco, sizeof(req.loco_id));
                    if (xQueueSend(s_load_queue, &req, 0) != pdTRUE)
                        Serial.println("[audio] ERREUR : file de chargement pleine");
                    break;
                }
            }
        }

        // ── Mixage ────────────────────────────────────────────────────────────
        memset(s_mix, 0, sizeof(s_mix));

        for (int vi = 0; vi < NUM_VOICES; vi++) {
            Voice& v = s_voices[vi];
            if (v.state == VOICE_IDLE) continue;

            for (int i = 0; i < BLOCK_SIZE; i++) {
                // Fondu
                if (v.fade_step != 0.0f) {
                    v.vol += v.fade_step;
                    if (v.fade_step > 0.0f) {
                        if (v.vol >= v.vol_target) {
                            v.vol       = v.vol_target;
                            v.fade_step = 0.0f;
                        }
                    } else {
                        if (v.vol <= 0.0f) {
                            v.vol   = 0.0f;
                            v.state = VOICE_IDLE;
                            break;
                        }
                    }
                }

                s_mix[i] += v.def->samples[v.pos] * v.vol;

                if (++v.pos >= v.def->num_samples) {
                    v.pos = 0;
                    if (!v.def->loop) {
                        v.state = VOICE_IDLE;
                        break;
                    }
                }
            }
        }

        // ── Volume global + plafond + conversion stéréo ───────────────────────
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
    Serial.println("[audio] Initialisation...");
    memset(s_voices, 0, sizeof(s_voices));
    strlcpy(s_loco[0], loco_id, sizeof(s_loco[0]));
    strlcpy(s_loco[1], loco_id, sizeof(s_loco[1]));

    if (!i2s_init()) {
        Serial.println("[audio] ERREUR : I2S non initialisé");
        return;
    }
    Serial.println("[audio] I2S OK");

    Serial.println("[audio] Chargement PSRAM :");
    load_loco_sounds(loco_id);

    uint32_t total = 0;
    for (int i = 0; i < MAX_SOUNDS; i++)
        if (s_sounds[i].key[0]) total += s_sounds[i].num_samples * 2;
    Serial.printf("[audio] %d sons — %lu Ko PSRAM utilisés\n",
                  s_num_sounds, (unsigned long)(total / 1024));

    s_cmd_queue  = xQueueCreate(16, sizeof(AudioCmd));
    s_load_queue = xQueueCreate(2, sizeof(LoadReq));
    // Core 1 (APP_CPU) : Core 0 est réservé aux tâches Wi-Fi/BT de l'IDF.
    xTaskCreatePinnedToCore(mixer_task,    "audio_mix",  4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(loco_load_task,"loco_load",  8192, NULL, 1, NULL, 1);
    Serial.println("[audio] Mélangeur + chargeur async démarrés (Core 1)");
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
    // Inverse courbe quadratique : pct = sqrt(vol) × 100
    return (uint8_t)(sqrtf(s_global_vol) * 100.0f + 0.5f);
}

bool audio_is_playing(const char* sound_id, uint8_t voie) {
    // Lecture non verrouillée depuis Core 1 — valeur indicative pour l'UI
    for (int i = 0; i < NUM_VOICES; i++) {
        const Voice& v = s_voices[i];
        if (v.state == VOICE_IDLE || v.voie != voie) continue;
        if (v.def && strcmp(v.def->id, sound_id) == 0) return true;
    }
    return false;
}

void audio_set_loco(uint8_t voie, const char* loco_id) {
    if (!s_cmd_queue) return;
    AudioCmd cmd = {};
    // Arrêter les sons de la voie avant de changer
    cmd.type = CMD_STOP_VOIE;
    cmd.voie = voie;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(10));
    // Changer la loco (traité après le stop)
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

bool audio_is_loading() { return s_loading; }
