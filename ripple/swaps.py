import os, threading
from . import store, catalog, upk

_lock = threading.RLock()


def active():
    return store.read_json("swaps.json", [])


def save(rows):
    store.write_json("swaps.json", rows)


def _find(key):
    k = (key or "").lower()
    for it in catalog.catalog_items():
        if it["asset_path"].lower() == k or it["code"].lower() == k:
            return it
    raise ValueError("item not found: " + str(key))


def apply_swap(p):
    cooked = store.cooked_dir()
    if not cooked:
        raise ValueError("Rocket League install not set")
    tgt = _find(p.get("targetKey"))
    src = _find(p.get("sourceKey"))
    if tgt["category"] != src["category"]:
        raise ValueError("You can only swap an item for another of the same category.")
    tpath = os.path.join(cooked, tgt["file"])
    spath = os.path.join(cooked, src["file"])
    if not os.path.exists(tpath):
        raise ValueError("target package missing in the game")
    if not os.path.exists(spath):
        raise ValueError("source package missing in the game")
    upk.swap_file(tpath, spath, upk.pkg_basename(tgt["file"]), store.DATA_DIR)
    row = {
        "installCookedDir": cooked,
        "installSource": (store.settings().get("target") or {}).get("source", "Epic"),
        "redirectAssetPath": src["asset_path"], "redirectKey": tgt["code"],
        "sourceFile": src["file"], "sourceLabel": src["label"],
        "sourcePackage": upk.pkg_basename(src["file"]),
        "targetFile": tgt["file"], "targetLabel": tgt["label"],
        "paint": p.get("paint") or "", "paintOnly": False,
        "painted": bool(p.get("paint")), "timestamp": store.iso_now(),
    }
    with _lock:
        rows = [r for r in active() if not (r["targetFile"] == tgt["file"] and r["installSource"] == row["installSource"])]
        rows.append(row)
        save(rows)
    return row


def paint_item(p):
    # Best effort: store paint request and mark the matching active swap row.
    rows = active()
    for r in rows:
        if r.get("redirectKey") == p.get("itemKey") or r.get("targetFile", "").lower().startswith(str(p.get("itemKey", "")).split(".")[0].lower()):
            r["paint"] = p.get("hex", "")
            r["painted"] = True
    save(rows)
    return {"ok": True, "paint": p.get("hex", "")}


def revert(index):
    rows = active()
    if index < 0 or index >= len(rows):
        raise ValueError("bad index")
    r = rows.pop(index)
    cooked = r.get("installCookedDir") or store.cooked_dir()
    if cooked:
        upk.restore_file(os.path.join(cooked, r["targetFile"]))
    save(rows)
    return {"ok": True}


def revert_all():
    rows = active()
    for r in rows:
        cooked = r.get("installCookedDir") or store.cooked_dir()
        if cooked:
            try:
                upk.restore_file(os.path.join(cooked, r["targetFile"]))
            except Exception:
                pass
    save([])
    return {"ok": True}


def list_active():
    rows = active()
    other = "Steam" if (store.settings().get("target") or {}).get("source") == "Epic" else "Epic"
    return {"swaps": rows, "count": len(rows), "otherInstall": other,
            "copyable": 1 if other else 0}


def copy_to_other():
    return {"ok": True, "copied": 0}
