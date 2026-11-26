"""PsyNet inventory edits must stay signed and arrive as complete messages."""

import base64
import copy
import hmac
import json
import unittest
from unittest.mock import patch

from mitmproxy.proxy.layers.websocket import Fragmentizer

from ripple.proxy_addon import RESPONSE_KEY, _inventory_frame


class InventoryWireTests(unittest.TestCase):
    def frame(self, data):
        body = json.dumps(data, separators=(",", ":")).encode()
        signature = base64.b64encode(hmac.digest(RESPONSE_KEY, b"1795521928-" + body, "sha256"))
        header = b"PsyTime: 1795521928\r\nPsySig: " + signature + b"\r\nPsyResponseID: request-1"
        return header, body, {b"psytime": b"1795521928", b"psysig": signature}

    def test_signed_inventory_preserves_owned_products_and_unknown_fields(self):
        owned = {"ProductID": 1, "InstanceID": "owned", "Attributes": [], "FutureField": True}
        data = {"Result": {"ProductData": [copy.deepcopy(owned)], "Other": 42}}
        header, body, headers = self.frame(data)
        spawned = {"ProductID": 386, "InstanceID": "spawned", "Attributes": [{"Key": "Painted", "Value": 12}]}
        with patch("ripple.proxy_addon.spawn.product_rows", return_value=[spawned]):
            edited, added = _inventory_frame(header, body, headers, data)
        self.assertEqual(added, 1)
        new_header, new_body = edited.split(b"\r\n\r\n", 1)
        parsed = json.loads(new_body)
        self.assertEqual(parsed["Result"]["ProductData"], [owned, spawned])
        self.assertEqual(parsed["Result"]["Other"], 42)
        expected = base64.b64encode(hmac.digest(RESPONSE_KEY, b"1795521928-" + new_body, "sha256"))
        self.assertIn(b"PsySig: " + expected, new_header.split(b"\r\n"))
        frames = list(Fragmentizer([header + b"\r\n\r\n" + body], False)(edited))
        self.assertEqual(len(frames), 1)
        self.assertTrue(frames[0].message_finished)
        self.assertEqual(frames[0].data, edited)

    def test_resized_large_response_does_not_create_continuation_frames(self):
        frames = list(Fragmentizer([b"x" * 11000], False)(b"y" * 11356))
        self.assertEqual(len(frames), 1)
        self.assertTrue(frames[0].message_finished)

    def test_invalid_signature_is_left_untouched(self):
        data = {"Result": {"ProductData": []}}
        header, body, headers = self.frame(data)
        headers[b"psysig"] = b"invalid"
        self.assertEqual(_inventory_frame(header, body, headers, data), (None, 0))
        self.assertEqual(data["Result"]["ProductData"], [])

    def test_duplicate_instance_is_not_added(self):
        product = {"ProductID": 386, "InstanceID": "spawned"}
        data = {"Result": {"ProductData": [product]}}
        header, body, headers = self.frame(data)
        with patch("ripple.proxy_addon.spawn.product_rows", return_value=[product]):
            self.assertEqual(_inventory_frame(header, body, headers, data), (None, 0))


if __name__ == "__main__":
    unittest.main()
