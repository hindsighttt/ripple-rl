import asyncio
import base64
import copy
import hmac
import json
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch, mock_open

from ripple import spawn, store
from ripple.proxy_addon import (
    RippleAddon, REQUEST_KEY, _expired_products, _filter_expiration_request,
)


class SpawnRemovalTests(unittest.TestCase):
    def test_single_and_bulk_remove_only_selected_instances(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(store, "STATE_DIR", folder):
            store._json_cache.clear()
            try:
                store.write_json("spawn.json", {"items": [
                    {"instanceId": "a", "productId": 386},
                    {"instanceId": "b", "productId": 386},
                    {"instanceId": "c", "productId": 3036},
                ]})
                self.assertEqual(spawn.remove({"instanceId": "a", "productId": 386})["removed"], 1)
                self.assertEqual([r["instanceId"] for r in spawn._load()["items"]], ["b", "c"])
                self.assertEqual(spawn.remove_many({"instanceIds": ["b"]})["removed"], 1)
                self.assertEqual(spawn.clear()["removed"], 1)
            finally:
                store._json_cache.clear()

    def test_expiry_preserves_paint_and_original_record(self):
        product = {"ProductID": 386, "InstanceID": "spawned", "UpdatedTimestamp": 999,
                   "Attributes": [{"Key": "Painted", "Value": 12}]}
        original = copy.deepcopy(product)
        row = _expired_products([product], 1000)[0]
        self.assertEqual(product, original)
        self.assertIn({"Key": "Painted", "Value": 12}, row["Attributes"])
        self.assertIn({"Key": "ExpirationTime", "Value": 940}, row["Attributes"])
        self.assertGreater(row["UpdatedTimestamp"], product["UpdatedTimestamp"])

    def test_cleanup_local_ids_and_forward_only_real_ids_with_valid_signature(self):
        for ids in (["spawned"], ["owned"], ["spawned", "owned"]):
            body = json.dumps({"InstanceIDs": ids}).encode()
            sig = base64.b64encode(hmac.digest(REQUEST_KEY, b"-" + body, "sha256"))
            headers = {b"psysig": sig}
            header = b"PsySig: " + sig
            result = _filter_expiration_request(header, body, headers, {"spawned"})
            if ids == ["spawned"]:
                self.assertEqual(result, b"")
            elif ids == ["owned"]:
                self.assertIsNone(result)
            else:
                new_header, new_body = result.split(b"\r\n\r\n")
                self.assertEqual(json.loads(new_body)["InstanceIDs"], ["owned"])
                expected = base64.b64encode(hmac.digest(REQUEST_KEY, b"-" + new_body, "sha256"))
                self.assertEqual(new_header, b"PsySig: " + expected)


class SpawnMonitorRemovalTests(unittest.IsolatedAsyncioTestCase):
    async def test_removed_saved_item_pushes_client_expiration(self):
        addon = RippleAddon()
        flow = SimpleNamespace(id="game", live=True)
        addon.live_flows[flow.id] = flow
        addon.inventory_ready.add(flow.id)
        addon.garage_sent_revision[flow.id] = b"0000000000001"
        addon.delivered_spawn[flow.id] = {"spawned": {
            "ProductID": 386, "InstanceID": "spawned", "Attributes": [], "UpdatedTimestamp": 1,
        }}
        with patch("ripple.proxy_addon.asyncio.sleep", new_callable=AsyncMock,
                   side_effect=[None, asyncio.CancelledError]), \
                patch("ripple.proxy_addon.spawn.product_rows", return_value=[]), \
                patch("ripple.proxy_addon.garage_wire.response", return_value=b"0000000000001"), \
                patch("ripple.proxy_addon.ctx.master", create=True) as master, \
                patch("builtins.open", mock_open()):
            with self.assertRaises(asyncio.CancelledError):
                await addon.monitor_spawn()
        args = master.commands.call.call_args.args
        self.assertEqual(args[:3], ("inject.websocket", flow, True))
        body = json.loads(args[3].split(b"\r\n\r\n")[1])
        self.assertEqual(body["ProductData"][0]["InstanceID"], "spawned")
        self.assertEqual(body["ProductData"][0]["Attributes"][0]["Key"], "ExpirationTime")
        self.assertEqual(addon.delivered_spawn[flow.id], {})


if __name__ == "__main__":
    unittest.main()
