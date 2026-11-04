// Carte microSD via SPI2 (FSPI)
// CS=GPIO10 (jaune), MOSI=GPIO11 (bleu), CLK=GPIO12 (blanc), MISO=GPIO13 (vert)

#include "sd_manager.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#define PIN_SD_CS   10
#define PIN_SD_MOSI 11
#define PIN_SD_CLK  12
#define PIN_SD_MISO 13

static SPIClass s_spi(FSPI);
static bool     s_ready = false;

bool sd_begin() {
    s_spi.begin(PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, s_spi)) {
        Serial.println("SD       : absente ou non FAT32 — sons chargés depuis LittleFS");
        return false;
    }
    uint64_t total = SD.totalBytes() / 1024;
    uint64_t free_ = (SD.totalBytes() - SD.usedBytes()) / 1024;
    Serial.printf("SD       : OK  (%llu Ko libres sur %llu Ko — type %s)\n",
                  free_, total,
                  SD.cardType() == CARD_SDHC ? "SDHC" : "SD");
    s_ready = true;
    return true;
}

bool sd_available() { return s_ready; }
