#!/usr/bin/env python3
"""
Test automatique des sons longs — Lignenouvelle
Lance chaque séquence via l'API REST, surveille le serial pour détecter EOF ou underrun.
Usage : python3 tools/test_audio.py [--port /dev/cu.usbmodem5C831039061] [--ip 192.168.1.150]
"""

import serial, time, threading, subprocess, argparse, json, os, sys

SERIAL_PORT = "/dev/cu.usbmodem5C831039061"
BAUD        = 115200
ESP_IP      = "192.168.1.150"

# Sons à tester : (seq_id, durée_attendue_s, voie)
# On garde les sons à 1 étape non-loop (les séquences multi-étapes boucles seraient longues à automatiser)
TESTS_ANNONCES = [
    ("annonce_tgv_1",  9.0),
    ("annonce_tgv_2", 17.6),
    ("annonce_tgv_3", 17.6),
    ("annonce_tgv_4", 22.1),
    ("annonce_tgv_5", 46.2),
]

TESTS_SONS_COURTS = [
    ("airhorn_quai",        2.2),
    ("airhorn_tunnel",      1.6),
    ("sifflet_conducteur",  5.5),
    ("porte_ouverture",     3.2),
    ("porte_fermeture",     5.5),
    ("frein_leve",          1.4),
]

# ─────────────────────────────────────────────────────────────────────────────

def api(path, method="POST", ip=ESP_IP):
    r = subprocess.run(
        ["curl", "-s", "-X", method, f"http://{ip}{path}"],
        capture_output=True, text=True, timeout=5
    )
    return r.stdout.strip()

def stop_all(ip):
    api("/api/stop", ip=ip)
    time.sleep(0.5)

def run_test(seq_id, expected_s, voie=1, timeout_factor=2.5, ser=None, ip=ESP_IP):
    """
    Lance seq_id et attend l'EOF dans le serial.
    Retourne (ok, elapsed, notes).
    """
    timeout = max(expected_s * timeout_factor, expected_s + 10)

    # Vider le buffer serial
    if ser:
        ser.reset_input_buffer()

    logs = []
    found_eof    = threading.Event()
    found_underrun = threading.Event()

    def collect(deadline):
        while time.time() < deadline:
            if not ser: break
            try:
                raw = ser.readline()
                if not raw: continue
                line = raw.decode("utf-8", errors="replace").rstrip()
                logs.append(line)
                if f"eof {seq_id}" in line:
                    found_eof.set()
                    return
                if f"underrun {seq_id}" in line:
                    found_underrun.set()
            except Exception:
                break

    t0 = time.time()
    deadline = t0 + timeout

    coll = threading.Thread(target=collect, args=(deadline,), daemon=True)
    coll.start()

    resp = api(f"/api/seq?voie={voie}&id={seq_id}", ip=ip)
    if '"ok":true' not in resp:
        found_eof.clear()
        coll.join(timeout=1)
        return False, 0.0, f"API refusé: {resp}"

    # Attendre EOF ou timeout
    found_eof.wait(timeout=timeout + 2)
    elapsed = time.time() - t0
    coll.join(timeout=2)

    underrun = found_underrun.is_set() or any("underrun" in l for l in logs)
    eof_ok   = found_eof.is_set()

    notes = []
    if underrun:
        u = [l for l in logs if "underrun" in l]
        notes.append(f"UNDERRUN: {u[0] if u else '?'}")
    if not eof_ok:
        # Chercher d'autres indices
        relevant = [l for l in logs if seq_id in l or "steal" in l or "vfs_fat" in l]
        if relevant:
            notes.append(f"logs: {relevant[:3]}")
        notes.append(f"timeout après {elapsed:.1f}s (attendu {expected_s:.1f}s)")

    return eof_ok and not underrun, elapsed, " | ".join(notes) if notes else "ok"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=SERIAL_PORT)
    parser.add_argument("--ip",   default=ESP_IP)
    parser.add_argument("--only", default=None, help="tester seulement ce seq_id")
    args = parser.parse_args()

    # Ouvrir le serial
    try:
        ser = serial.Serial(args.port, BAUD, timeout=1)
        time.sleep(1)
        ser.reset_input_buffer()
        print(f"Serial ouvert : {args.port}\n")
    except Exception as e:
        print(f"[WARN] Pas de serial ({e}) — test sans logs (EOF non détectable)")
        ser = None

    # Mettre le volume à 5 % pour les tests
    api("/api/volume?v=5", ip=args.ip)
    print("Volume réglé à 5 %\n")

    # Vérifier que le boîtier répond
    state = api("/api/state", method="GET", ip=args.ip)
    try:
        js = json.loads(state)
        loco = js["voie1"]["loco"]
        print(f"Boîtier OK — loco voie 1 : {loco}\n")
        if loco != "TGV":
            print(f"[WARN] La loco n'est pas TGV (c'est {loco}). Les séquences TGV risquent d'échouer.")
            print("Basculer sur TGV ? (o/n) ", end="", flush=True)
            if input().strip().lower() in ("o", "oui", "y", "yes"):
                api("/api/loco?voie=1&id=TGV", ip=args.ip)
                time.sleep(1)
    except Exception:
        print(f"[ERREUR] Boîtier ne répond pas : {state}")
        sys.exit(1)

    all_tests = TESTS_ANNONCES + TESTS_SONS_COURTS
    if args.only:
        all_tests = [(s, d) for s, d in all_tests if s == args.only]
        if not all_tests:
            # test à la volée avec durée inconnue
            all_tests = [(args.only, 30.0)]

    results = []
    pause_between = 2.0

    print(f"{'Son':35s}  {'Attendu':>8s}  {'Réel':>8s}  Résultat")
    print("-" * 75)

    for seq_id, expected_s in all_tests:
        stop_all(args.ip)
        time.sleep(pause_between)

        print(f"  {seq_id:33s}  {expected_s:7.1f}s  ", end="", flush=True)
        ok, elapsed, notes = run_test(seq_id, expected_s, ser=ser, ip=args.ip)
        status = "✓ OK" if ok else "✗ FAIL"
        print(f"{elapsed:7.1f}s  {status}  {notes}")
        results.append((seq_id, expected_s, elapsed, ok, notes))

    stop_all(args.ip)
    if ser:
        ser.close()

    # Résumé
    passed = sum(1 for *_, ok, _ in results if ok)
    total  = len(results)
    print()
    print(f"═══ Résultats : {passed}/{total} OK ═══")
    if passed < total:
        print("\nÉchecs :")
        for seq_id, exp, elapsed, ok, notes in results:
            if not ok:
                print(f"  {seq_id:35s}  {notes}")


def run_load_test(ser, ip):
    """
    Test de charge : simule l'usage réel depuis mobile.
    - Polling /api/state toutes les 2 s (comme l'interface)
    - Plusieurs sons simultanés sur voie 1 (groupes différents)
    - Annonce longue pendant que des sons courts se déclenchent
    """
    print("\n═══ TEST DE CHARGE — sons simultanés + polling Wi-Fi ═══\n")

    # Scénarios : liste de (nom_scenario, [(seq_id, délai_départ_s, voie), ...], durée_totale_s)
    scenarios = [
        (
            "Annonce longue + ventilateur + compresseur",
            [
                ("annonce_tgv_5",   0.0, 1),   # 46 s
                ("ventilateur",     1.0, 1),   # 9 s (3 étapes dont loop)
                ("compresseur",     2.0, 1),   # ~8 s (3 étapes)
            ],
            55.0,
        ),
        (
            "2 annonces (voies différentes) simultanées",
            [
                ("annonce_tgv_4",   0.0, 1),   # 22 s
                ("annonce_tgv_3",   0.5, 2),   # 17.6 s
            ],
            30.0,
        ),
        (
            "Annonce + sifflet + porte pendant polling intensif",
            [
                ("annonce_tgv_2",   0.0, 1),   # 17.6 s
                ("sifflet_conducteur", 3.0, 1),
                ("porte_ouverture", 8.0, 1),
                ("airhorn_quai",   12.0, 1),
            ],
            25.0,
        ),
        (
            "Rafale de sons courts pendant annonce longue",
            [
                ("annonce_tgv_5",   0.0, 1),   # 46 s
                ("airhorn_quai",    4.0, 1),
                ("frein_leve",      8.0, 1),
                ("porte_fermeture", 14.0, 1),
                ("sifflet_conducteur", 22.0, 1),
                ("airhorn_tunnel",  30.0, 1),
            ],
            55.0,
        ),
    ]

    all_ok = True

    for scenario_name, actions, total_s in scenarios:
        stop_all(ip)
        time.sleep(2)

        print(f"Scénario : {scenario_name}")
        print(f"  Sons : {', '.join(a[0] for a in actions)}")

        # Collecter les logs pendant toute la durée
        logs = []
        stop_ev = threading.Event()
        eofs_found = set()
        underruns   = []

        def collect():
            while not stop_ev.is_set():
                if not ser: break
                try:
                    raw = ser.readline()
                    if not raw: continue
                    line = raw.decode("utf-8", errors="replace").rstrip()
                    logs.append((time.time(), line))
                    if "[dbg] eof " in line:
                        sid = line.split("[dbg] eof ")[1].split(" ")[0]
                        eofs_found.add(sid)
                    if "[dbg] underrun" in line:
                        underruns.append(line)
                except Exception:
                    break

        if ser:
            ser.reset_input_buffer()
        coll = threading.Thread(target=collect, daemon=True)
        coll.start()

        # Thread polling (simule l'interface mobile)
        poll_results = []
        def poller():
            while not stop_ev.is_set():
                t0 = time.time()
                r = api("/api/state", method="GET", ip=ip)
                poll_results.append(time.time() - t0)
                time.sleep(2.0)

        poll_th = threading.Thread(target=poller, daemon=True)
        poll_th.start()

        t_start = time.time()

        # Lancer les sons aux délais prévus
        for seq_id, delay, voie in actions:
            wait = delay - (time.time() - t_start)
            if wait > 0: time.sleep(wait)
            resp = api(f"/api/seq?voie={voie}&id={seq_id}", ip=ip)
            ok_str = "✓" if '"ok":true' in resp else "✗"
            print(f"  +{time.time()-t_start:5.1f}s  {ok_str} POST {seq_id} voie={voie}")

        # Attendre la fin du scénario
        remaining = total_s - (time.time() - t_start)
        if remaining > 0: time.sleep(remaining)

        stop_ev.set()
        coll.join(timeout=2)
        poll_th.join(timeout=2)

        # Analyser
        expected_eofs = {a[0] for a in actions if not any(
            # séquences multi-étapes avec loop ne génèrent pas forcément d'EOF simple
            a[0] in ("ventilateur", "compresseur", "drive_cycle", "crissement",
                     "noise_low", "moteur_ralenti", "frein") for a in [a]
        )}
        # Simplification : on vérifie juste les annonces (toujours 1 étape once)
        annonces = {a[0] for a in actions if "annonce" in a[0]}
        missing  = annonces - eofs_found
        ok = len(missing) == 0 and len(underruns) == 0

        avg_poll = sum(poll_results) / len(poll_results) if poll_results else 0
        print(f"  EOF reçus   : {sorted(eofs_found)}")
        if missing:
            print(f"  EOF manqués : {sorted(missing)}")
        if underruns:
            for u in underruns:
                print(f"  UNDERRUN    : {u}")
        print(f"  Polling     : {len(poll_results)} requêtes, moy {avg_poll*1000:.0f} ms")
        print(f"  → {'✓ OK' if ok else '✗ FAIL'}\n")

        if not ok:
            all_ok = False
        stop_all(ip)

    print(f"═══ Test de charge : {'TOUT OK' if all_ok else 'DES ÉCHECS'} ═══")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=SERIAL_PORT)
    parser.add_argument("--ip",   default=ESP_IP)
    parser.add_argument("--only", default=None, help="tester seulement ce seq_id")
    parser.add_argument("--load", action="store_true", help="test de charge (sons simultanés + polling)")
    args = parser.parse_args()

    # Ouvrir le serial
    try:
        ser = serial.Serial(args.port, BAUD, timeout=1)
        time.sleep(1)
        ser.reset_input_buffer()
        print(f"Serial ouvert : {args.port}\n")
    except Exception as e:
        print(f"[WARN] Pas de serial ({e}) — test sans logs (EOF non détectable)")
        ser = None

    # Volume à 5 % pour les tests
    api("/api/volume?v=5", ip=args.ip)
    print("Volume réglé à 5 %\n")

    # Vérifier que le boîtier répond
    state = api("/api/state", method="GET", ip=args.ip)
    try:
        js = json.loads(state)
        loco = js["voie1"]["loco"]
        print(f"Boîtier OK — loco voie 1 : {loco}\n")
        if loco != "TGV":
            print(f"[WARN] La loco n'est pas TGV (c'est {loco}).")
            print("Basculer sur TGV ? (o/n) ", end="", flush=True)
            if input().strip().lower() in ("o", "oui", "y", "yes"):
                api("/api/loco?voie=1&id=TGV", ip=args.ip)
                time.sleep(1)
    except Exception:
        print(f"[ERREUR] Boîtier ne répond pas : {state}")
        sys.exit(1)

    if args.load:
        run_load_test(ser, args.ip)
        if ser: ser.close()
        return

    all_tests = TESTS_ANNONCES + TESTS_SONS_COURTS
    if args.only:
        all_tests = [(s, d) for s, d in all_tests if s == args.only]
        if not all_tests:
            all_tests = [(args.only, 30.0)]

    results = []
    pause_between = 2.0

    print(f"{'Son':35s}  {'Attendu':>8s}  {'Réel':>8s}  Résultat")
    print("-" * 75)

    for seq_id, expected_s in all_tests:
        stop_all(args.ip)
        time.sleep(pause_between)

        print(f"  {seq_id:33s}  {expected_s:7.1f}s  ", end="", flush=True)
        ok, elapsed, notes = run_test(seq_id, expected_s, ser=ser, ip=args.ip)
        status = "✓ OK" if ok else "✗ FAIL"
        print(f"{elapsed:7.1f}s  {status}  {notes}")
        results.append((seq_id, expected_s, elapsed, ok, notes))

    stop_all(args.ip)
    if ser:
        ser.close()

    passed = sum(1 for *_, ok, _ in results if ok)
    total  = len(results)
    print()
    print(f"═══ Résultats : {passed}/{total} OK ═══")
    if passed < total:
        print("\nÉchecs :")
        for seq_id, exp, elapsed, ok, notes in results:
            if not ok:
                print(f"  {seq_id:35s}  {notes}")

if __name__ == "__main__":
    main()
