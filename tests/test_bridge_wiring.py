import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import Mock, patch

from ripple import store, packs, features, quickchat
from ripple.bridge import Bridge
from ripple.overlay import Overlays


class BridgeWiringTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.patchers = [patch.object(store, "STATE_DIR", str(self.root)),
                         patch("ripple.bridge._steam_path", return_value=""),
                         patch("ripple.bridge._epic_path", return_value=""),
                         patch("ripple.bridge._rl_running", return_value=False)]
        for p in self.patchers:
            p.start()
            self.addCleanup(p.stop)
        store._json_cache.clear()
        self.addCleanup(store._json_cache.clear)
        self.app = Mock()
        self.bridge = Bridge(self.app)

    def call(self, command, payload=None):
        self.app.send.reset_mock()
        self.bridge.dispatch({"type": command, "payload": payload or {}, "id": "test"})
        response = self.app.send.call_args.args[0]
        if "error" in response:
            raise AssertionError(response["error"])
        return response["payload"]

    def test_tracker_key_value_controls_save_actual_setting_and_apply(self):
        self.call("tracker.set", {"key": "ballEnabled", "value": True})
        state = features.get("tracker.settings")
        self.assertTrue(state["ballEnabled"])
        self.assertNotIn("key", state)
        self.app.apply_overlays.assert_called_once()

    def test_username_history_delete_does_not_clear_current_name(self):
        self.call("home.setPseudo", {"value": "First"})
        self.call("home.setPseudo", {"value": "Second"})
        self.call("home.setPseudo", {"value": "First"})
        self.assertEqual(self.call("home.state")["pseudoHistory"], ["First", "Second"])
        self.call("home.forgetPseudo", {"value": "Second"})
        self.assertEqual(store.settings()["pseudo"], "First")
        self.assertEqual(store.settings()["pseudoHistory"], ["First"])

    def test_source_picker_contract_and_switch_preserve_both_paths(self):
        for source in ("Epic", "Steam"):
            cooked = self.root / source / "TAGame" / "CookedPCConsole"
            cooked.mkdir(parents=True)
            self.call("settings.setSourcePath", {"source": source, "path": str(self.root / source)})
        self.call("home.setInstall", {"source": "Epic"})
        self.assertEqual(store.cooked_dir(), str(self.root / "Epic" / "TAGame" / "CookedPCConsole"))
        self.assertEqual(len(self.call("home.state")["installs"]), 2)

    def test_window_settings_and_hotkey_payload_are_wired(self):
        self.call("settings.setCloseToTray", {"value": False})
        self.call("settings.setAlwaysOnTop", {"value": True})
        self.call("settings.setBringToFront", {"value": "Ctrl+K@75:2"})
        self.assertFalse(store.settings()["closeToTray"])
        self.assertTrue(store.settings()["alwaysOnTop"])
        self.app.apply_window_settings.assert_called_once()
        self.app.reload_qc.assert_called_once()
        self.assertEqual(quickchat.parse(store.settings()["bringToFrontHotkey"]), (2, 75))

    def test_pack_spa_routes_import_apply_rename_and_restore_original(self):
        directory = self.root / "packs"
        directory.mkdir()
        cooked = self.root / "CookedPCConsole"
        cooked.mkdir()
        target = cooked / "Ball.upk"
        target.write_bytes(b"original")
        store.save_settings({"target": {"source": "Epic", "cookedDir": str(cooked)}})
        archive = self.root / "Example.zip"
        with zipfile.ZipFile(archive, "w") as z:
            z.writestr("folder/Ball.upk", b"replacement")
        with patch.object(packs, "KIND_DIRS", {"ball": str(directory)}):
            self.call("custom.import", {"kind": "ball", "path": str(archive)})
            self.assertEqual(self.call("custom.list", {"kind": "ball"})["packs"][0]["slug"], "Example")
            self.call("custom.ball.apply", {"slug": "Example"})
            self.assertEqual(target.read_bytes(), b"replacement")
            self.assertEqual(Path(str(target) + ".ripplebak").read_bytes(), b"original")
            self.assertTrue(self.call("custom.list", {"kind": "ball"})["packs"][0]["active"])
            self.call("custom.rename", {"kind": "ball", "slug": "Example", "name": "Renamed"})
            self.call("custom.ball.restore")
            self.assertEqual(target.read_bytes(), b"original")
            self.call("custom.remove", {"kind": "ball", "slug": "Renamed"})
            self.assertEqual(self.call("custom.list", {"kind": "ball"})["packs"], [])

    def test_ball_overlay_uses_requested_units_and_precision(self):
        overlay = Mock()
        Overlays.set_ball(overlay, 1000, "mph", 2)
        overlay.ball_label.setText.assert_called_with("22.37 mph")
        Overlays.set_ball(overlay, 1000, "m/s", 0)
        overlay.ball_label.setText.assert_called_with("10 m/s")


if __name__ == "__main__":
    unittest.main()
