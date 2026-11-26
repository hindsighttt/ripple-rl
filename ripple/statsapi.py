import json, socket, threading

HOST = "127.0.0.1"
PORT = 49123


class Stats:
    def __init__(self, on_event):
        self.on_event = on_event
        self.sock = None
        self.thread = None
        self.stop = False
        self.buf = b""
        self._stop_event = threading.Event()
        self._lifecycle_lock = threading.Lock()

    def start(self):
        with self._lifecycle_lock:
            if self.thread and self.thread.is_alive():
                return
            self.stop = False
            self._stop_event.clear()
            self.thread = threading.Thread(target=self._run, daemon=True)
            self.thread.start()

    def _connect(self):
        try:
            return socket.create_connection((HOST, PORT), timeout=2)
        except OSError:
            return None

    def _run(self):
        retry_delay = 0.5
        while not self._stop_event.is_set():
            s = self._connect()
            self.sock = s
            self.buf = b""
            received_data = False
            try:
                if s is not None:
                    s.settimeout(0.5)
                    while not self._stop_event.is_set():
                        try:
                            chunk = s.recv(65536)
                        except socket.timeout:
                            continue
                        if not chunk:
                            break
                        received_data = True
                        self.buf += chunk
                        while b"\n" in self.buf:
                            line, self.buf = self.buf.split(b"\n", 1)
                            if not line.strip():
                                continue
                            try:
                                ev = json.loads(line)
                                data = ev["Data"]
                                if isinstance(data, str):
                                    data = json.loads(data)
                                self.on_event(ev.get("Event"), data)
                            except (ValueError, KeyError, TypeError):
                                continue
                            except Exception:
                                # A consumer failure must not stop the transport.
                                continue
            except OSError:
                pass
            finally:
                if s is not None:
                    s.close()
                self.sock = None
                self.buf = b""
            if received_data:
                retry_delay = 0.5
            self._stop_event.wait(retry_delay)
            retry_delay = min(retry_delay * 2, 5.0)

    def shutdown(self):
        with self._lifecycle_lock:
            self.stop = True
            self._stop_event.set()
            sock = self.sock
            thread = self.thread
        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            finally:
                sock.close()
        if thread and thread is not threading.current_thread():
            thread.join(2.5)


def ensure_ini(cooked_dir, port=PORT):
    """Enable the game's Stats API on the target install."""
    import os
    ini = os.path.join(cooked_dir, "..", "Config", "DefaultStatsAPI.ini")
    ini = os.path.normpath(ini)
    os.makedirs(os.path.dirname(ini), exist_ok=True)
    bak = ini + ".ripple-bak"
    if os.path.exists(ini) and not os.path.exists(bak):
        open(bak, "wb").write(open(ini, "rb").read())
    body = (
        "[TAGame.MatchStatsExporter_TA]\r\n"
        "; Ripple RL - Stats API\r\n"
        "Port=%d\r\nPacketSendRate=10\r\n" % port
    )
    open(ini, "w", encoding="utf-8").write(body)
