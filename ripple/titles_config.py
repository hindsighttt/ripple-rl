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
import socket
import ssl
import struct
import time
import urllib.request

from . import features, store
from .webcache import header_crc, pack_entry, split_entry, webcache_dir

CATEGORY_PREFIX = "shift_custom_"
DEFAULT_COLOR = "FFFFFF"
_HEX6 = re.compile(r"^[0-9A-Fa-f]{6}$")

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
    return isinstance(value, str) and value.startswith((CATEGORY_PREFIX, "ripple_custom_"))


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
    prefix, body, properties = split_entry(data)
    if not body.startswith(b"{"):
        raise ValueError("bad cache body")
    return prefix, body, properties


def _read_u32(buf, p):
    return struct.unpack_from("<I", buf, p)[0], p + 4


def _read_cstr(buf, p):
    n, p = _read_u32(buf, p)
    if n == 0 or p + n > len(buf):
        raise ValueError("bad string")
    return buf[p:p + n - 1], p + n


def parse_props(props):
    """Parse the property block into (records, tail).

    A record keeps its type and the two unknown u32 fields verbatim; only the
    value of StrProperty records is rewritten. ``tail`` is the bytes from the
    ``None`` sentinel onward.
    """
    records = []
    p = 0
    while p < len(props):
        key, p = _read_cstr(props, p)
        if key == b"None":
            return records, props[p:]
        typ, p = _read_cstr(props, p)
        a, p = _read_u32(props, p)
        c, p = _read_u32(props, p)
        if typ == b"IntProperty":
            value, p = _read_u32(props, p)
        else:
            value, p = _read_cstr(props, p)
        records.append({"key": key, "type": typ, "a": a, "c": c, "value": value})
    raise ValueError("missing property sentinel")


def encode_props(records, tail):
    out = bytearray()
    for rec in records:
        out += struct.pack("<I", len(rec["key"]) + 1) + rec["key"] + b"\x00"
        out += struct.pack("<I", len(rec["type"]) + 1) + rec["type"] + b"\x00"
        out += struct.pack("<I", rec["a"]) + struct.pack("<I", rec["c"])
        if rec["type"] == b"IntProperty":
            out += struct.pack("<I", rec["value"])
        else:
            out += struct.pack("<I", len(rec["value"]) + 1) + rec["value"] + b"\x00"
    out += struct.pack("<I", 5) + b"None\x00" + tail
    return bytes(out)


def set_prop(records, key, value):
    for rec in records:
        if rec["key"] == key:
            rec["value"] = value
            return
    records.append({"key": key, "type": b"StrProperty", "a": 12, "c": 0, "value": value})


# ---- live config headers -------------------------------------------------
# RL's conditional GET only returns 304 when the cached ETag matches the
# server's current content revision. If it returns 200 the pristine body
# replaces the injected one in memory. We therefore pin the live ETag (and its
# signature) into the entry so the client keeps the patched body.

_live_cache = {"at": 0.0, "path": "", "etag": "", "signature": ""}
_BUILD_SECRET = re.compile(rb"buildSecret=([0-9a-fA-F]{46,48})")
_CONFIG_PATH = re.compile(rb"(/v2/Config/BattleCars/[ -~]*?buildSecret=[0-9a-fA-F]{46,48})")


def _rl_pids():
    import ctypes
    import ctypes.wintypes as wt

    class Entry(ctypes.Structure):
        _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                    ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
                    ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD),
                    ("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", ctypes.c_long),
                    ("dwFlags", wt.DWORD), ("szExeFile", wt.WCHAR * 260)]

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    snap = k32.CreateToolhelp32Snapshot(2, 0)
    if snap == ctypes.c_void_p(-1).value:
        return []
    entry = Entry()
    entry.dwSize = ctypes.sizeof(entry)
    pids = []
    try:
        if k32.Process32FirstW(snap, ctypes.byref(entry)):
            while True:
                if entry.szExeFile.lower() == "rocketleague.exe":
                    pids.append(entry.th32ProcessID)
                if not k32.Process32NextW(snap, ctypes.byref(entry)):
                    break
    finally:
        k32.CloseHandle(snap)
    return pids


def _config_path_from_memory(pid=None):
    """Find the live config URL path (with buildSecret) in the client."""
    import ctypes
    import ctypes.wintypes as wt

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)

    class MBI(ctypes.Structure):
        _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                    ("AllocationProtect", wt.DWORD), ("RegionSize", ctypes.c_size_t),
                    ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]

    for target in ([pid] if pid else _rl_pids()):
        h = k32.OpenProcess(0x10 | 0x400, False, target)
        if not h:
            continue
        si = ctypes.create_string_buffer(64)
        k32.GetSystemInfo(si)
        addr = ctypes.c_void_p.from_buffer(si, 8).value
        top = ctypes.c_void_p.from_buffer(si, 16).value
        try:
            while addr < top:
                mbi = MBI()
                if not k32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
                    break
                if (mbi.State == 0x1000 and mbi.RegionSize and
                        not (mbi.Protect & 0x101) and (mbi.Protect & 0xFF)):
                    buf = ctypes.create_string_buffer(mbi.RegionSize)
                    got = ctypes.c_size_t(0)
                    if k32.ReadProcessMemory(h, ctypes.c_void_p(mbi.BaseAddress), buf,
                                             mbi.RegionSize, ctypes.byref(got)):
                        match = _CONFIG_PATH.search(buf.raw[:got.value])
                        if match:
                            return match.group(1).decode("ascii", "replace")
                addr = mbi.BaseAddress + mbi.RegionSize
        finally:
            k32.CloseHandle(h)
    return ""


def _saved_path():
    try:
        return store.read_json("title-config-url.json", {}).get("path", "")
    except Exception:
        return ""


def _save_path(path):
    try:
        store.write_json("title-config-url.json", {"path": path})
    except Exception:
        pass


def _path_from_cache_names():
    import base64
    directory = webcache_dir()
    try:
        names = os.listdir(directory)
    except OSError:
        return ""
    for name in names:
        if name.endswith((".ripplebak", ".tmp")):
            continue
        try:
            decoded = base64.b64decode(name + "=" * (-len(name) % 4)).decode("ascii")
        except Exception:
            continue
        if "/Config/BattleCars/" in decoded and "buildSecret" not in decoded:
            return decoded if decoded.startswith("/") else "/" + decoded
    return ""


_DUMP_PATHS = [
    os.path.join(os.environ.get("APPDATA", ""), "RippleRL", "research", "reference-runtime.bin"),
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "shift_mem.bin"),
]


def _path_from_dump():
    for dump in _DUMP_PATHS:
        if not os.path.isfile(dump):
            continue
        try:
            with open(dump, "rb") as stream:
                while True:
                    chunk = stream.read(32 * 1024 * 1024)
                    if not chunk:
                        break
                    match = _CONFIG_PATH.search(chunk)
                    if match:
                        path = match.group(1).decode("ascii", "replace")
                        _save_path(path)
                        return path
        except OSError:
            continue
    return ""


_mem_fail_at = 0.0


def _config_path():
    global _mem_fail_at
    now = time.monotonic()
    path = ""
    if now - _mem_fail_at > 60:
        path = _config_path_from_memory()
        if path:
            _save_path(path)
            return path
        _mem_fail_at = now
    path = _saved_path()
    if path:
        return path
    path = _path_from_dump()
    if path:
        return path
    base = _path_from_cache_names()
    if base:
        return base
    return ""


def live_headers(max_age=20.0):
    """Current (path, ETag, Psysignature) for the config; cached briefly."""
    now = time.monotonic()
    if _live_cache["at"] and now - _live_cache["at"] < max_age:
        return _live_cache
    path = _live_cache["path"] or _config_path()
    if not path:
        return None
    url = "https://config.psynet.gg" + path
    try:
        ctx = ssl.create_default_context()
        req = urllib.request.Request(url, headers={"User-Agent": "RippleRL"})
        with urllib.request.urlopen(req, timeout=8, context=ctx) as resp:
            etag = resp.headers.get("ETag", "")
            signature = resp.headers.get("Psysignature", "")
    except Exception:
        return _live_cache if _live_cache["etag"] else None
    if etag:
        _live_cache.update(at=now, path=path, etag=etag, signature=signature)
    return _live_cache if _live_cache["etag"] else None


def pin_live_headers(props):
    """Rewrite the ETag/Signature properties to the server's current values."""
    live = live_headers()
    if not live:
        return props
    try:
        records, tail = parse_props(props)
    except ValueError:
        return props
    set_prop(records, b"ETag", live["etag"].encode())
    if live["signature"]:
        set_prop(records, b"Signature", live["signature"].encode())
    return encode_props(records, tail)


_host_patched_pid = 0
_HOST = b"config.psynet.gg"
_HOST_ALT = b"config.localhost"


def patch_config_host():
    """Point the live client's config host at the local listener.

    The 16-byte same-length replacement preserves the following path (Windows
    resolves ``*.localhost`` to loopback). Returns True once patched.
    """
    import ctypes
    import ctypes.wintypes as wt

    global _host_patched_pid
    pids = _rl_pids()
    if not pids:
        return False
    pid = pids[0]
    if pid == _host_patched_pid:
        return True

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)

    class MBI(ctypes.Structure):
        _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                    ("AllocationProtect", wt.DWORD), ("RegionSize", ctypes.c_size_t),
                    ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]

    h = k32.OpenProcess(0x10 | 0x20 | 0x8 | 0x400, False, pid)
    if not h:
        return False
    patched = 0
    si = ctypes.create_string_buffer(64)
    k32.GetSystemInfo(si)
    addr = ctypes.c_void_p.from_buffer(si, 8).value
    top = ctypes.c_void_p.from_buffer(si, 16).value
    try:
        while addr < top:
            mbi = MBI()
            if not k32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
                break
            if (mbi.State == 0x1000 and mbi.RegionSize and
                    not (mbi.Protect & 0x101) and (mbi.Protect & 0xFF)):
                buf = ctypes.create_string_buffer(mbi.RegionSize)
                got = ctypes.c_size_t(0)
                if k32.ReadProcessMemory(h, ctypes.c_void_p(mbi.BaseAddress), buf,
                                         mbi.RegionSize, ctypes.byref(got)):
                    data = buf.raw[:got.value]
                    for needle, repl in ((_HOST, _HOST_ALT),
                                         (_HOST.decode().encode("utf-16-le"),
                                          _HOST_ALT.decode().encode("utf-16-le"))):
                        start = 0
                        while True:
                            i = data.find(needle, start)
                            if i < 0:
                                break
                            if k32.WriteProcessMemory(h, ctypes.c_void_p(mbi.BaseAddress + i),
                                                      repl, len(repl), None):
                                patched += 1
                            start = i + len(needle)
            addr = mbi.BaseAddress + mbi.RegionSize
    finally:
        k32.CloseHandle(h)
    if patched:
        _host_patched_pid = pid
        _trace_marker("TITLE_CONFIG_HOST_PATCHED %d" % patched)
    return bool(patched)


def _trace_marker(text):
    try:
        with open(os.path.join(store.STATE_DIR, "proxy_trace.log"), "a", encoding="utf-8") as stream:
            stream.write(text + "\n")
    except Exception:
        pass


def rebuild_cache(data, body):
    """Reparcel a cache entry with a new JSON body.

    The header carries the total size twice, the array header carries the body
    size, and the client checks the header CRC. The property block is kept and
    the live ETag/Signature are pinned where the server is reachable.
    """
    prefix, _old_body, properties = _split_cache(data)
    properties = pin_live_headers(properties)
    return pack_entry(prefix, body, properties)


def _write(path, data):
    tmp = path + ".tmp"
    with open(tmp, "wb") as stream:
        stream.write(data)
    os.replace(tmp, path)


def cache_stamp(path):
    """(mtime_ns, size) used to detect client rewrites of the cache."""
    try:
        st = os.stat(path)
    except OSError:
        return None
    return (st.st_mtime_ns, st.st_size)


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


def cache_seen(seen, directory=None):
    """Seed a watch_webcache() map with the current stamps (no injection)."""
    directory = directory or webcache_dir()
    try:
        names = os.listdir(directory)
    except OSError:
        return
    for name in names:
        if name.endswith((".ripplebak", ".tmp", ".last_purge_WebCache FileCache")):
            continue
        path = os.path.join(directory, name)
        stamp = cache_stamp(path)
        if stamp is not None:
            seen[path] = stamp


def _has_wanted(path, ids):
    try:
        with open(path, "rb") as stream:
            data = stream.read()
    except OSError:
        return True
    return all(i.encode() in data for i in ids)


def watch_webcache(seen, directory=None):
    """Re-inject when the client rewrote a cached config.

    Rocket League re-exports the config after every refresh cycle (a 304
    re-export keeps the body, a real 200 fetch replaces it with the pristine
    body). The reference app's ``refreshConfigs`` watcher re-injects into the
    freshly written file; ``seen`` maps path -> (mtime_ns, size) and is updated
    in place. Returns the number of files rewritten.

    File timestamps on Windows can be coarse, so a missing title record is also
    checked directly rather than relying on the stamp alone.
    """
    directory = directory or webcache_dir()
    wanted = [row["id"] for row in custom_titles()]
    try:
        names = os.listdir(directory)
    except OSError:
        return 0
    changed = 0
    current = set()
    for name in names:
        if name.endswith((".ripplebak", ".tmp", ".last_purge_WebCache FileCache")):
            continue
        path = os.path.join(directory, name)
        stamp = cache_stamp(path)
        if stamp is None:
            continue
        current.add(path)
        if seen.get(path) == stamp and _has_wanted(path, wanted):
            continue
        seen[path] = stamp
        if refresh_file(path):
            changed += 1
        after = cache_stamp(path)
        if after is not None:
            seen[path] = after
    for path in list(seen):
        if path not in current:
            del seen[path]
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
