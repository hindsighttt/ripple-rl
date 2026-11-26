"""Register custom titles with the Rocket League client.

Rocket League resolves a player title through the ``PlayerTitleConfig`` block of
the cached PsyNet configuration: ``Titles`` maps an ID to its text and color
category, and ``Categories`` maps that category to a color and glow color.

This module writes the enabled custom titles into that configuration (both the
cached copy and, when the client fetches a fresh one, the HTTP response) using
the same shape the reference app used:

    title    {"ID":"<titleId>","Category":"ripple_custom_<titleId>","Text":"..."}
    category {"ID":"ripple_custom_<titleId>","Color":"RRGGBB","GlowColor":"RRGGBB"}

The cache file is an Unreal ``ArrayProperty`` envelope: a short header, the JSON
body, then a property list (``DataCRC``, ``ETag``, ``ContentType``,
``Signature``). Only the body is replaced, so the remaining server metadata is
preserved verbatim.
"""

import json
import os
import re
import struct

from . import features

DATA_PROPERTY = b"ArrayProperty\x00"
PROPERTY_START = b"\x08\x00\x00\x00DataCRC\x00"
CATEGORY_PREFIX = "ripple_custom_"
DEFAULT_COLOR = "FFFFFF"
_HEX6 = re.compile(r"^[0-9A-Fa-f]{6}$")


def webcache_dir():
    return os.path.join(os.environ.get("USERPROFILE", ""), "Documents", "My Games",
                        "Rocket League", "TAGame", "Cache", "WebCache")


def _color(value, fallback=""):
    value = (value or "").strip().lstrip("#")
    if not value:
        return fallback
    return value.upper() if _HEX6.match(value) else fallback


def custom_titles():
    """Enabled, saved custom titles in registration order."""
    state = features.get("titles")
    if not state.get("enabled"):
        return []
    out = []
    for row in state.get("titles", []):
        if not row.get("custom"):
            continue
        title_id = (row.get("id") or "").strip()
        text = (row.get("text") or "").strip()
        if not title_id or not text:
            continue
        out.append({
            "id": title_id,
            "category": CATEGORY_PREFIX + title_id,
            "text": text,
            "color": _color(row.get("color"), DEFAULT_COLOR),
            "glow": _color(row.get("glow")),
        })
    return out


def title_record(row):
    return {"ID": row["id"], "Category": row["category"], "Text": row["text"]}


def category_record(row):
    return {"ID": row["category"], "Color": row["color"], "GlowColor": row["glow"]}


def _is_ours_category(value):
    return isinstance(value, str) and value.startswith(CATEGORY_PREFIX)


def inject(config):
    """Add the enabled custom titles to a parsed PlayerTitleConfig.

    Returns True when the configuration changed. Existing custom records are
    replaced in place so repeated calls are idempotent.
    """
    block = config.get("PlayerTitleConfig")
    if not isinstance(block, dict):
        return False
    titles = block.setdefault("Titles", [])
    categories = block.setdefault("Categories", [])
    if not isinstance(titles, list) or not isinstance(categories, list):
        return False

    wanted = custom_titles()
    wanted_ids = {row["id"] for row in wanted}

    before_titles = json.dumps(titles, sort_keys=False, ensure_ascii=False)
    before_categories = json.dumps(categories, sort_keys=False, ensure_ascii=False)

    titles[:] = [t for t in titles
                 if not (isinstance(t, dict) and _is_ours_category(t.get("Category")))]
    categories[:] = [c for c in categories
                     if not (isinstance(c, dict) and _is_ours_category(c.get("ID")))]
    for row in wanted:
        if row["id"] in wanted_ids:
            titles.append(title_record(row))
            categories.append(category_record(row))

    return (json.dumps(titles, sort_keys=False, ensure_ascii=False) != before_titles or
            json.dumps(categories, sort_keys=False, ensure_ascii=False) != before_categories)


def inject_text(body):
    """Inject into a UTF-8 JSON configuration body; returns the new bytes."""
    try:
        config = json.loads(body)
    except (ValueError, UnicodeDecodeError):
        return body
    if not isinstance(config, dict):
        return body
    if not inject(config):
        return body
    return json.dumps(config, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def _split_cache(data):
    """Return (prefix, body, properties) for a serialized cache entry."""
    marker = data.find(DATA_PROPERTY)
    if marker < 0:
        raise ValueError("no Data array property")
    cursor = marker + len(DATA_PROPERTY)
    if cursor + 12 > len(data):
        raise ValueError("truncated array header")
    body_length = struct.unpack_from("<I", data, cursor + 8)[0]
    body_start = cursor + 12
    body_end = body_start + body_length
    if body_end > len(data) or data[body_start:body_start + 1] != b"{":
        raise ValueError("bad cache body")
    properties = data[body_end:]
    if not properties.startswith(PROPERTY_START):
        raise ValueError("missing cache properties")
    return data[:body_start], data[body_start:body_end], properties


def rebuild_cache(data, body):
    """Reparcel a cache entry with a new JSON body.

    The header carries the total size twice and the array header carries the
    body size; all other bytes (including the property list and its DataCRC and
    Signature) are preserved.
    """
    prefix, _old_body, properties = _split_cache(data)
    cursor = prefix.find(DATA_PROPERTY) + len(DATA_PROPERTY)
    head = bytearray(prefix)
    total = len(head) + len(body) + len(properties)
    struct.pack_into("<I", head, 0, total - 8)
    struct.pack_into("<I", head, 8, total - 4)
    struct.pack_into("<I", head, cursor, len(body) + 4)
    struct.pack_into("<I", head, cursor + 8, len(body))
    return bytes(head) + body + properties


def _write(path, data):
    tmp = path + ".tmp"
    with open(tmp, "wb") as stream:
        stream.write(data)
    os.replace(tmp, path)


def refresh_file(path):
    """Inject into one cache file. Returns True when it was rewritten."""
    try:
        with open(path, "rb") as stream:
            data = stream.read()
    except OSError:
        return False
    try:
        _prefix, body, _properties = _split_cache(data)
    except ValueError:
        return False
    if b'"PlayerTitleConfig"' not in body:
        return False
    updated = inject_text(body)
    if updated == body:
        return False
    backup = path + ".ripplebak"
    if not os.path.exists(backup):
        try:
            with open(backup, "wb") as stream:
                stream.write(data)
        except OSError:
            pass
    try:
        _write(path, rebuild_cache(data, updated))
    except (OSError, ValueError):
        return False
    return True


def refresh_webcache(directory=None):
    """Inject into every cached configuration that carries PlayerTitleConfig."""
    directory = directory or webcache_dir()
    try:
        names = os.listdir(directory)
    except OSError:
        return 0
    changed = 0
    for name in names:
        if name.endswith((".ripplebak", ".tmp", ".last_purge_WebCache FileCache")):
            continue
        if refresh_file(os.path.join(directory, name)):
            changed += 1
    return changed


def restore_webcache(directory=None):
    """Put the pre-injection cache files back from their .ripplebak copies."""
    directory = directory or webcache_dir()
    try:
        names = os.listdir(directory)
    except OSError:
        return
    for name in names:
        if not name.endswith(".ripplebak"):
            continue
        backup = os.path.join(directory, name)
        target = os.path.join(directory, name[:-len(".ripplebak")])
        try:
            with open(backup, "rb") as stream:
                data = stream.read()
            _write(target, data)
            os.remove(backup)
        except OSError:
            pass
