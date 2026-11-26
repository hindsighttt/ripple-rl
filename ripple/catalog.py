import os, json, threading
from . import store

_lock = threading.RLock()
_cat = None
_items = None
_inv = None
_rarity_map = None
_prepared = None

CATEGORIES = [
    ("Bodies", "Bodies"), ("Boosts", "Boosts"), ("Wheels", "Wheels"),
    ("Decals", "Decals"), ("Toppers", "Toppers"), ("Antennas", "Antennas"),
    ("Goal Explosions", "GoalExplosions"), ("Engine Sounds", "EngineSounds"),
    ("Anthems", "Anthems"), ("Player Banners", "PlayerBanners"),
    ("Avatar Borders", "AvatarBorders"), ("Paint Finishes", "PaintFinishes"),
    ("Trails", "Trails"),
]


def load():
    global _cat, _items, _inv
    with _lock:
        if _cat is None:
            _cat = json.load(open(os.path.join(store.DATA_DIR, "catalog.json"), encoding="utf-8"))
        if _items is None:
            _items = json.load(open(os.path.join(store.DATA_DIR, "items.json"), encoding="utf-8"))
        if _inv is None:
            _inv = store.read_json("inventory.json", {})
            if not _inv:
                try:
                    _inv = json.load(open(os.path.join(store.DATA_DIR, "inventory.json"), encoding="utf-8"))
                except Exception:
                    _inv = {}
        return _cat, _items, _inv


def catalog_items():
    return load()[0].get("items", [])


def by_key():
    out = {}
    for it in catalog_items():
        out[it.get("asset_path", "").lower()] = it
        out[it.get("code", "").lower()] = it
    return out


def inv_ids():
    ids = set(load()[2].get("ids", []))
    return ids


def inv_count():
    return len(inv_ids())


def categories():
    counts = {}
    for it in catalog_items():
        counts[it.get("category")] = counts.get(it.get("category"), 0) + 1
    out = []
    for label, slug in CATEGORIES:
        if counts.get(slug):
            out.append({"label": label, "slug": slug, "count": counts[slug]})
    return {"categories": out, "total": sum(counts.values())}


def titles():
    with open(os.path.join(store.DATA_DIR, "titles.json"), encoding="utf-8") as f:
        title_rows = json.load(f)
    with open(os.path.join(store.DATA_DIR, "categories.json"), encoding="utf-8") as f:
        title_categories = json.load(f)
    return {"titles": title_rows, "categories": title_categories}


RARITY_ORDER = {"common": 0, "uncommon": 1, "rare": 2, "veryrare": 3, "import": 4,
                "exotic": 5, "blackmarket": 6, "limited": 7, "legacy": 8}


def _rarity():
    # Rarity map built once from items.json (large) and memoized.
    global _rarity_map
    with _lock:
        if _rarity_map is None:
            out = {}
            for it in load()[1]:
                out[it.get("ID")] = (it.get("Quality") or "Common").lower()
            _rarity_map = out
        return _rarity_map


def _prepared_items():
    # Project the catalog into the shape list_items returns, attach rarity and
    # pre-lowercased search fields, and sort by (rarity, label) ONCE. Each
    # list_items call then only filters and slices this list instead of
    # rebuilding the rarity map, re-projecting and re-sorting the whole catalog.
    global _prepared
    with _lock:
        if _prepared is None:
            rar = _rarity()
            rows = []
            for it in catalog_items():
                iid = it.get("id")
                r = rar.get(iid, "common")
                rows.append({
                    "category": it.get("category"), "code": it.get("code"), "itemId": iid,
                    "key": it.get("asset_path"), "label": it.get("label"), "rarity": r,
                    "_ll": (it.get("label") or "").lower(), "_lc": (it.get("code") or "").lower(),
                })
            rows.sort(key=lambda x: (RARITY_ORDER.get(x["rarity"], 9), x["_ll"]))
            _prepared = rows
        return _prepared


def list_items(p):
    load()
    rows = _prepared_items()
    cat = p.get("category") or ""
    rq = p.get("rarity") or ""
    q = (p.get("query") or "").lower()
    owned = inv_ids()
    only = bool(p.get("ownedOnly"))
    not_owned = bool(p.get("notOwnedOnly"))
    res = []
    for row in rows:
        if cat and row["category"] != cat:
            continue
        if rq and row["rarity"] != rq:
            continue
        if q and q not in row["_ll"] and q not in row["_lc"]:
            continue
        iid = row["itemId"]
        if only and iid not in owned:
            continue
        if not_owned and iid in owned:
            continue
        res.append({
            "category": row["category"], "code": row["code"], "itemId": iid,
            "key": row["key"], "label": row["label"], "rarity": row["rarity"],
        })
    off = int(p.get("offset") or 0)
    lim = int(p.get("limit") or 100)
    return {
        "items": res[off:off + lim], "total": len(res), "offset": off,
        "inventoryKnown": True, "inventoryCount": inv_count(),
    }
