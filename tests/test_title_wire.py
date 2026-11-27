"""/s/title must match the reference app's real wire: "<TitleID>|B1:<constant blob>".
Captured from Ripple RL in a live match (2026-11-24); the blob is identical across titles."""

import tempfile
import unittest

from ripple import social, spawn, features, store

# Exact blob captured from the reference app for two different titles (byte-identical).
RIPPLE_BLOB = "B1:ARUBAAAA1MoBAAAAAAAAAAAAAAAAAAAAAAA="


class TitleWireTests(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.mkdtemp()
        self._saved = store.STATE_DIR
        store.STATE_DIR = self._dir
        store._json_cache.clear()

    def tearDown(self):
        store.STATE_DIR = self._saved
        store._json_cache.clear()

    def test_empty_when_no_title(self):
        self.assertEqual(social.title_response(), "")

    def test_latest_custom_is_served_in_reference_format(self):
        spawn.add_title({"titleId": "Developer"})
        features.set_state("titles", {"enabled": True, "titles": [
            {"id": "CustomTitle_Shift_OLD", "custom": True},
            {"id": "CustomTitle_Ripple_NEW", "custom": True},
            {"id": "Developer", "custom": False},
        ]})
        self.assertEqual(social.title_response(), "CustomTitle_Ripple_NEW|" + RIPPLE_BLOB)

    def test_latest_spawned_title_is_served_without_a_custom_title(self):
        spawn.add_title({"titleId": "PsyonixHackathonWinner"})
        spawn.add_title({"titleId": "Developer"})
        self.assertEqual(social.title_response(), "Developer|" + RIPPLE_BLOB)

    def test_legacy_custom_id_is_supported(self):
        features.set_state("titles", {"enabled": True, "titles": [
            {"id": "CustomTitle_Shift_ABCD1234", "custom": True},
        ]})
        self.assertEqual(social.title_response(), "CustomTitle_Shift_ABCD1234|" + RIPPLE_BLOB)

    def test_empty_when_custom_titles_disabled(self):
        features.set_state("titles", {"enabled": False, "titles": [
            {"id": "CustomTitle_Ripple_ABCD1234", "custom": True},
        ]})
        self.assertEqual(social.title_response(), "")

    def test_spawned_title_is_served_when_custom_titles_are_disabled(self):
        spawn.add_title({"titleId": "Developer"})
        features.set_state("titles", {"enabled": False, "titles": [
            {"id": "CustomTitle_Ripple_ABCD1234", "custom": True},
        ]})
        self.assertEqual(social.title_response(), "Developer|" + RIPPLE_BLOB)

    def test_no_longer_emits_spacket_json(self):
        features.set_state("titles", {"enabled": True,
            "titles": [{"id": "CustomTitle_Ripple_ABCD1234", "text": "x", "custom": True}]})
        self.assertNotIn("SPacket1", social.title_response())


if __name__ == "__main__":
    unittest.main()
