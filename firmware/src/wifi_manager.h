#pragma once
#include <Arduino.h>

// Initialise le Wi-Fi.
// - Si SSID/mdp mémorisés : tente la connexion (15 s max).
// - Sinon (ou si échec) : démarre le portail captif AP "Lignenouvelle-Config".
// Appeler dans setup() après audio_begin() — le portail utilise le même WebServer.
void wifi_begin();

// À appeler dans loop() : gère le DNS du portail, les reconnexions.
void wifi_handle();

bool   wifi_is_connected();
bool   wifi_is_portal_active();
String wifi_get_ip();
void   wifi_clear_credentials();
