"""Apply Ripple RL's garage/underglow UPK patch, hash-guarded and fail-safe.

Instead of re-implementing RL's cooked-package codec, this applies a *version-matched*
ground-truth patch captured from the real tool: the exact patched packages Ripple RL produces,
keyed by the vanilla package's sha256. Application is byte-exact (the result is Ripple RL's own
output) and fail-safe:

- A package is patched only when the live file's sha256 matches the recorded vanilla hash.
- If the live hash is unknown (e.g. a game update changed the package), the package is left
  untouched and reported, so an update can never corrupt the install.
- The pre-patch bytes are backed up, so revert restores the exact original.

The patch data (manifest + patched packages) is version-specific; after a Rocket League
update it must be re-captured, the same way Ripple RL re-ships its manifests.
"""

import hashlib
import json
import os
import shutil
from pathlib import Path

from . import store

PACKAGES = [
    "TAGame.upk",
    "Engine.upk",
    "GFxShell_SF.upk",
    "GFX_PremiumContainerMenu_SF.upk",
    "GFX_Hud_SF.upk",
]


class GaragePatchError(Exception):
    pass


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_manifest(patch_dir):
    """patch_dir holds patch-manifest.json and an `after/` folder of patched packages."""
    path = os.path.join(patch_dir, "patch-manifest.json")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _state(live_path, entry):
    if not os.path.isfile(live_path):
        return "missing"
    sha = _sha256(live_path)
    if sha == entry["patchedSha256"]:
        return "patched"
    if sha == entry["vanillaSha256"]:
        return "vanilla"
    return "unknown"


def status(cooked_dir, patch_dir):
    """Classify each package: vanilla / patched / unknown / missing."""
    man = load_manifest(patch_dir)
    out = {}
    for name in PACKAGES:
        entry = man["packages"].get(name)
        if not entry:
            out[name] = "no-patch-data"
            continue
        out[name] = _state(os.path.join(cooked_dir, name), entry)
    return out


def apply(cooked_dir, patch_dir, backup_dir):
    """Patch every package whose live bytes match the recorded vanilla hash.

    Returns a per-package result dict. Never raises on an unknown/updated package —
    it is skipped and reported, so the game is never corrupted.
    """
    man = load_manifest(patch_dir)
    after_dir = os.path.join(patch_dir, "after")
    os.makedirs(backup_dir, exist_ok=True)
    results = {}
    for name in PACKAGES:
        entry = man["packages"].get(name)
        live = os.path.join(cooked_dir, name)
        if not entry:
            results[name] = "no-patch-data"
            continue
        st = _state(live, entry)
        if st == "patched":
            results[name] = "already-patched"
            continue
        if st != "vanilla":
            # unknown hash (game updated) or missing -> fail safe, do not touch
            results[name] = "skipped-" + st
            continue
        patched_src = os.path.join(after_dir, name)
        if _sha256(patched_src) != entry["patchedSha256"]:
            results[name] = "bad-patch-data"
            continue
        # back up the exact vanilla bytes, then write the patched package atomically
        backup = os.path.join(backup_dir, name)
        if not os.path.exists(backup):
            shutil.copy2(live, backup)
        if _sha256(backup) != entry["vanillaSha256"] or _state(live, entry) != "vanilla":
            results[name] = "baseline-changed"
            continue
        tmp = live + ".ripple-tmp"
        shutil.copy2(patched_src, tmp)
        os.replace(tmp, live)
        if _sha256(live) != entry["patchedSha256"]:
            raise GaragePatchError("Verification failed after patching " + name)
        results[name] = "patched"
    return results


def revert(cooked_dir, patch_dir, backup_dir):
    """Restore the exact vanilla bytes from backup for any patched package."""
    man = load_manifest(patch_dir)
    results = {}
    flip = store.read_json("flip-reset-patch.json", {})
    for name in PACKAGES:
        entry = man["packages"].get(name)
        live = os.path.join(cooked_dir, name)
        backup = os.path.join(backup_dir, name)
        if not entry or not os.path.isfile(live):
            results[name] = "missing"
            continue
        current = _sha256(live)
        if current == entry["vanillaSha256"]:
            results[name] = "already-vanilla"
            continue
        managed_flip = (
            name == "TAGame.upk" and isinstance(flip, dict)
            and flip.get("path") == str(Path(live).resolve())
            and flip.get("baseHash") == entry["patchedSha256"]
            and flip.get("installedHash") == current
        )
        if current != entry["patchedSha256"] and not managed_flip:
            results[name] = "skipped-unknown"
            continue
        if not os.path.isfile(backup) or _sha256(backup) != entry["vanillaSha256"]:
            backup = os.path.join(patch_dir, "before", name)
        if os.path.isfile(backup) and _sha256(backup) == entry["vanillaSha256"] and _sha256(live) == current:
            tmp = live + ".ripple-tmp"
            shutil.copy2(backup, tmp)
            if _sha256(tmp) != entry["vanillaSha256"]:
                os.unlink(tmp)
                raise GaragePatchError("Garage restore backup changed: " + name)
            os.replace(tmp, live)
            results[name] = "reverted"
        else:
            results[name] = "no-clean-backup"
    return results
