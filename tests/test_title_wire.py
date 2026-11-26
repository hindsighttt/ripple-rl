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

    def test_spawned_title_served_in_reference_format(self):
        spawn.add_title({"titleId": "PsyonixHackathonWinner"})
        self.assertEqual(
            social.title_response(),
            "PsyonixHackathonWinner|" + RIPPLE_BLOB,
        )

    def test_blob_is_constant_across_titles(self):
        spawn.add_title({"titleId": "S23_2v2_TopRank_100"})
        blob = social.title_response().split("|", 1)[1]
        self.assertEqual(blob, RIPPLE_BLOB)

    def test_most_recent_spawned_title_wins(self):
        spawn.add_title({"titleId": "Developer"})
        spawn.add_title({"titleId": "PsyonixHackathonWinner"})
        self.assertEqual(social.active_title_id(), "PsyonixHackathonWinner")

    def test_custom_display_title_takes_precedence_over_spawn(self):
        spawn.add_title({"titleId": "Developer"})
        features.set_state("titles", {"enabled": True,
            "titles": [{"id": "Season_13_Grand_Champion", "text": "x", "custom": True}]})
        self.assertEqual(social.active_title_id(), "Season_13_Grand_Champion")
        self.assertEqual(social.title_response(),
                         "Season_13_Grand_Champion|" + RIPPLE_BLOB)

    def test_no_longer_emits_spacket_json(self):
        spawn.add_title({"titleId": "Developer"})
        self.assertNotIn("SPacket1", social.title_response())


if __name__ == "__main__":
    unittest.main()
