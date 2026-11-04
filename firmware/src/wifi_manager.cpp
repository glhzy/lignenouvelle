// Gestionnaire Wi-Fi Lignenouvelle
// Mode normal : connexion à la box, mDNS lignenouvelle.local.
// Mode portail : AP "Lignenouvelle-Config", page de configuration au 192.168.4.1.

#include "wifi_manager.h"
#include "ln_webserver.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

static DNSServer  s_dns;
static bool       s_connected = false;
static bool       s_portal    = false;
static uint32_t   s_connect_start = 0;

// ── Page du portail (embarquée) ────────────────────────────────────────────────

static const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="fr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lignenouvelle — Wi-Fi</title>
<style>
body{margin:0;font-family:Arial,sans-serif;background:#1B2A49;display:flex;
  align-items:center;justify-content:center;min-height:100vh}
.b{background:#F4F1E8;color:#14203A;padding:28px 24px;max-width:340px;width:100%}
h1{margin:0 0 6px;font-size:22px}
.bar{height:4px;background:#E8761A;margin-bottom:20px}
label{display:block;font-size:13px;margin-bottom:4px;font-weight:bold}
input{width:100%;padding:10px;border:2px solid #D8D5CA;font-size:16px;
  margin-bottom:16px;box-sizing:border-box;background:#fff}
button{width:100%;padding:14px;background:#1B2A49;color:#fff;border:0;font-size:16px;cursor:pointer}
.err{color:#C8102E;font-size:13px;margin-top:8px}
</style></head>
<body><div class="b">
<h1>La ligne nouvelle</h1>
<div class="bar"></div>
<form method="POST" action="/wifi-save">
<label for="s">Nom du réseau (SSID)</label>
<input id="s" name="ssid" type="text" autocomplete="off" autocorrect="off"
  autocapitalize="none" spellcheck="false">
<label for="p">Mot de passe</label>
<input id="p" name="pass" type="password" autocomplete="current-password">
<button type="submit">Enregistrer et redémarrer</button>
</form>
%ERROR%
</div></body></html>)HTML";

static const char PORTAL_OK[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="fr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lignenouvelle</title>
<style>
body{margin:0;font-family:Arial,sans-serif;background:#1B2A49;display:flex;
  align-items:center;justify-content:center;min-height:100vh;color:#fff;text-align:center}
p{font-size:18px}
</style></head>
<body><p>Enregistré.<br>Redémarrage en cours…</p></body></html>)HTML";

// ── Portail ───────────────────────────────────────────────────────────────────

static void start_portal(bool failed = false) {
    Serial.println("[wifi] Démarrage portail captif AP \"Lignenouvelle-Config\"");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Lignenouvelle-Config");
    delay(200);
    IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));

    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(53, "*", ip);

    // Route : page de configuration
    server.on("/", HTTP_GET, [failed]() {
        String page = FPSTR(PORTAL_HTML);
        page.replace("%ERROR%", failed
            ? "<p class=\"err\">Connexion échouée — vérifier SSID et mot de passe.</p>"
            : "");
        server.send(200, "text/html; charset=utf-8", page);
    });
    server.on("/wifi-config", HTTP_GET, [failed]() {
        String page = FPSTR(PORTAL_HTML);
        page.replace("%ERROR%", failed
            ? "<p class=\"err\">Connexion échouée — vérifier SSID et mot de passe.</p>"
            : "");
        server.send(200, "text/html; charset=utf-8", page);
    });

    // Route : soumission du formulaire
    server.on("/wifi-save", HTTP_POST, []() {
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        ssid.trim();
        if (ssid.length() == 0) {
            server.sendHeader("Location", "/");
            server.send(302);
            return;
        }
        Preferences prefs;
        prefs.begin("wifi", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.end();
        server.send(200, "text/html; charset=utf-8", FPSTR(PORTAL_OK));
        delay(1500);
        ESP.restart();
    });

    server.begin();
    s_portal    = true;
    s_connected = false;
    Serial.println("[wifi] Portail : http://192.168.4.1/");
}

// ── API publique ──────────────────────────────────────────────────────────────

void wifi_begin() {
    Preferences prefs;
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid.length() == 0) {
        start_portal(false);
        return;
    }

    Serial.printf("[wifi] Connexion à \"%s\"…\n", ssid.c_str());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.begin(ssid.c_str(), pass.c_str());
    s_connect_start = millis();
}

void wifi_handle() {
    if (s_portal) {
        s_dns.processNextRequest();
        return;
    }

    if (!s_connected) {
        if (WiFi.status() == WL_CONNECTED) {
            s_connected = true;
            Serial.printf("[wifi] Connecté !  IP : %s\n",
                          WiFi.localIP().toString().c_str());
            Serial.println("[wifi] Conseil : réservez cette IP dans votre box.");
            if (MDNS.begin("lignenouvelle")) {
                MDNS.addService("http", "tcp", 80);
                Serial.println("[wifi] mDNS : http://lignenouvelle.local/");
            }
        } else if (millis() - s_connect_start > 15000) {
            Serial.println("[wifi] Délai dépassé — passage en portail.");
            start_portal(true);
        }
    }
}

bool   wifi_is_connected()     { return s_connected; }
bool   wifi_is_portal_active() { return s_portal; }
String wifi_get_ip()           { return s_connected ? WiFi.localIP().toString() : ""; }

void wifi_clear_credentials() {
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.end();
    Serial.println("[wifi] Identifiants effacés.");
}
