import json
import base64
import hmac
from types import SimpleNamespace
import unittest
from pathlib import Path
from unittest.mock import patch, AsyncMock

from mitmproxy.test import tflow

from ripple import garage_wire
from ripple.proxy_addon import RippleAddon, RESPONSE_KEY


FIXTURES = Path(__file__).parent / "fixtures" / "garage"


class GarageWireTests(unittest.TestCase):
    def test_nine_native_captures_round_trip_byte_exactly(self):
        template = (FIXTURES / "distinct.bin").read_bytes()
        for capture in FIXTURES.glob("*.bin"):
            path = capture.with_suffix(".json")
            with self.subTest(capture=path.stem):
                state = json.loads(path.read_text())
                expected = path.with_suffix(".bin").read_bytes()
                actual = garage_wire.serialize(
                    **state, revision=int(expected[:13]),
                    config_revision=int(expected[9841:9854]), template=template,
                )
                self.assertEqual(actual, expected)

    def test_boundary_values_preserve_fixed_positions(self):
        state = json.loads((FIXTURES / "distinct.json").read_text())
        state["garage"]["camber"] = {"deg": 22.5, "ud": -5, "lr": 5}
        state["garage"]["underglow"][0]["level"] = 0
        state["paint"]["rainbowSpeed"] = 100
        body = garage_wire.serialize(**state, revision=1, config_revision=2)
        self.assertEqual(len(body), 9867)
        self.assertEqual(body[22:25], b"-09")
        self.assertEqual(body[85:95], b"0225-50050")
        self.assertEqual(body[198:201], b"100")

    def test_roulette_setting_refreshes_the_game_config(self):
        state = json.loads((FIXTURES / "distinct.json").read_text())
        roulette = {"on": True}
        with patch.object(garage_wire.features, "get", side_effect=state.__getitem__), \
                patch.object(garage_wire.store, "read_json", return_value=roulette) as read:
            enabled = garage_wire.response()
            roulette["on"] = False
            disabled = garage_wire.response()
            self.assertEqual(garage_wire.response(), disabled)
            roulette["on"] = True
            restored = garage_wire.response()
            read.assert_called_with("feature_roulette.json", {})

        self.assertEqual(len(disabled), 9867)
        self.assertEqual((enabled[714:715], disabled[714:715], restored[714:715]), (b"1", b"0", b"1"))
        self.assertGreater(int(disabled[:13]), int(enabled[:13]))
        self.assertGreater(int(restored[:13]), int(disabled[:13]))
        self.assertEqual(enabled[13:714], disabled[13:714])
        self.assertEqual(enabled[715:9841], disabled[715:9841])
        self.assertEqual(enabled[9854:], disabled[9854:])

    def test_proxy_serves_all_garage_aliases_locally(self):
        body = (FIXTURES / "distinct.bin").read_bytes()
        for endpoint in ("geam", "gean", "gear"):
            flow = tflow.tflow()
            flow.request.host = "127.0.0.1"
            flow.request.path = "/s/" + endpoint
            with patch("ripple.proxy_addon.garage_wire.response", return_value=body):
                RippleAddon().request(flow)
            self.assertEqual(flow.response.status_code, 200)
            self.assertEqual(flow.response.content, body)


class GaragePollTests(unittest.IsolatedAsyncioTestCase):
    async def test_unchanged_gear_waits_until_a_new_revision(self):
        addon = RippleAddon()
        addon.gear_revision = b"0000000000001"
        flow = tflow.tflow()
        flow.request.path = "/s/gear"
        old, new = b"0000000000001old", b"0000000000002new"
        with patch("ripple.proxy_addon.garage_wire.response", side_effect=[old, new]), \
                patch("ripple.proxy_addon.asyncio.sleep", new_callable=AsyncMock) as sleep:
            await addon.requestheaders(flow)
        sleep.assert_awaited_once_with(0.2)
        self.assertEqual(addon.gear_revision, new[:13])


class GaragePushTests(unittest.TestCase):
    def test_live_refresh_uses_native_reward_match_guid_and_valid_signature(self):
        addon = RippleAddon()
        flow = SimpleNamespace(id="game-socket")
        with patch("ripple.proxy_addon.ctx.master", create=True) as master, \
                patch("builtins.open", unittest.mock.mock_open()):
            addon.refresh_garage(flow, b"0000000000001")
            addon.refresh_garage(flow, b"0000000000001")
            self.assertEqual(master.commands.call.call_count, 1)
            args = master.commands.call.call_args.args
            self.assertEqual(args[:3], ("inject.websocket", flow, True))
            header, body = args[3].split(b"\r\n\r\n", 1)
            headers = dict(line.split(b": ", 1) for line in header.split(b"\r\n"))
            self.assertEqual(headers[b"PsyService"], b"Reward/RewardResult")
            self.assertEqual(headers[b"PsyServiceVersion"], b"2")
            payload = json.loads(body)
            self.assertEqual(payload["MatchGUID"], "S.Gear:go")
            self.assertEqual(payload["ProductData"], [])
            expected = base64.b64encode(hmac.digest(RESPONSE_KEY, headers[b"PsyTime"] + b"-" + body, "sha256"))
            self.assertEqual(headers[b"PsySig"], expected)
            addon.refresh_garage(flow, b"0000000000002")
            self.assertEqual(master.commands.call.call_count, 2)


if __name__ == "__main__":
    unittest.main()
