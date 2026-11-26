"""Local line-delimited-JSON TCP bridge for the native (ImGui/C++) frontend.

The C++ UI connects to 127.0.0.1:IPC_PORT and speaks the SAME contract the WebView
SPA used, just over a socket instead of the `chrome.webview` shim:

  client -> server request : {"type": "<command>", "payload": {...}, "id": <int>}
  server -> client reply   : {"type": "reply", "id": <int>, "payload": {...}}  (or "error")
  server -> client event   : {"type": "<event>", "payload": {...}}            (no id)

Each JSON object is one line (terminated by '\\n'). Requests are queued and drained on
the Qt main thread (see IpcApp), because bridge.dispatch touches Qt objects (QTimer,
overlays) exactly as it did when the WebView called it on the GUI thread.
"""

import json
import queue
import socket
import threading

IPC_PORT = 49327


class IpcServer:
    def __init__(self, host="127.0.0.1", port=IPC_PORT):
        self.host = host
        self.port = port
        self._sock = None
        self._clients = []
        self._clients_lock = threading.Lock()
        self._requests = queue.Queue()
        self._running = False

    def start(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((self.host, self.port))
        s.listen(8)
        self._sock = s
        self._running = True
        threading.Thread(target=self._accept_loop, name="ipc-accept", daemon=True).start()
        return self.port

    def _accept_loop(self):
        while self._running:
            try:
                conn, _ = self._sock.accept()
            except OSError:
                break
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            with self._clients_lock:
                self._clients.append(conn)
            threading.Thread(target=self._read_loop, args=(conn,),
                             name="ipc-client", daemon=True).start()

    def _read_loop(self, conn):
        buf = b""
        while self._running:
            try:
                data = conn.recv(65536)
            except OSError:
                break
            if not data:
                break
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    msg = json.loads(line.decode("utf-8"))
                except Exception:
                    continue
                self._requests.put(msg)
        with self._clients_lock:
            if conn in self._clients:
                self._clients.remove(conn)
        try:
            conn.close()
        except OSError:
            pass

    def drain(self, dispatch):
        """Call on the Qt main thread: run every pending request through dispatch,
        which itself sends replies/errors back via the bridge's app.send -> broadcast."""
        while True:
            try:
                msg = self._requests.get_nowait()
            except queue.Empty:
                return
            try:
                dispatch(msg)
            except Exception as error:
                mid = msg.get("id")
                if mid is not None:
                    self.broadcast({"type": "reply", "id": mid, "error": str(error)})

    def broadcast(self, obj):
        """Send a JSON object (reply or event) to every connected client."""
        data = (json.dumps(obj) + "\n").encode("utf-8")
        with self._clients_lock:
            dead = []
            for c in self._clients:
                try:
                    c.sendall(data)
                except OSError:
                    dead.append(c)
            for c in dead:
                self._clients.remove(c)
                try:
                    c.close()
                except OSError:
                    pass

    def client_count(self):
        with self._clients_lock:
            return len(self._clients)

    def stop(self):
        self._running = False
        try:
            if self._sock:
                self._sock.close()
        except OSError:
            pass
        with self._clients_lock:
            for c in self._clients:
                try:
                    c.close()
                except OSError:
                    pass
            self._clients.clear()
