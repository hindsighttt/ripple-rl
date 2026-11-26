#!/usr/bin/env python3
"""Ripple RL IPC - send commands to a running Ripple RL backend over IPC (127.0.0.1:49327).

For testing features whose UI pages are preview-only mockups. Launch ripple_rl.exe
(or ripple_backend.exe), then:

  py tools\\poke.py overlay-fps on|off       # FPS overlay (shows "FPS 0" even with no match)
  py tools\\poke.py overlay-ball on|off      # ball-speed overlay (text appears once in a match)
  py tools\\poke.py overlay-roster on|off    # roster overlay (populates in a match)
  py tools\\poke.py qc "<text>" <key> [tgt]  # bind a quickchat line, e.g.  qc "gg" F5 0
  py tools\\poke.py raw <command> ['<json>']  # any backend command, e.g. raw spawn.status

Prints the reply, then listens ~2s for pushed events (e.g. tracker.changed).
"""
import socket, json, sys, time

HOST, PORT = "127.0.0.1", 49327


def send(reqs, listen=2.0):
    try:
        s = socket.create_connection((HOST, PORT), timeout=5)
    except OSError:
        print("Could not connect to the Ripple RL backend on 127.0.0.1:49327 - is the app running?")
        return
    s.settimeout(0.5)
    want = set()
    for i, r in enumerate(reqs, 1):
        r["id"] = i
        want.add(i)
        s.sendall((json.dumps(r) + "\n").encode())
    buf = b""
    end = time.time() + listen
    while time.time() < end:
        try:
            data = s.recv(65536)
        except socket.timeout:
            if not want:
                break
            continue
        if not data:
            break
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            if not line.strip():
                continue
            m = json.loads(line.decode())
            if m.get("type") == "reply" and m.get("id") in want:
                want.discard(m["id"])
                print("reply:", m.get("error") or m.get("payload"))
            elif m.get("type") != "reply":
                print("event:", m.get("type"), m.get("payload"))
    s.close()


def onoff(v):
    return str(v).lower() in ("on", "true", "1", "yes")


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    cmd = a[0]
    if cmd == "overlay-fps":
        send([{"type": "fps.set", "payload": {"on": onoff(a[1])}}])
    elif cmd == "overlay-ball":
        send([{"type": "tracker.set", "payload": {"key": "ballEnabled", "value": onoff(a[1])}}])
    elif cmd == "overlay-roster":
        send([{"type": "tracker.set", "payload": {"key": "rosterEnabled", "value": onoff(a[1])}}])
    elif cmd == "qc":
        text, key = a[1], a[2]
        target = int(a[3]) if len(a) > 3 else 0
        send([
            {"type": "misc.qc.setEntries", "payload": {"entries": [{"text": text, "key": key, "target": target}]}},
            {"type": "misc.qc.setEnabled", "payload": {"value": True}},
        ])
        print(f"Bound '{text}' to {key} (target {target}). Focus Rocket League and press {key}.")
    elif cmd == "raw":
        payload = json.loads(a[2]) if len(a) > 2 else {}
        send([{"type": a[1], "payload": payload}])
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
