import os, json, time, threading, copy

APP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(APP_DIR, "data")
STATE_DIR = os.path.join(os.environ.get("APPDATA", APP_DIR), "RippleRL")
PACKS_DIR = os.path.join(STATE_DIR, "packs")
WORKSHOP_DIR = os.path.join(STATE_DIR, "workshop")
BACKUP_DIR = os.path.join(STATE_DIR, "backups")
CACHE_DIR = os.path.join(os.environ.get("LOCALAPPDATA", STATE_DIR), "RippleRL", "cache")

for d in (STATE_DIR, PACKS_DIR, WORKSHOP_DIR, BACKUP_DIR, CACHE_DIR):
    os.makedirs(d, exist_ok=True)
for kind in ("ball", "car", "decal", "hud"):
    os.makedirs(os.path.join(PACKS_DIR, kind), exist_ok=True)

_lock = threading.RLock()

# In-memory cache of parsed JSON state files, keyed by filename.
# Each entry is (mtime_ns, size, parsed_object). The cache is validated against
# the file's stat() on every read, so an external edit is still picked up, but a
# hot read (settings, feature state) avoids re-opening and re-parsing from disk
# on every single bridge call.
_json_cache = {}


def _file(name):
    return os.path.join(STATE_DIR, name)


def read_json(name, default):
    path = _file(name)
    try:
        st = os.stat(path)
    except OSError:
        return default
    key = (st.st_mtime_ns, st.st_size)
    with _lock:
        hit = _json_cache.get(name)
        if hit is not None and hit[0] == key:
            return copy.deepcopy(hit[1])
    try:
        with open(path, encoding="utf-8") as f:
            parsed = json.load(f)
    except Exception:
        return default
    with _lock:
        _json_cache[name] = (key, parsed)
    return copy.deepcopy(parsed)


def write_json(name, obj, indent=1):
    with _lock:
        tmp = _file(name + ".tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(obj, f, indent=indent, ensure_ascii=False)
        os.replace(tmp, _file(name))
        # Refresh the cache in-place so the next read is served from memory
        # without another disk round-trip (and with the new mtime/size).
        try:
            st = os.stat(_file(name))
            _json_cache[name] = ((st.st_mtime_ns, st.st_size), copy.deepcopy(obj))
        except OSError:
            _json_cache.pop(name, None)


DEFAULT_SETTINGS = {
    "language": "en", "theme": "dark", "firstRunComplete": True,
    "library": {}, "target": {}, "bringToFrontHotkey": "F2",
    "alwaysOnTop": False, "closeToTray": True, "discordRpc": True,
    "startup": False, "backup": True, "proxyEnabled": True,
}


def settings():
    s = read_json("settings.json", {})
    out = dict(DEFAULT_SETTINGS)
    out.update(s)
    return out


def save_settings(s):
    cur = settings()
    cur.update(s)
    write_json("settings.json", cur)


def garage_patch_dir():
    """Directory holding patch-manifest.json and the `after/` patched packages."""
    captures = os.path.join(BACKUP_DIR, "ground-truth")
    verified = os.path.join(captures, "verified-epic")
    target = settings().get("target")
    if isinstance(target, dict) and target.get("source") == "Epic" and os.path.isfile(
        os.path.join(verified, "patch-manifest.json")
    ):
        return verified
    return captures


def garage_backup_dir():
    """Where pre-patch (vanilla) garage UPKs are backed up for revert."""
    location = "garage-verified-epic" if os.path.basename(garage_patch_dir()) == "verified-epic" else "garage"
    return os.path.join(BACKUP_DIR, location)


def cooked_dir():
    s = settings()
    t = s.get("target") or {}
    if t.get("cookedDir") and os.path.isdir(t["cookedDir"]):
        return t["cookedDir"]
    return None


def iso_now():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
