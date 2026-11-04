// Lignenouvelle — boîtier sonore Wi-Fi pour décor ferroviaire
// Étapes 3+4+5+6+SD : PSRAM + LittleFS + SD + moteur audio + Wi-Fi + serveur web.

#include <Arduino.h>
#include <LittleFS.h>
#include "audio.h"
#include "sd_manager.h"
#include "wifi_manager.h"
#include "ln_webserver.h"

void setup() {
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);   // USB CDC : ne pas bloquer si personne ne lit
#endif
    Serial.begin(115200);
    delay(500);
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  Lignenouvelle — démarrage           ║");
    Serial.println("╚══════════════════════════════════════╝");

    // 1. PSRAM
    if (psramFound()) {
        Serial.printf("PSRAM    : OK  (%lu Ko libres)\n", (unsigned long)(ESP.getFreePsram() / 1024));
    } else {
        Serial.println("PSRAM    : INTROUVABLE !");
        Serial.println("  → Variante R8 requise. Vérifier : esptool flash_id");
    }

    // 2. LittleFS
    if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        Serial.println("LittleFS : ERREUR — lancer d'abord :");
        Serial.println("  python3 tools/prepare_fs.py && pio run -t uploadfs");
        return;
    }
    Serial.printf("LittleFS : OK  (%u Ko libres sur %u Ko)\n",
                  (LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024,
                  LittleFS.totalBytes() / 1024);

    // 3. Carte SD (sons WAV — fallback sur LittleFS si absente)
    sd_begin();

    // 4+5. Moteur audio — une seule loco en PSRAM à la fois (8 Mo max)
    audio_begin("CC6500");

    // 6+7. Wi-Fi + serveur web (sur Core 1, dans loop())
    wifi_begin();
    webserver_begin();

    Serial.println("\nLignenouvelle prêt.");
}

void loop() {
    wifi_handle();
    webserver_handle();
}
