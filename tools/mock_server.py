#!/usr/bin/env python3
"""
Lignenouvelle — serveur de maquette (sans ESP32).

Sert l'interface web et simule toutes les API REST.
Usage :
    python3 tools/mock_server.py          # port 8080 par défaut
    python3 tools/mock_server.py 9090     # port personnalisé

Ouvrir : http://localhost:8080/
"""

import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

# ── Racines des fichiers ──────────────────────────────────────────────────────

ROOT      = os.path.join(os.path.dirname(__file__), "..", "firmware", "data")
WWW_DIR   = os.path.join(ROOT, "www")
SND_DIR   = os.path.join(ROOT, "sounds")
WAV_DIR   = os.path.join(os.path.dirname(__file__), "..", "sounds")  # vrais WAV
UI_DIR    = os.path.join(os.path.dirname(__file__), "..", "ui")       # pages démo

# ── État mémoire (imite le firmware) ─────────────────────────────────────────

state = {
    "volume": 70,
    "loco":   "CC6500",
    "voie1_seqs": {},   # dict id → {"id":..., "step":0, "steps":1, "loop":False}
    "auto": {
        "annonce":  {"on": False, "min": 60,  "max": 300},
        "ambiance": {"on": False, "min": 120, "max": 600},
    },
}

# ── Utilitaires ───────────────────────────────────────────────────────────────

MIME = {
    ".html": "text/html; charset=utf-8",
    ".css":  "text/css",
    ".js":   "application/javascript",
    ".json": "application/json",
    ".woff": "font/woff",
    ".ico":  "image/x-icon",
    ".gif":  "image/gif",
    ".wav":  "audio/wav",
}

def mime(path):
    ext = os.path.splitext(path)[1].lower()
    return MIME.get(ext, "text/plain")

def send_json(handler, obj, code=200):
    body = json.dumps(obj, ensure_ascii=False).encode()
    handler.send_response(code)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", len(body))
    handler.send_header("Access-Control-Allow-Origin", "*")
    handler.end_headers()
    handler.wfile.write(body)

def load_sons(loco_id):
    """Charge sons.json pour une loco (chemin firmware)."""
    path = os.path.join(SND_DIR, loco_id, "sons.json")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    return []

def load_seq(loco_id):
    """Charge sons_player.json si présent."""
    path = os.path.join(SND_DIR, loco_id, "sons_player.json")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    return None

# ── Handlers API ─────────────────────────────────────────────────────────────

def api_state(handler, _params):
    resp = {
        "volume": state["volume"],
        "voie1": {
            "loco":    state["loco"],
            "playing": [],
            "seqs":    list(state["voie1_seqs"].values()),
        },
        "voie2": {
            "loco":    state["loco"],
            "playing": [],
            "seqs":    [],
        },
        "auto":    state["auto"],
        "loading": False,
    }
    send_json(handler, resp)

def api_locos(handler, _params):
    path = os.path.join(SND_DIR, "locos.json")
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    send_json(handler, data)

def api_volume(handler, params):
    v = int(params.get("v", ["70"])[0])
    v = max(0, min(100, v))
    state["volume"] = v
    print(f"  volume → {v}")
    send_json(handler, {"ok": True})

def api_loco(handler, params):
    loco_id = params.get("id", [state["loco"]])[0]
    state["loco"]       = loco_id
    state["voie1_seqs"] = {}
    print(f"  loco → {loco_id}")
    send_json(handler, {"ok": True})

def api_seq(handler, params):
    seq_id = params.get("id", [""])[0]
    if not seq_id:
        send_json(handler, {"ok": False, "err": "id manquant"}, 400)
        return
    # Bascule : même séquence active → arrêt
    if seq_id in state["voie1_seqs"]:
        del state["voie1_seqs"][seq_id]
        print(f"  seq STOP {seq_id}")
        send_json(handler, {"ok": True})
        return
    # Chercher la séquence pour connaître num_steps et le type de la première étape
    seqs = load_seq(state["loco"])
    if seqs:
        found = next((s for s in seqs if s["id"] == seq_id), None)
        if found:
            step0 = found["etapes"][0] if found.get("etapes") else {}
            is_loop = step0.get("type") == "loop"
            state["voie1_seqs"][seq_id] = {
                "id":    seq_id,
                "step":  0,
                "steps": len(found.get("etapes", [])),
                "loop":  is_loop,
            }
            print(f"  seq START {seq_id}  step=0  loop={is_loop}")
            send_json(handler, {"ok": True})
            return
    # Fallback (séquence 1-étape depuis sons.json)
    sons = load_sons(state["loco"])
    son  = next((s for s in sons if s["id"] == seq_id), None)
    is_loop = bool(son.get("boucle")) if son else False
    state["voie1_seqs"][seq_id] = {
        "id":    seq_id,
        "step":  0,
        "steps": 1,
        "loop":  is_loop,
    }
    print(f"  seq START {seq_id}  (fallback 1-étape)  loop={is_loop}")
    send_json(handler, {"ok": True})

def api_seq_next(handler, params):
    seq_id = params.get("id", [""])[0]
    if not seq_id or seq_id not in state["voie1_seqs"]:
        send_json(handler, {"ok": True})
        return
    seq = state["voie1_seqs"][seq_id]
    seqs = load_seq(state["loco"])
    if seqs:
        found = next((s for s in seqs if s["id"] == seq_id), None)
        if found and seq["step"] + 1 < len(found["etapes"]):
            seq["step"] += 1
            step_data  = found["etapes"][seq["step"]]
            seq["loop"] = step_data.get("type") == "loop"
            print(f"  seq NEXT {seq_id} → step {seq['step']}  loop={seq['loop']}")
        else:
            del state["voie1_seqs"][seq_id]
            print(f"  seq FIN {seq_id}")
    send_json(handler, {"ok": True})

def api_stop(handler, _params):
    state["voie1_seqs"] = {}
    print("  STOP ALL")
    send_json(handler, {"ok": True})

def api_auto(handler, params):
    groupe = params.get("groupe", ["annonce"])[0]
    on     = params.get("on",     ["0"])[0] == "1"
    mn     = int(params.get("min", ["60"])[0])
    mx     = int(params.get("max", ["300"])[0])
    if groupe in state["auto"]:
        state["auto"][groupe] = {"on": on, "min": mn, "max": mx}
        print(f"  auto {groupe} → on={on}  min={mn}s  max={mx}s")
    send_json(handler, {"ok": True})

API_GET  = {"/api/state": api_state, "/api/locos": api_locos}
API_POST = {
    "/api/volume":   api_volume,
    "/api/loco":     api_loco,
    "/api/seq/next": api_seq_next,
    "/api/seq":      api_seq,
    "/api/stop":     api_stop,
    "/api/auto":     api_auto,
}

# ── Serveur HTTP ──────────────────────────────────────────────────────────────

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # silencieux sauf les print() ci-dessus

    def do_GET(self):
        parsed = urlparse(self.path)
        params = parse_qs(parsed.query)
        path   = parsed.path

        if path in API_GET:
            API_GET[path](self, params)
            return

        # Fichiers statiques
        if path.startswith("/sounds/"):
            rel = path[len("/sounds/"):]
            # WAV → dossier sounds/ à la racine du projet ; JSON → firmware/data/sounds/
            fs = os.path.join(WAV_DIR if rel.lower().endswith(".wav") else SND_DIR, rel)
        elif path.startswith("/ui/"):
            fs = os.path.join(UI_DIR, path[len("/ui/"):])
        else:
            fs = os.path.join(WWW_DIR, path.lstrip("/") or "index.html")

        if os.path.isdir(fs):
            fs = os.path.join(fs, "index.html")
        if not os.path.exists(fs):
            self.send_response(404)
            self.end_headers()
            return

        with open(fs, "rb") as f:
            body = f.read()
        self.send_response(200)
        self.send_header("Content-Type", mime(fs))
        self.send_header("Content-Length", len(body))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        parsed  = urlparse(self.path)
        params  = parse_qs(parsed.query)
        path    = parsed.path
        handler = None
        # Cherche le handler (séquence d'abord car /api/seq/next ⊂ /api/seq)
        for route in sorted(API_POST.keys(), key=len, reverse=True):
            if path == route:
                handler = API_POST[route]
                break
        if handler:
            handler(self, params)
        else:
            send_json(self, {"ok": True})

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.end_headers()

# ── Lancement ─────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"Lignenouvelle mock server — http://localhost:{port}/")
    print("Ctrl+C pour arrêter.\n")
    HTTPServer(("", port), Handler).serve_forever()
