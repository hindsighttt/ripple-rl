import os, sys, json, signal, time
from PySide6.QtCore import Qt, QTimer, QSize, Signal, Slot
from PySide6.QtGui import QIcon, QAction
from PySide6.QtWidgets import QApplication, QMainWindow, QSystemTrayIcon, QMenu
from PySide6.QtWebEngineWidgets import QWebEngineView

from ripple import store, proxy
from ripple.bridge import Bridge
from ripple import ui as ui_mod, features, statsapi
from ripple.overlay import Overlays
from ripple import quickchat as qcmod


class Win(QMainWindow):
    stats_event = Signal(str, object)

    def __init__(self, app, bridge):
        super().__init__()
        self.app = app
        self.bridge = bridge
        self._shutting_down = False
        self.setWindowTitle("Ripple RL")
        self.setWindowIcon(QIcon(os.path.join(store.UI_DIR, "assets", "app-icon.png")))
        self.view = ui_mod.WebView(bridge, self)
        self.setCentralWidget(self.view)
        self.resize(1280, 820)
        self.view.load_app()
        self.bridge.event.connect(self._on_event)
        self.overlays = Overlays()
        self.stats_event.connect(self._on_stats)
        self.stats = statsapi.Stats(self.stats_event.emit)
        self.stats.start()
        self._tray = None
        self._make_tray()
        self.hk = None
        self.reload_qc()
        self.apply_overlays()
        self.apply_window_settings()
        self.app.aboutToQuit.connect(self._shutdown)

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

    def bring_to_front(self):
        self.showNormal()
        self.raise_()
        self.activateWindow()

    def apply_window_settings(self):
        visible = self.isVisible()
        self.setWindowFlag(Qt.WindowStaysOnTopHint, bool(store.settings().get("alwaysOnTop")))
        if visible:
            self.show()

    def _on_event(self, type_, payload):        self.send({"type": type_, "payload": payload or {}})

    def send(self, obj):
        self.view.page().runJavaScript("window.__emit&&window.__emit(%s)" % json.dumps(json.dumps(obj)))

    def toggle_max(self):
        self.showNormal() if self.isMaximized() else self.showMaximized()

    def _make_tray(self):
        if not QSystemTrayIcon.isSystemTrayAvailable():
            return
        self._tray = QSystemTrayIcon(self.windowIcon(), self)
        m = QMenu()
        m.addAction("Show", self.showNormal)
        m.addAction("Quit", self._quit)
        self._tray.setContextMenu(m)
        self._tray.activated.connect(lambda r: self.showNormal() if r == QSystemTrayIcon.Trigger else None)
        self._tray.show()

    def _quit(self):
        self._shutdown()
        QApplication.quit()

    def _shutdown(self):
        if self._shutting_down:
            return
        self._shutting_down = True
        # Each teardown step is isolated so a failure in one still lets the proxy
        # be disabled fully (process killed, system proxy off, WebCache reverted).
        # The bootstrapped ProjectX.upk is deliberately left in place on exit.
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
            proxy.stop()
        except Exception as error:
            print("Proxy stop error:", error)

    def closeEvent(self, e):
        if store.settings().get("closeToTray") and self._tray:
            e.ignore()
            self.hide()
        else:
            self._shutdown()
            e.accept()
    @Slot(str, object)
    def _on_stats(self, ev, data):
        if self._shutting_down:
            return
        try:
            game = data.get("Game") or {}
            players = data.get("Players") or []
            in_match = bool(players)
            # Local overlay widgets are cheap to repaint, so keep them smooth at
            # the full ~10 events/sec the Stats API sends.
            if self.overlays.ball.isVisible():
                settings = features.get("tracker.settings")
                self.overlays.set_ball((game.get("Ball") or {}).get("Speed", 0), settings.get("ballUnit", "kmh"), settings.get("ballDecimals", 2))
                self.overlays._frames += 1
            if self.overlays.roster.isVisible():
                self.overlays.set_roster([{"Name": p.get("Name"), "MMR": 0} for p in players])
            # The disk write + JS round-trip (tracker.changed -> refreshTracker)
            # are the expensive part, so coalesce them to ~4/sec. Always flush
            # immediately when inMatch flips (match start/end) so state isn't
            # stale, and on the first event after a quiet period.
            now = time.monotonic()
            last = getattr(self, "_tracker_last", 0.0)
            prev_in = getattr(self, "_tracker_in", None)
            if in_match != prev_in or (now - last) >= 0.25:
                self._tracker_last = now
                self._tracker_in = in_match
                self.bridge.emit_event("tracker.changed",
                                       {"event": ev, "players": players, "game": game})
                f = features.get("tracker")
                f["inMatch"] = in_match
                store.write_json("feature_tracker.json", f)
        except Exception:
            pass




class IpcApp:
    """Headless app shim: bridges the same command surface to the native C++ UI over TCP."""

    def __init__(self):
        from ripple import ipcserver
        self.server = ipcserver.IpcServer()
        self.bridge = Bridge(self)
        self.bridge.event.connect(lambda t, p: self.server.broadcast(
            {"type": t, "payload": p or {}}))
        self.timer = QTimer()
        self.timer.timeout.connect(lambda: self.server.drain(self.bridge.dispatch))
        self.timer.start(10)

    def start(self):
        self.server.start()
        if store.settings().get("proxyEnabled", True):
            try:
                proxy.start()
            except Exception as ex:
                print("proxy:", ex)

    def send(self, obj):
        self.server.broadcast(obj)

    def reload_qc(self):
        pass

    def apply_overlays(self):
        pass

    def bring_to_front(self):
        pass

    def toggle_max(self):
        pass

    def isMaximized(self):
        return False

    def showMinimized(self):
        pass

    def close(self):
        try:
            self.server.stop()
            proxy.stop()
        finally:
            QApplication.quit()


def run_ipc():
    app = QApplication(sys.argv)
    app.setApplicationName("RippleBackend")
    ipc = IpcApp()
    ipc.start()
    app.aboutToQuit.connect(lambda: ipc.server.stop())
    print("IPC backend on 127.0.0.1:%d" % ipc.server.port, flush=True)
    return app.exec()


def main():
    if "--ipc" in sys.argv:
        return run_ipc()
    ui_mod.register_scheme()
    app = QApplication(sys.argv)
    app.setApplicationName("Ripple RL")
    if store.settings().get("proxyEnabled", True):
        try:
            proxy.start()
        except Exception as ex:
            print("proxy:", ex)
    bridge = Bridge(None)
    win = Win(app, bridge)
    bridge.app = win
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())


