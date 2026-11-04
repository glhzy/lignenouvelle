#pragma once
#include <WiFi.h>
#include <WebServer.h>

// Instance partagée — utilisée aussi par wifi_manager pour le portail.
extern WebServer server;

// Enregistre les routes API. Appeler après audio_begin() et wifi_begin().
void webserver_begin();

// À appeler dans loop().
void webserver_handle();
