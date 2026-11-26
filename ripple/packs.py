import os
import shutil
import tempfile
import hashlib
from pathlib import Path
from urllib.parse import quote
from threading import Lock

from . import store

KIND_DIRS = {k: os.path.join(store.PACKS_DIR, k) for k in ("ball", "car", "decal", "hud")}
_apply_lock = Lock()


def pack_path(kind, name):
    if kind not in KIND_DIRS or not isinstance(name, str) or not name.strip():
        raise ValueError("Unknown pack")
    root = Path(KIND_DIRS[kind]).resolve()
    path = (root / name).resolve()
    if path.parent != root or any(c in name for c in '/\\:'):
        raise ValueError("Invalid pack name")
    return path


def _hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _state():
    return store.read_json("custom-pack-state.json", {})



def list_packs(kind):
    d = KIND_DIRS.get(kind)
    if not d:
        return {"packs": [], "activeName": ""}
    packs = []
    active = _state().get(kind, {})
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        if os.path.isdir(p):
            thumb = os.path.join(p, "thumb.png")
            packs.append({"name": name, "slug": name, "active": active.get("name") == name,
                          "thumb": "ripple://local/__packs/%s/%s/thumb.png" % (kind, quote(name, safe="")) if os.path.exists(thumb) else ""})
    return {"packs": packs, "activeName": active.get("name", "")}


def rename(kind, old, new):
    source, dest = pack_path(kind, old), pack_path(kind, new)
    if dest.exists():
        raise ValueError("Pack name already exists")
    source.rename(dest)
    state = _state()
    if state.get(kind, {}).get("name") == old:
        state[kind]["name"] = new
        store.write_json("custom-pack-state.json", state)
    return {"ok": True}


def remove(kind, name):
    path = pack_path(kind, name)
    if _state().get(kind, {}).get("name") == name:
        raise ValueError("Restore this pack before deleting it")
    if path.is_dir():
        shutil.rmtree(path)
    return {"ok": True}


def import_zip(kind, path, name=None):
    import zipfile
    name = name or Path(path).stem
    dest = pack_path(kind, name)
    if dest.exists():
        raise ValueError("Pack name already exists")
    with tempfile.TemporaryDirectory(dir=KIND_DIRS[kind]) as staging:
        root = Path(staging).resolve()
        with zipfile.ZipFile(path) as archive:
            for member in archive.infolist():
                target = (root / member.filename.replace("\\", "/")).resolve()
                if not target.is_relative_to(root):
                    raise ValueError("Archive path leaves the pack folder")
            archive.extractall(root)
        if not any(p.is_file() and p.suffix.lower() == ".upk" for p in root.rglob("*")):
            raise ValueError("Pack contains no UPK packages")
        shutil.copytree(root, dest)
    return {"ok": True, "name": name, "slug": name}


def restore_pack(kind, name=None):
    with _apply_lock:
        return _restore_pack(kind, name)


def _restore_pack(kind, name=None):
    if kind not in KIND_DIRS:
        raise ValueError("Unknown kind")
    state = _state()
    record = state.get(kind)
    if not record or (name and name != record.get("name")):
        return {"ok": True, "restored": 0}
    cooked = Path(record["cookedDir"]).resolve()
    targets = []
    for filename, digest in record["files"].items():
        target = (cooked / filename).resolve()
        if target.parent != cooked:
            raise ValueError("Invalid saved pack target")
        if _hash(target) != digest:
            raise ValueError("A pack file changed outside the rebuild: " + filename)
        backup = Path(str(target) + ".ripplebak")
        if not backup.is_file():
            raise ValueError("Pack backup missing: " + filename)
        targets.append((target, backup))
    for target, backup in targets:
        temporary = Path(str(target) + ".ripple-restore")
        shutil.copy2(backup, temporary)
        os.replace(temporary, target)
    state.pop(kind)
    store.write_json("custom-pack-state.json", state)
    return {"ok": True, "restored": len(targets)}


def apply_pack(kind, name):
    cooked = store.cooked_dir()
    if not cooked:
        raise ValueError("Rocket League install not set")
    src = pack_path(kind, name)
    if not src.is_dir():
        raise ValueError("pack not found")
    packages = [p for p in src.rglob("*") if p.is_file() and p.suffix.lower() == ".upk"
                and (Path(cooked) / p.name).is_file()]
    if not packages:
        raise ValueError("No pack package matches this game install")
    if len({p.name.lower() for p in packages}) != len(packages):
        raise ValueError("Pack contains duplicate package filenames")
    with _apply_lock, tempfile.TemporaryDirectory(dir=cooked) as staging:
        # Stage every source before restoring/replacing anything in the install.
        for source in packages:
            shutil.copy2(source, os.path.join(staging, source.name))
        current = _state()
        for other, record in current.items():
            if other != kind and record.get("cookedDir") == cooked and any(p.name in record["files"] for p in packages):
                raise ValueError("Another active pack uses these packages")
        _restore_pack(kind)
        for source in packages:
            target = Path(cooked) / source.name
            backup = Path(str(target) + ".ripplebak")
            if not backup.exists():
                shutil.copy2(target, backup)
            os.replace(os.path.join(staging, source.name), target)
        current = _state()
        current[kind] = {"name": name, "cookedDir": cooked,
                         "files": {p.name: _hash(Path(cooked) / p.name) for p in packages}}
        store.write_json("custom-pack-state.json", current)
    return {"ok": True, "copied": len(packages)}
