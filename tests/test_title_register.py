"""Custom title registration into PlayerTitleConfig (cache + HTTP response)."""

import base64
import hashlib
import hmac
import json
import os
import struct
import tempfile
import unittest

from ripple import features, store, titles_config


BASE = {
    "RegionConfig": {"Class": "RegionConfig_X", "Regions": []},
    "PlayerTitleConfig": {
        "Class": "TitleConfig_X",
        "Categories": [{"ID": "Developer", "Color": "00FFAA", "GlowColor": "00FFFF"}],
        "Titles": [{"ID": "Developer", "Category": "Developer", "Text": "Developer"}],
    },
    "ShopConfig": {"Class": "ShopConfig_X"},
}


def cache_file(body, crc=0x12345678, signature="sig"):
    payload = json.dumps(BASE, separators=(",", ":")).encode() if body is None else body
    prefix = bytearray()
    prefix += struct.pack("<I", 0)              # total size - 8, patched later
    prefix += struct.pack("<I", 0xAABBCCDD)     # header hash, preserved
    prefix += struct.pack("<I", 0)              # total size - 4, patched later
    prefix += struct.pack("<I", 0xFFFFFFFF)
    prefix += struct.pack("<I", 5)
    prefix += b"Data\x00"
    prefix += struct.pack("<I", 14)
    prefix += b"ArrayProperty\x00"
    prefix += struct.pack("<I", len(payload) + 4)
    prefix += struct.pack("<I", 0)
    prefix += struct.pack("<I", len(payload))
    props = (b"\x08\x00\x00\x00DataCRC\x00\x0c\x00\x00\x00IntProperty\x00"
             b"\x04\x00\x00\x00\x00\x00\x00\x00" + struct.pack("<I", crc) +
             b"\x05\x00\x00\x00ETag\x00\x0c\x00\x00\x00StrProperty\x00\x0c\x00\x00\x00"
             b"\x00\x00\x00\x00\x08\x00\x00\x001234567\x00"
             b"\x0c\x00\x00\x00ContentType\x00\x0c\x00\x00\x00StrProperty\x00"
             b"\x15\x00\x00\x00\x00\x00\x00\x00\x11\x00\x00\x00application/json\x00"
             b"\x0a\x00\x00\x00Signature\x00\x0c\x00\x00\x00StrProperty\x00"
             b"\x31\x00\x00\x00\x00\x00\x00\x00" + struct.pack("<I", len(signature) + 1) +
             signature.encode() + b"\x00\x05\x00\x00\x00None\x00")
    total = len(prefix) + len(payload) + len(props)
    struct.pack_into("<I", prefix, 0, total - 8)
    struct.pack_into("<I", prefix, 8, total - 4)
    return bytes(prefix) + payload + props


def read(path):
    with open(path, "rb") as stream:
        return stream.read()


class TitleRegisterTests(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.mkdtemp()
        self._saved_state = store.STATE_DIR
        store.STATE_DIR = self._dir
        store._json_cache.clear()
        features.set_state("titles", {
            "enabled": True,
            "titles": [
                {"id": "CustomTitle_abc123", "text": "HELLO \"WORLD\"",
                 "color": "#00ff00", "glow": "#ff0000", "custom": True},
            ],
        })
        self.cache = tempfile.mkdtemp()

    def tearDown(self):
        store.STATE_DIR = self._saved_state
        store._json_cache.clear()

    def test_injects_linked_title_and_category(self):
        cfg = json.loads(json.dumps(BASE))
        self.assertTrue(titles_config.inject(cfg))
        block = cfg["PlayerTitleConfig"]
        title = block["Titles"][-1]
        category = block["Categories"][-1]
        self.assertEqual(title, {"ID": "CustomTitle_abc123",
                                 "Category": "shift_custom_CustomTitle_abc123",
                                 "Text": "HELLO \"WORLD\""})
        self.assertEqual(category, {"ID": "shift_custom_CustomTitle_abc123",
                                    "Color": "00FF00", "GlowColor": "FF0000"})
        # Catalog records are left alone.
        self.assertIn({"ID": "Developer", "Category": "Developer", "Text": "Developer"},
                      block["Titles"])

    def test_idempotent(self):
        cfg = json.loads(json.dumps(BASE))
        self.assertTrue(titles_config.inject(cfg))
        self.assertFalse(titles_config.inject(cfg))
        self.assertEqual(len(cfg["PlayerTitleConfig"]["Categories"]), 2)

    def test_disabled_removes_records(self):
        cfg = json.loads(json.dumps(BASE))
        titles_config.inject(cfg)
        features.set_state("titles", {"enabled": False,
                                      "titles": [{"id": "CustomTitle_abc123", "text": "x",
                                                  "custom": True}]})
        self.assertTrue(titles_config.inject(cfg))
        self.assertEqual(len(cfg["PlayerTitleConfig"]["Titles"]), 1)
        self.assertEqual(len(cfg["PlayerTitleConfig"]["Categories"]), 1)

    def test_missing_block_is_ignored(self):
        self.assertFalse(titles_config.inject({"RegionConfig": {}}))

    def test_cache_round_trip_preserves_body_size_and_properties(self):
        original = cache_file(None)
        path = os.path.join(self.cache, "cfg")
        with open(path, "wb") as stream:
            stream.write(original)
        self.assertTrue(titles_config.refresh_file(path))
        with open(path, "rb") as stream:
            written = stream.read()
        prefix, body, props = titles_config._split_cache(written)
        parsed = json.loads(body)
        self.assertEqual(parsed["PlayerTitleConfig"]["Categories"][-1]["Color"], "00FF00")
        self.assertIn(b"DataCRC", props)
        self.assertIn(struct.pack("<I", 0x12345678), props)
        self.assertEqual(struct.unpack_from("<I", written, 0)[0], len(written) - 8)
        self.assertEqual(struct.unpack_from("<I", written, 8)[0], len(written) - 4)
        self.assertEqual(struct.unpack_from("<I", written, 4)[0],
                         titles_config.header_crc(written[8:]))
        self.assertTrue(os.path.exists(path + ".ripplebak"))

    def test_header_crc_matches_reference_cache(self):
        name = ("NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_"
                "YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg==")
        roots = [os.path.join(os.environ.get("USERPROFILE", ""), "Documents", "My Games",
                              "Rocket League", "TAGame", "Cache", "WebCache.before-launcher-only-20261125-163354"),
                 os.path.join(os.environ.get("USERPROFILE", ""), "Documents", "My Games",
                              "Rocket League", "TAGame", "Cache", "WebCache")]
        path = next((os.path.join(r, name) for r in roots if os.path.exists(os.path.join(r, name))), None)
        if path is None:
            self.skipTest("reference cache not present")
        data = read(path)
        stored = struct.unpack_from("<I", data, 4)[0]
        # Only the pre-injection reference copy is guaranteed to be intact.
        self.assertEqual(stored, titles_config.header_crc(data[8:]))

    def test_restore_webcache(self):
        path = os.path.join(self.cache, "cfg")
        original = cache_file(None)
        with open(path, "wb") as stream:
            stream.write(original)
        titles_config.refresh_file(path)
        self.assertNotEqual(read(path), original)
        titles_config.restore_webcache(self.cache)
        self.assertEqual(read(path), original)
        self.assertFalse(os.path.exists(path + ".ripplebak"))

    def test_live_response_is_rewritten_and_resigned(self):
        from ripple import proxy_addon
        body = json.dumps(BASE, separators=(",", ":")).encode()
        psy_time = b"1795622521000"
        signature = base64.b64encode(
            hmac.digest(proxy_addon.RESPONSE_KEY, psy_time + b"-" + body, "sha256")).decode()

        class Response:
            status_code = 200
            headers = {"PsyTime": psy_time.decode(), "PsySig": signature}
            content = body

        class Flow:
            response = Response()

        proxy_addon._rewrite_title_config(Flow)
        updated = Flow.response.content
        payload = json.loads(updated)
        self.assertEqual(payload["PlayerTitleConfig"]["Categories"][-1]["Color"], "00FF00")
        expected = base64.b64encode(
            hmac.digest(proxy_addon.RESPONSE_KEY, psy_time + b"-" + updated, "sha256")).decode()
        self.assertEqual(Flow.response.headers["PsySig"], expected)

    def test_watch_reinjects_after_client_rewrite(self):
        path = os.path.join(self.cache, "cfg")
        original = cache_file(None)
        with open(path, "wb") as stream:
            stream.write(original)
        titles_config.refresh_file(path)
        seen = {}
        titles_config.cache_seen(seen, self.cache)
        titles_config.restore_webcache(self.cache)
        self.assertEqual(read(path), original)
        self.assertEqual(titles_config.watch_webcache(seen, self.cache), 1)
        _p, body, _props = titles_config._split_cache(read(path))
        self.assertIn(b"CustomTitle_abc123", body)
        self.assertEqual(titles_config.watch_webcache(seen, self.cache), 0)

    def test_unverified_live_response_is_left_alone(self):
        from ripple import proxy_addon
        body = json.dumps(BASE, separators=(",", ":")).encode()

        class Response:
            status_code = 200
            headers = {"PsyTime": "1", "PsySig": "wrong"}
            content = body

        class Flow:
            response = Response()

        proxy_addon._rewrite_title_config(Flow)
        self.assertEqual(Flow.response.content, body)


if __name__ == "__main__":
    unittest.main()

