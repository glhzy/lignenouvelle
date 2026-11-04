// Serveur web Lignenouvelle — API REST + fichiers statiques LittleFS.

#include "ln_webserver.h"
#include "wifi_manager.h"
#include "audio.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

WebServer server(80);

// ── Utilitaires ───────────────────────────────────────────────────────────────

static String content_type(const String& path) {
    if (path.endsWith(".html")) return "text/html; charset=utf-8";
    if (path.endsWith(".css"))  return "text/css";
    if (path.endsWith(".js"))   return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".woff")) return "font/woff";
    if (path.endsWith(".ico"))  return "image/x-icon";
    return "text/plain";
}

static void serve_file(const String& uri) {
    // /sounds/* → directement en LittleFS (sons.json pour le JS)
    // tout le reste → /www/<uri>
    String path = uri.startsWith("/sounds/") ? uri : "/www" + uri;
    if (path == "/www/") path = "/www/index.html";

    if (!LittleFS.exists(path)) {
        server.send(404, "text/plain", "Not found: " + path);
        return;
    }
    File f = LittleFS.open(path, "r");
    server.streamFile(f, content_type(path));
    f.close();
}

static void send_json(int code, const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    server.send(code, "application/json", out);
}

static void cors() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
}

// ── Handlers API ──────────────────────────────────────────────────────────────

// GET /api/state
// {"volume":70,"voie1":{"loco":"CC6500","playing":["compresseur"]},"voie2":{...}}
static void handle_state() {
    cors();
    JsonDocument doc;
    doc["volume"] = audio_get_volume();

    char ids[6][32];
    for (int voie = 1; voie <= 2; voie++) {
        String key = voie == 1 ? "voie1" : "voie2";
        doc[key]["loco"] = audio_get_loco(voie);
        JsonArray arr = doc[key]["playing"].to<JsonArray>();
        int n = audio_get_playing(voie, ids, 6);
        for (int i = 0; i < n; i++) arr.add(ids[i]);
    }
    doc["auto"]    = false; // step 7
    doc["loading"] = audio_is_loading();

    send_json(200, doc);
}

// POST /api/play?voie=1&sound=xxx
static void handle_play() {
    cors();
    if (!server.hasArg("sound")) { server.send(400, "text/plain", "?sound manquant"); return; }
    uint8_t voie  = server.hasArg("voie") ? (uint8_t)server.arg("voie").toInt() : 1;
    String  sound = server.arg("sound");
    if (voie < 1 || voie > 2) { server.send(400, "text/plain", "voie 1 ou 2"); return; }
    audio_play(sound.c_str(), voie);
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/stop?voie=1&sound=xxx  (sound optionnel — sans = arrêt tout)
static void handle_stop() {
    cors();
    if (server.hasArg("sound") && server.hasArg("voie")) {
        uint8_t voie = (uint8_t)server.arg("voie").toInt();
        audio_stop(server.arg("sound").c_str(), voie);
    } else {
        audio_stop_all();
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/volume?v=0..100
static void handle_volume() {
    cors();
    if (!server.hasArg("v")) { server.send(400, "text/plain", "?v manquant"); return; }
    int v = server.arg("v").toInt();
    if (v < 0 || v > 100) { server.send(400, "text/plain", "v hors plage"); return; }
    audio_set_volume((uint8_t)v);
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/loco?voie=1&id=BB67400
static void handle_loco() {
    cors();
    if (!server.hasArg("voie") || !server.hasArg("id")) {
        server.send(400, "text/plain", "?voie et ?id requis");
        return;
    }
    uint8_t voie = (uint8_t)server.arg("voie").toInt();
    String  id   = server.arg("id");
    if (voie < 1 || voie > 2) { server.send(400, "text/plain", "voie 1 ou 2"); return; }
    audio_set_loco(voie, id.c_str());
    server.send(200, "application/json", "{\"ok\":true}");
}

// GET /api/locos — sert directement le fichier LittleFS
static void handle_locos() {
    cors();
    if (!LittleFS.exists("/sounds/locos.json")) {
        server.send(503, "application/json", "{}");
        return;
    }
    File f = LittleFS.open("/sounds/locos.json", "r");
    server.streamFile(f, "application/json");
    f.close();
}

// POST /api/auto?on=0|1&min=60&max=300  — stub step 7
static void handle_auto() {
    cors();
    server.send(200, "application/json", "{\"ok\":true,\"note\":\"step 7\"}");
}

// ── Initialisation ────────────────────────────────────────────────────────────

void webserver_begin() {
    server.on("/api/state",  HTTP_GET,  handle_state);
    server.on("/api/locos",  HTTP_GET,  handle_locos);
    server.on("/api/play",   HTTP_POST, handle_play);
    server.on("/api/stop",   HTTP_POST, handle_stop);
    server.on("/api/volume", HTTP_POST, handle_volume);
    server.on("/api/loco",   HTTP_POST, handle_loco);
    server.on("/api/auto",   HTTP_POST, handle_auto);

    // Toutes les autres requêtes : redirection portail ou fichier LittleFS
    server.onNotFound([]() {
        if (wifi_is_portal_active()) {
            server.sendHeader("Location", "http://192.168.4.1/");
            server.send(302, "text/plain", "");
            return;
        }
        serve_file(server.uri());
    });

    // Le portail appelle déjà server.begin() s'il est actif.
    // Sinon, on le démarre ici.
    if (!wifi_is_portal_active()) {
        server.begin();
        Serial.println("[web] Serveur démarré sur le port 80");
    }
}

void webserver_handle() {
    server.handleClient();
}
