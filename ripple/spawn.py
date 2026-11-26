import os, json, secrets, time
from . import store

FILE = "spawn.json"

# RL represents every player title as the one generic "Title" product (3036) with the
# specific title carried in a "TitleID" attribute. Confirmed from a live GetPlayerProducts
# capture (ProductID 3036, Attributes [{"Key":"TitleID","Value":"BackupPlan"}]).
TITLE_PID = 3036


def _load():
    d = store.read_json(FILE, {"items": []})
    if d.get("items") is None:
        d["items"] = []
    return d


def _save(d):
    store.write_json(FILE, d)


def list_spawn():
    d = _load()
    inv = _inventory()
    out = []
    for it in d["items"]:
        row = dict(it)
        row["instanceId"] = _instance_index(it, d)
        out.append(row)
    return {"items": out}


_ITEM_DB = None
_PAINTABLE = None


def _items():
    global _ITEM_DB
    if _ITEM_DB is None:
        with open(os.path.join(store.DATA_DIR, "items.json"), encoding="utf-8") as stream:
            _ITEM_DB = json.load(stream)
    return _ITEM_DB


_INV = None


def _inventory():
    global _INV
    if _INV is None:
        try:
            with open(os.path.join(store.DATA_DIR, "inventory.json"), encoding="utf-8") as stream:
                _INV = json.load(stream)
        except Exception:
            _INV = {}
    return _INV


def _by_id(pid):
    for it in _items():
        if it.get("ID") == pid:
            return it
    return None


def paintable(p):
    global _PAINTABLE
    if _PAINTABLE is None:
        with open(os.path.join(store.DATA_DIR, "paintable.json"), encoding="utf-8") as f:
            _PAINTABLE = set(json.load(f))
    return {"paintable": int(p.get("productId") or 0) in _PAINTABLE}


PAINTS = ["Unpainted", "Crimson", "Lime", "Black", "Sky Blue", "Cobalt", "Burnt Sienna",
          "Forest Green", "Purple", "Pink", "Orange", "Grey", "Titanium White", "Saffron",
          "Gold", "Rose Gold", "White Gold", "Onyx", "Platinum"]


def _row(pid, paint=0, from_drop=0, title_id=""):
    it = _by_id(pid)
    return {
        "category": (it or {}).get("Slot", "Misc"), "fromDrop": from_drop,
        "instanceId": secrets.token_hex(16), "label": (it or {}).get("Product", "Item %s" % pid),
        "paint": paint, "paintName": PAINTS[paint] if paint < len(PAINTS) else "Unpainted",
        "productId": pid, "titleId": title_id,
        "addedTimestamp": int(time.time()),
    }


def _instance_index(it, d):
    return it.get("instanceId", "")


def add(p):
    d = _load()
    row = _row(int(p.get("productId") or 0), int(p.get("paint") or 0), int(p.get("fromDrop") or 0))
    d["items"].append(row)
    _save(d)
    return {"ok": True, "item": row}


def add_many(p):
    out = []
    for it in p.get("items") or []:
        out.append(add(it))
    return {"ok": True, "added": len(out)}


def add_list(p):
    product_ids = list(dict.fromkeys(int(pid) for pid in p.get("productIds") or []))
    selected_paints = list(dict.fromkeys(int(paint) for paint in p.get("paints") or []))
    if any(paint < 0 or paint >= len(PAINTS) for paint in selected_paints):
        raise ValueError("unknown paint")
    rows = []
    if selected_paints:
        for pid in product_ids:
            paints_for_product = selected_paints if paintable({"productId": pid})["paintable"] else [0]
            rows.extend(_row(pid, paint) for paint in paints_for_product)
    d = _load()
    d["items"].extend(rows)
    _save(d)
    return {"ok": True, "count": len(rows)}


def add_title(p):
    """Spawn a player title. The SPA sends {titleId}; titles ride the generic Title
    product (3036) + a TitleID attribute, so an existing title is not duplicated."""
    tid = str(p.get("titleId") or "").strip()
    if not tid:
        raise ValueError("titleId required")
    d = _load()
    for x in d["items"]:
        if x.get("titleId") == tid:
            return {"ok": True, "item": x, "already": True}
    row = _row(TITLE_PID, 0, 0, tid)
    d["items"].append(row)
    _save(d)
    return {"ok": True, "item": row}


def titles_owned():
    """TitleIDs currently in the spawn list, for the SPA's owned-title set."""
    d = _load()
    tids = [x.get("titleId") for x in d["items"] if x.get("titleId")]
    return {"titles": list(dict.fromkeys(tids)), "known": True}


def remove(p):
    d = _load()
    iid = p.get("instanceId")
    pid = p.get("productId")
    if not iid and pid is None:
        raise ValueError("instanceId or productId required")
    before = len(d["items"])
    d["items"] = [x for x in d["items"] if (x.get("instanceId") != iid if iid else x.get("productId") != pid)]
    _save(d)
    return {"ok": True, "removed": before - len(d["items"])}


def remove_many(p):
    # The SPA always sends {instanceIds:[...]} (items, drops and titles); keep legacy
    # {ids:[productId,...]} as a fallback. Titles share one productId, so per-instance
    # removal is required to delete a single title without clearing the rest.
    d = _load()
    insts = set(p.get("instanceIds") or [])
    pids = set(p.get("ids") or [])
    before = len(d["items"])
    d["items"] = [x for x in d["items"]
                  if x.get("instanceId") not in insts and x.get("productId") not in pids]
    _save(d)
    return {"ok": True, "removed": before - len(d["items"])}


def clear():
    count = len(_load()["items"])
    _save({"items": []})
    return {"ok": True, "removed": count}


def status():
    d = _load()
    return {"available": True, "count": len(d["items"])}


def response():
    d = _load()
    return "," + ",".join(x.get("instanceId", "") for x in d["items"]) + ","


def product_rows(timestamp):
    products = []
    for item in _load()["items"]:
        product_id = int(item.get("productId") or 0)
        instance_id = item.get("instanceId", "")
        if product_id <= 0 or not instance_id:
            continue
        attributes = []
        paint = int(item.get("paint") or 0)
        if paint:
            attributes.append({"Key": "Painted", "Value": paint})
        if item.get("titleId"):
            attributes.append({"Key": "TitleID", "Value": item["titleId"]})
        added = int(item.get("addedTimestamp") or timestamp)
        products.append({
            "ProductID": product_id, "InstanceID": instance_id, "Attributes": attributes,
            "SeriesID": 0, "AddedTimestamp": added, "UpdatedTimestamp": added,
        })
    return products


def paints():
    return {"paints": [{"id": i, "name": n} for i, n in enumerate(PAINTS)]}


def title_rows():
    return titles_owned()
