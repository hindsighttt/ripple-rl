"""Title spawning: a title is the generic Title product (3036) + a TitleID attribute,
injected through the same inventory path as item spawns."""

import os
import tempfile
import unittest

from ripple import spawn, store


class TitleSpawnTests(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.mkdtemp()
        self._saved = store.STATE_DIR
        store.STATE_DIR = self._dir          # _file() joins STATE_DIR at call time
        store._json_cache.clear()

    def tearDown(self):
        store.STATE_DIR = self._saved
        store._json_cache.clear()

    def test_add_title_stores_generic_product_and_title_id(self):
        out = spawn.add_title({"titleId": "CRL_Season_1_Champion"})
        self.assertTrue(out["ok"])
        self.assertEqual(out["item"]["productId"], spawn.TITLE_PID)
        self.assertEqual(out["item"]["productId"], 3036)
        self.assertEqual(out["item"]["titleId"], "CRL_Season_1_Champion")
        self.assertTrue(out["item"]["instanceId"])
        self.assertEqual(
            spawn.titles_owned(),
            {"titles": ["CRL_Season_1_Champion"], "known": True},
        )

    def test_add_title_requires_a_title(self):
        with self.assertRaises(ValueError):
            spawn.add_title({"titleId": "  "})
        with self.assertRaises(ValueError):
            spawn.add_title({})

    def test_same_title_is_not_duplicated(self):
        first = spawn.add_title({"titleId": "Developer"})["item"]
        again = spawn.add_title({"titleId": "Developer"})
        self.assertTrue(again.get("already"))
        self.assertEqual(again["item"]["instanceId"], first["instanceId"])
        self.assertEqual(len(spawn._load()["items"]), 1)

    def test_product_rows_emit_title_id_attribute(self):
        spawn.add_title({"titleId": "Developer"})
        rows = spawn.product_rows(1795521928)
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row["ProductID"], 3036)
        self.assertIn({"Key": "TitleID", "Value": "Developer"}, row["Attributes"])

    def test_remove_many_by_instance_removes_only_that_title(self):
        a = spawn.add_title({"titleId": "Developer"})["item"]
        spawn.add_title({"titleId": "CRL_Analyst"})
        res = spawn.remove_many({"instanceIds": [a["instanceId"]]})
        self.assertEqual(res["removed"], 1)
        self.assertEqual(spawn.titles_owned()["titles"], ["CRL_Analyst"])

    def test_titles_are_excluded_from_the_item_spawn_response_shape(self):
        # list_spawn still carries titles (the SPA filters by titleId client-side),
        # but each title keeps its instanceId so it round-trips.
        spawn.add_title({"titleId": "Developer"})
        items = spawn.list_spawn()["items"]
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["titleId"], "Developer")
        self.assertTrue(items[0]["instanceId"])


if __name__ == "__main__":
    unittest.main()
