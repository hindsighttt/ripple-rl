import os, json, shutil, zipfile
from . import store

CAT = os.path.join(store.DATA_DIR, "workshop_catalog.json")


def catalog():
    try:
        rows = json.load(open(CAT, encoding="utf-8"))
        return rows if isinstance(rows, list) else rows.get("maps", rows.get("items", []))
    except Exception:
        return []


def list_maps():
    imported = []
    for name in os.listdir(store.WORKSHOP_DIR):
        p = os.path.join(store.WORKSHOP_DIR, name)
        if os.path.isdir(p):
            imported.append({"name": name, "imported": True})
    return {"catalog": catalog(), "imported": imported}


def import_map(path, name=None):
    name = name or os.path.splitext(os.path.basename(path))[0]
    dest = os.path.join(store.WORKSHOP_DIR, name)
    os.makedirs(dest, exist_ok=True)
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            z.extractall(dest)
    else:
        shutil.copy2(path, dest)
    return {"ok": True, "name": name}


def apply_map(name):
    cooked = store.cooked_dir()
    if not cooked:
        raise ValueError("Rocket League install not set")
    src = os.path.join(store.WORKSHOP_DIR, name)
    if not os.path.isdir(src):
        raise ValueError("map not found")
    target = os.path.join(cooked, "Labs_Utopia_P.upk")
    if not os.path.exists(target):
        raise ValueError("Labs_Utopia_P.upk missing")
    if not os.path.exists(target + ".ripplebak"):
        shutil.copy2(target, target + ".ripplebak")
    upk = [f for f in os.listdir(src) if f.lower().endswith((".upk", ".udk"))]
    if not upk:
        raise ValueError("map package missing")
    shutil.copy2(os.path.join(src, upk[0]), target)
    return {"ok": True}


def restore_map():
    cooked = store.cooked_dir()
    if cooked:
        t = os.path.join(cooked, "Labs_Utopia_P.upk")
        if os.path.exists(t + ".ripplebak"):
            shutil.copy2(t + ".ripplebak", t)
    return {"ok": True}


def delete_map(name):
    shutil.rmtree(os.path.join(store.WORKSHOP_DIR, name), ignore_errors=True)
    return {"ok": True}
