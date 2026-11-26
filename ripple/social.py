import time, secrets, random
from . import features, titles_config

TITLE_FILE = "titles"


def now_ms():
    return str(int(time.time() * 1000))


def name_response(pseudo):
    return now_ms() + (pseudo or "")


def ping_value():
    p = features.get("ping")
    if not p.get("enabled"):
        return now_ms()
    if p.get("random"):
        return str(random.randint(int(p.get("min") or 0), int(p.get("max") or 100)))
    return str(int(p.get("value") or 0))


def save_title(p):
    f = features.get("titles")
    tid = p.get("titleId") or ("CustomTitle_" + secrets.token_hex(6))
    text = (p.get("text") or "").strip()
    if not text:
        raise ValueError("title text required")
    rows = [t for t in f.get("titles", []) if t.get("id") != tid]
    rows.append({"id": tid, "text": text, "color": p.get("color", ""), "glow": p.get("glow", ""),
                 "custom": True})
    features.set_state("titles", {"titles": rows})
    try:
        titles_config.refresh_webcache()
    except Exception:
        pass
    return {"ok": True, "id": tid}


# Real /s/title wire (captured from the reference app in a live match, 2026-11-24):
#   "<TitleID>|B1:<base64 blob>"
# The injected in-game plugin polls /s/title and applies <TitleID> to the local PRI so
# the scoreboard/leaderboard shows it (client-side). The blob is CONSTANT across titles
# (verified against two different titles) — only the TitleID prefix changes. The blob
# decodes to [ver=01][slot=0x15=21 Player Title][count=1][u32 117460][16-byte zero IID].
# The older 'SPacket1:{...}' JSON was only a static template string, NOT the wire format.
TITLE_BLOB = "B1:ARUBAAAA1MoBAAAAAAAAAAAAAAAAAAAAAAA="


def active_title_id():
    """The title to display on the PRI/leaderboard: an explicit custom-title selection
    if the titles feature has one, otherwise the most-recently spawned title."""
    f = features.get("titles")
    if f.get("enabled"):
        own = [t for t in f.get("titles", []) if t.get("custom") and t.get("id")]
        if own:
            return own[-1]["id"]
    from . import spawn
    spawned = [x.get("titleId") for x in spawn._load().get("items", []) if x.get("titleId")]
    if spawned:
        return spawned[-1]
    return ""


def title_response(account_id=""):
    tid = active_title_id()
    if not tid:
        return ""
    return "%s|%s" % (tid, TITLE_BLOB)


def title_wire():
    return title_response()
