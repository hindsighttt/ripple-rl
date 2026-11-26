"""Headless backend entry for the native (ImGui/C++) frontend.

Runs the SAME backend as main.py (proxy, Bridge, overlays, Stats API, quick-chat
hotkeys) but, instead of hosting the WebView2 SPA in a QtWebEngine window, exposes
the bridge over a local JSON socket (ripple/ipcserver.py) that the C++ UI
connects to. A QApplication event loop still runs so overlays, hotkeys, QTimer and
Qt signals keep working exactly as before.

Run the backend:   python main_cpp.py
Then launch the C++ UI (ui-cpp/build/bin/ripple_rl.exe).
"""

import os
import sys
import time

from PySide6.QtCore import QObject, Signal, QTimer
from PySide6.QtWidgets import QApplication

from ripple import store, proxy, features, statsapi
from ripple.bridge import Bridge
from ripple.overlay import Overlays
from ripple import quickchat as qcmod
from ripple.ipcserver import IpcServer


class IpcApp(QObject):
    """Stands in for the Qt main window: provides the methods the bridge calls on
    `self.app`, but routes `send()` to connected sockets and leaves window chrome
    to the native frontend."""

    stats_event = Signal(str, object)

    def __init__(self, app, bridge, server):
        super().__init__()
        self.app = app
        self.bridge = bridge
        self.server = server
        self._shutting_down = False

        self.overlays = Overlays()
        self.stats_event.connect(self._on_stats)
        self.stats = statsapi.Stats(self.stats_event.emit)
        self.stats.start()

        self.hk = None
        self.reload_qc()
        self.apply_overlays()

        self.bridge.event.connect(self._on_event)
        app.aboutToQuit.connect(self._shutdown)

        # Run queued IPC requests on the Qt main thread (bridge.dispatch touches Qt).
        self._timer = QTimer(self)
        self._timer.setInterval(4)
        self._timer.timeout.connect(lambda: self.server.drain(self._dispatch))
        self._timer.start()

    def _dispatch(self, msg):
        if os.environ.get("RIPPLE_RL_IPC_DEBUG"):
            print("IPC <-", msg.get("type"), flush=True)
        self.bridge.dispatch(msg)

    # ---- transport ----
    def send(self, obj):
        self.server.broadcast(obj)

    def _on_event(self, type_, payload):
        self.send({"type": type_, "payload": payload or {}})

    # ---- window shims (the native window owns its own chrome) ----
    def showMinimized(self):
        self.send({"type": "win.command", "payload": {"action": "minimize"}})

    def toggle_max(self):
        self.send({"type": "win.command", "payload": {"action": "maximize"}})

    def close(self):
        self.send({"type": "win.command", "payload": {"action": "close"}})

    def isMaximized(self):
        return False

    def apply_window_settings(self):
        pass

    def bring_to_front(self):
        self.send({"type": "win.command", "payload": {"action": "front"}})

    # ---- overlays / hotkeys (same as main.py's Win) ----
    def reload_qc(self):
        try:
            if self.hk:
                self.hk.unbind_all()
                self.app.removeNativeEventFilter(self.hk)
            self.hk = qcmod.quickchat(features.get("qc").get("entries", []))
            self.hk.bind(store.settings().get("bringToFrontHotkey", "F2"), self.bring_to_front)
            self.app.installNativeEventFilter(self.hk)
        except Exception:
            pass

    def apply_overlays(self):
        s = features.get("tracker.settings")
        self.overlays.show_ball(bool(s.get("ballEnabled")))
        self.overlays.show_roster(bool(s.get("rosterEnabled")))
        self.overlays.show_fps(bool(features.get("fps").get("on")))

    def _on_stats(self, ev, data):
        if self._shutting_down:
            return
        try:
            game = data.get("Game") or {}
            players = data.get("Players") or []
            in_match = bool(players)
            if self.overlays.ball.isVisible():
                settings = features.get("tracker.settings")
                self.overlays.set_ball((game.get("Ball") or {}).get("Speed", 0),
                                       settings.get("ballUnit", "kmh"), settings.get("ballDecimals", 2))
                self.overlays._frames += 1
            if self.overlays.roster.isVisible():
                self.overlays.set_roster([{"Name": p.get("Name"), "MMR": 0} for p in players])
            now = time.monotonic()
            last = getattr(self, "_tracker_last", 0.0)
            prev_in = getattr(self, "_tracker_in", None)
            if in_match != prev_in or (now - last) >= 0.25:
                self._tracker_last = now
                self._tracker_in = in_match
                self.bridge.emit_event("tracker.changed", {"event": ev, "players": players, "game": game})
                f = features.get("tracker")
                f["inMatch"] = in_match
                store.write_json("feature_tracker.json", f)
        except Exception:
            pass

    def _shutdown(self):
        if self._shutting_down:
            return
        self._shutting_down = True
        try:
            self.stats.shutdown()
        except Exception as error:
            print("Stats shutdown error:", error)
        if self.hk:
            try:
                self.hk.unbind_all()
                self.app.removeNativeEventFilter(self.hk)
            except Exception as error:
                print("Hotkey teardown error:", error)
            self.hk = None
        try:
            self.server.stop()
        except Exception as error:
            print("IPC stop error:", error)
        try:
            proxy.stop()
        except Exception as error:
            print("Proxy stop error:", error)


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("Ripple RL")
    if store.settings().get("proxyEnabled", True) and not os.environ.get("RIPPLE_RL_NO_PROXY"):
        try:
            proxy.start()
        except Exception as ex:
            print("proxy:", ex)
    server = IpcServer()
    port = server.start()
    print("Ripple RL backend: IPC listening on 127.0.0.1:%d" % port, flush=True)
    bridge = Bridge(None)
    ipc = IpcApp(app, bridge, server)
    bridge.app = ipc
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
