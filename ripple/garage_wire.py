"""Garage and drop-opening fields in the reference build 1.8.4's fixed-size /s/geam payload.

Offsets and formatting are checked against distinct-colour native captures. The
remaining protocol sections use a neutral captured template; they are not tilde
padding and must retain their positions. Flip-reset colour is a package change,
not a field in this HTTP payload.
"""

import hashlib
import json
import math
import time
from pathlib import Path

from . import features, store

WIRE_SIZE = 9867
ROULETTE_OFFSET = 714  # S_State.Gear reads one digit for S.Drops.v1.roulette.
LIGHTS = ("underglow", "headlights", "taillights")
PAINTS = ("primary", "secondary", "trim", "wheels", "boost")
_template = None
_last_key = None
_last_revision = 0
_last_payload = None


def _number(value, default, minimum, maximum):
    try:
        value = float(value)
        if not math.isfinite(value):
            return default
        return max(minimum, min(maximum, value))
    except (TypeError, ValueError):
        return default


def _integer(value, width):
    encoded = ("%0*d" % (width, value)).encode("ascii")
    if len(encoded) != width:
        raise ValueError("Garage value does not fit its wire field")
    return encoded


def _bit(value):
    return b"1" if value else b"0"


def _team(state, name, team):
    rows = state.get(name, [])
    return rows[team] if isinstance(rows, list) and team < len(rows) and isinstance(rows[team], dict) else {}


def _rgb(value):
    try:
        if isinstance(value, str):
            value = int(value.removeprefix("#"), 16)
        value = int(value)
        if 0 <= value <= 0xFFFFFF:
            return _integer(value, 8)
    except (ValueError, TypeError):
        pass
    return b"16777215"


def _rainbow(garage, paint, name, team):
    flags = paint.get(name + "Rainbow", [])
    enabled = isinstance(flags, list) and team < len(flags) and flags[team]
    return _bit(_team(garage, name, team).get("on") and enabled)


def template_bytes():
    global _template
    if _template is None:
        _template = (Path(store.DATA_DIR) / "garage-wire-template.bin").read_bytes()
        if len(_template) != WIRE_SIZE:
            raise ValueError("Invalid garage wire template size")
    return _template


def serialize(garage, paint, *, revision, config_revision, roulette_on=None, template=None):
    """Serialize configured controls; revisions are explicit runtime metadata.

    Supplying captured revisions permits byte-exact offline replay. Other feature
    sections are preserved from template, with no out-of-scope state overrides.
    """
    payload = bytearray(template_bytes() if template is None else template)
    if len(payload) != WIRE_SIZE:
        raise ValueError("Expected a 9867-byte garage protocol template")
    payload[:13] = _integer(revision, 13)
    payload[9841:9854] = _integer(config_revision, 13)
    cursor = 13
    for team in range(2):
        for name in LIGHTS:
            row = _team(garage, name, team)
            level = int(_number(row.get("level"), 10, 1, 20))
            record = _bit(row.get("on")) + _rgb(row.get("color", "#ffffff")) + _integer(level - 10, 3)
            payload[cursor:cursor + 12] = record
            cursor += 12
    camber = garage.get("camber", {})
    if not isinstance(camber, dict):
        camber = {}
    for name, width, limit in (("deg", 4, 22.5), ("ud", 3, 5), ("lr", 3, 5)):
        value = _number(camber.get(name), 0, -limit, limit) * 10
        value = math.floor(value + 0.5) if value >= 0 else math.ceil(value - 0.5)
        payload[cursor:cursor + width] = _integer(value, width)
        cursor += width
    for team, start in ((0, 96), (1, 147)):
        cursor = start
        for name in PAINTS:
            row = _team(paint, name, team)
            record = _bit(row.get("on")) + _rgb(row.get("color", "#ffffff")) + _bit(row.get("on") and row.get("rainbow"))
            payload[cursor:cursor + 10] = record
            cursor += 10
        payload[cursor:cursor + 1] = _rainbow(garage, paint, "headlights", team)
    payload[198:201] = _integer(int(_number(paint.get("rainbowSpeed"), 20, 1, 100)), 3)
    for name, offsets in (("underglow", (236, 237)), ("taillights", (240, 241))):
        for team, offset in enumerate(offsets):
            payload[offset:offset + 1] = _rainbow(garage, paint, name, team)
    if roulette_on is not None:
        payload[ROULETTE_OFFSET:ROULETTE_OFFSET + 1] = _bit(roulette_on)
    if len(payload) != WIRE_SIZE:
        raise ValueError("Garage serialization changed fixed protocol offsets")
    return bytes(payload)


def response():
    """Cache a payload until the game configuration changes."""
    global _last_key, _last_revision, _last_payload
    garage, paint = features.get("garage"), features.get("paint")
    roulette = store.read_json("feature_roulette.json", {})
    roulette_on = not (isinstance(roulette, dict) and roulette.get("on") is False)
    key = hashlib.sha256(json.dumps([garage, paint, roulette_on], sort_keys=True, separators=(",", ":")).encode()).digest()
    if key != _last_key:
        stamp = max(int(time.time() * 1000), _last_revision + 1)
        _last_payload = serialize(garage, paint, revision=stamp * 2 + 1, config_revision=stamp,
                                  roulette_on=roulette_on)
        _last_revision, _last_key = stamp, key
    return _last_payload
