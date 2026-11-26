import os, sys, json, time, subprocess, webbrowser, secrets, inspect
from PySide6.QtCore import QObject, Slot, Signal, QTimer
import copy
from . import store, catalog, swaps, packs, workshop, features, spawn, social, upk, proxy, quickchat, bootstrap, garage_patch, flip_patch, titles_config

VERSION = "1.8.4"


_rl_cache = {"t": 0.0, "v": False}
_RL_TTL = 2.0


def _rl_running():
    # home.state is polled every ~3s and asks twice per call; spawning tasklist
    # each time froze the GUI thread for 100-300ms. Cache the answer briefly.
    now = time.time()
    if now - _rl_cache["t"] < _RL_TTL:
        return _rl_cache["v"]
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq RocketLeague.exe"],
                             capture_output=True, text=True, creationflags=0x08000000).stdout
        val = "RocketLeague.exe" in out
    except Exception:
        val = False
    _rl_cache["t"] = now
    _rl_cache["v"] = val
    return val


_path_cache = {}


def _steam_path():
    # Install paths don't change during a session; memoize to avoid a registry
    # hit on every home.state poll.
    if "steam" in _path_cache:
        return _path_cache["steam"]
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
            root, _ = winreg.QueryValueEx(k, "SteamPath")
        val = os.path.join(root, "steamapps", "common", "rocketleague", "TAGame", "CookedPCConsole")
    except Exception:
        val = ""
    _path_cache["steam"] = val
    return val


def _epic_path():
    if "epic" in _path_cache:
        return _path_cache["epic"]
    _path_cache["epic"] = _epic_path_uncached()
    return _path_cache["epic"]


def _epic_path_uncached():
    p = r"C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    try:
        data = json.load(open(p, encoding="utf-8"))
        for inst in data.get("InstallationList", []):
            if inst.get("AppName") == "Sugar":
                return os.path.join(inst["InstallLocation"], "TAGame", "CookedPCConsole")
    except Exception:
        pass
    return ""


class Bridge(QObject):
    event = Signal(str, dict)
    state = Signal()

    def __init__(self, app):
        super().__init__()
        self.app = app
        self._started = time.time()
        self._play_base = 0
        self._handlers = {}
        self._register()

    def emit_event(self, type_, payload=None):
        self.event.emit(type_, payload or {})

    # ---------- registry ----------
    def _register(self):
        h = self._handlers
        h.update({
            "app.state": self.app_state,
            "app.openUrl": lambda p: webbrowser.open(p.get("url", "")),
            "app.setTheme": self.set_theme,
            "app.setLang": self.set_lang,
            "app.devtools": lambda p: None,
            "win.minimize": lambda p: self.app.showMinimized(),
            "win.maximize": lambda p: self.app.toggle_max(),
            "win.close": lambda p: self.app.close(),
            "win.drag": lambda p: None,
            "win.resize": lambda p: None,
            "settings.get": self.settings_get,
            "settings.setBringToFront": self.settings_set,
            "settings.setSourcePath": self.settings_source_path,
            "settings.setCloseToTray": lambda p: self.setting_toggle("closeToTray", p),
            "settings.setAlwaysOnTop": lambda p: self.setting_toggle("alwaysOnTop", p),
            "settings.detectSourcePath": self.detect_source,
            "settings.openBackupFolder": lambda p: os.startfile(store.BACKUP_DIR),
            "settings.refreshCatalog": lambda p: {"ok": True},
            "settings.repair": lambda p: {"ok": True},
            "home.state": self.home_state,
            "home.launch": self.home_launch,
            "home.setPseudo": self.set_pseudo,
            "home.forgetPseudo": self.forget_pseudo,
            "home.setInstall": self.home_set_install,
            "items.categories": lambda p: catalog.categories(),
            "catalog.titles": lambda p: catalog.titles(),
            "items.list": catalog.list_items,
            "items.inventorySync": lambda p: {"ok": True, "inventoryKnown": True, "inventoryCount": catalog.inv_count()},
            "items.swap": self.items_swap,
            "items.paint": swaps.paint_item,
            "active.list": swaps.list_active,
            "active.revert": self.active_revert,
            "active.revertAll": self.active_revert_all,
            "active.copyToOther": lambda p: swaps.copy_to_other(),
            "presets.list": self.presets_list,
            "presets.new": self.presets_new,
            "presets.rename": self.presets_rename,
            "presets.duplicate": self.presets_duplicate,
            "presets.delete": self.presets_delete,
            "presets.load": self.presets_load,
            "presets.export": self.presets_export,
            "presets.import": self.presets_import,
            "custom.list": self.custom_list,
            "custom.import": self.custom_import,
            "custom.remove": self.custom_remove,
            "custom.rename": self.custom_rename,
            "custom.apply": self.custom_apply,
            "custom.catalog": lambda p: {"packs": []},
            "custom.catalog.download": lambda p: {"ok": True},
            "custom.car.bodies": lambda p: {"bodies": []},
            "custom.car.options": lambda p: {"options": []},
            "custom.ballImage": lambda p: {"ok": True},
            "custom.decal.thumbnails": lambda p: {},
            "custom.setThumbnail": self.custom_thumbnail,
            "workshop.list": lambda p: {**workshop.list_maps(), "imported": workshop.list_maps()["imported"]},
            "workshop.refresh": lambda p: {"ok": True},
            "workshop.import": lambda p: workshop.import_map(p.get("path", ""), p.get("name")),
            "workshop.apply": lambda p: workshop.apply_map(p.get("name", "")),
            "workshop.play": lambda p: {"ok": True},
            "workshop.restore": lambda p: workshop.restore_map(),
            "workshop.delete": lambda p: workshop.delete_map(p.get("name", "")),
            "workshop.activate": lambda p: {"ok": True},
            "workshop.download": lambda p: {"ok": True},
            "titles.get": lambda p: features.get("titles"),
            "titles.save": social.save_title,
            "titles.setEnabled": self.title_set_enabled,
            "titles.remove": self.title_remove,
            "titles.removeAll": lambda p: functions_title_clear(),
            "spawn.status": lambda p: spawn.status(),
            "spawn.list": lambda p: spawn.list_spawn(),
            "spawn.add": spawn.add,
            "spawn.addMany": spawn.add_many,
            "spawn.addList": spawn.add_list,
            "spawn.addTitle": spawn.add_title,
            "spawn.remove": spawn.remove,
            "spawn.removeMany": spawn.remove_many,
            "spawn.clear": lambda p: spawn.clear(),
            "spawn.paints": lambda p: spawn.paints(),
            "spawn.paintable": spawn.paintable,
            "spawn.titlesOwned": lambda p: spawn.titles_owned(),
            "spawn.getLoadoutDelay": lambda p: {"delay": 1200},
            "spawn.setLoadoutDelay": lambda p: {"ok": True},
            "spawn.getLoadoutOnlyHold": lambda p: {"hold": False},
            "spawn.setLoadoutOnlyHold": lambda p: {"ok": True},
            "spawn.sendPriRpc": lambda p: {"ok": True},
            "spawn.spectate": lambda p: {"ok": True},
            "spawn.joinTeam": lambda p: {"ok": True},
            "fakerank.get": lambda p: features.get("fakerank"),
            "fakerank.setActive": lambda p: features.set_state("fakerank", {"active": bool(p.get("value"))}),
            "fakerank.setAutoStart": lambda p: features.set_state("fakerank", {"autoStart": bool(p.get("value"))}),
            "fakerank.setOverride": self.fakerank_override,
            "fakerank.clearOverride": self.fakerank_clear,
            "fakerank.setReward": lambda p: features.set_state("fakerank", {"reward": p}),
            "misc.ping.get": lambda p: features.get("ping"),
            "misc.ping.set": lambda p: features.set_state("ping", p),
            "misc.ping.own.set": lambda p: features.set_state("ping", {"own": bool(p.get("value"))}),
            "misc.ping.changed": lambda p: None,
            "misc.platform.get": lambda p: features.get("platform"),
            "misc.platform.set": lambda p: features.set_state("platform", p),
            "misc.qc.get": lambda p: features.get("qc"),
            "misc.qc.setEnabled": self.qc_enabled,
            "misc.qc.setEntries": self.qc_set,
            "misc.qc.setKey": self.qc_key,
            "misc.qc.labels": lambda p: {"labels": [quickchat.key_label(code) for code in p.get("codes") or []]},
            "misc.qc.scan": lambda p: {"ok": True},
            "misc.match.get": lambda p: features.get("misc.match"),
            "misc.match.setKey": lambda p: features.set_state("misc.match", p),
            "misc.match.freeze": lambda p: features.set_state("misc.match", {"frozen": bool(p.get("value"))}),
            "overlay.get": lambda p: features.get("overlay"),
            "overlay.active": lambda p: {"active": False},
            "overlay.set": self.overlay_set,
            "overlay.reset": lambda p: {"ok": True},
            "garage.get": lambda p: features.get("garage"),
            "garage.set": lambda p: features.set_state("garage", p),
            "garage.patch.status": lambda p: self.garage_patch_status(),
            "garage.patch.apply": lambda p: self.garage_patch_apply(),
            "garage.patch.revert": lambda p: self.garage_patch_revert(),
            "paint.get": lambda p: features.get("paint"),
            "paint.set": lambda p: features.set_state("paint", p),
            "match.get": lambda p: features.get("match"),
            "match.set": lambda p: features.set_state("match", p),
            "stadium.get": lambda p: {"maps": features.stadiums(), "plus": True, "teams": [
                {"color": "#ffffff", "on": False, "rainbow": False}] * 2, "menuMap": "", "rainbowSpeed": 20},
            "stadium.set": lambda p: {"ok": True},
            "game.get": lambda p: features.get("game"),
            "game.set": lambda p: features.set_state("game", p),
            "misc.match.fired": lambda p: None,
            "plugins.get": lambda p: {"enabled": False, "instant": {}, "presets": [], "customs": [], "slots": [False] * 4},
            "cust.palette.get": lambda p: features.get("cust.palette"),
            "cust.palette.set": lambda p: features.set_state("cust.palette", p),
            "cust.cameraCaps.get": lambda p: features.get("cust.cameraCaps"),
            "cust.cameraCaps.set": lambda p: features.set_state("cust.cameraCaps", p),
            "credits.get": lambda p: features.get("credits"),
            "tracker.state": lambda p: features.get("tracker"),
            "tracker.settings": lambda p: features.get("tracker.settings"),
            "tracker.active": lambda p: {"active": False},
            "tracker.set": self.tracker_set,
            "tracker.resetSession": lambda p: features.set_state("tracker", {"session": {"losses": 0, "streak": 0, "wins": 0}}),
            "tracker.forgetAccount": lambda p: {"ok": True},
            "sounds.list": lambda p: features.get("sounds"),
            "sounds.import": lambda p: {"ok": True},
            "sounds.revert": lambda p: {"ok": True},
            "sounds.setAlphaBoost": lambda p: features.set_state("sounds", {"alphaBoost": bool(p.get("value"))}),
            "sounds.setVolume": lambda p: {"ok": True},
            "rlsettings.get": lambda p: features.get("rlsettings"),
            "loadouts.list": lambda p: {"loadouts": []},
            "loadouts.accounts": lambda p: {"accounts": []},
            "loadouts.inventory": lambda p: {"items": []},
            "loadouts.catalog": lambda p: {"items": []},
            "loadouts.palettes": lambda p: {"palettes": []},
            "loadouts.config.list": lambda p: {"configs": []},
            "loadouts.export": lambda p: {"ok": True},
            "loadouts.pack.open": lambda p: {"ok": True},
            "loadouts.setAccount": lambda p: {"ok": True},
            "mp.state": lambda p: {**features.get("mp"), "pseudo": store.settings().get("pseudo", "")},
            "mp.refreshRooms": lambda p: {"rooms": []},
            "mp.host": lambda p: {"ok": True},
            "mp.join": lambda p: {"ok": True},
            "mp.leave": lambda p: {"ok": True},
            "mp.chat": lambda p: {"ok": True},
            "mp.kick": lambda p: {"ok": True},
            "mp.lock": lambda p: {"ok": True},
            "mp.createMatch": lambda p: {"ok": True},
            "mp.joinMatch": lambda p: {"ok": True},
            "mp.acceptMap": lambda p: {"ok": True},
            "invite.get": lambda p: features.get("invite"),
            "invite.arm": lambda p: features.set_state("invite", {"armed": bool(p.get("value"))}),
            "invite.players": lambda p: {"players": []},
            "fps.get": lambda p: features.get("fps"),
            "fps.set": self.fps_set,
            "spoof.get": lambda p: features.get("spoof"),
            "spoof.setFriends": lambda p: features.set_state("spoof", {"friends": p.get("friends", [])}),
            "spoof.resolve": lambda p: {"status": "Stopped"},
            "tas.get": lambda p: {"plus": True, "enabled": False, "files": [], "loaded": None},
            "tas.openFolder": lambda p: {"ok": True},
            "titles.changed": lambda p: None,
            "notice.dismiss": lambda p: {"ok": True},
            "drop.write": lambda p: {"ok": True},
            "spawn.failed": lambda p: None,
            "spawn.freeze": lambda p: None,
        })
        for kind in ("ball", "car", "decal", "hud"):
            h["custom." + kind + ".apply"] = lambda p, k=kind: self.custom_apply({**p, "kind": k})
            h["custom." + kind + ".restore"] = lambda p, k=kind: self.custom_restore({**p, "kind": k})
        h["custom.car.revert"] = lambda p: self.custom_restore({**p, "kind": "car"})
        h["custom.decal.revertOne"] = lambda p: self.custom_restore({**p, "kind": "decal"})

    # ---------- app/settings ----------
    def app_state(self, p):
        s = store.settings()
        return {"version": VERSION, "licenseType": "plus", "itemCount": len(catalog.catalog_items()),
                "lang": s.get("language", "en"), "theme": s.get("theme", "dark"),
                "devBuild": False, "inviteAccess": False, "catalogBuilding": False,
                "maximized": self.app.isMaximized()}

    def set_theme(self, p):
        v = p.get("theme") or p.get("value") or "dark"
        if "dark" in p:
            v = "dark" if p["dark"] else "light"
        store.save_settings({"theme": v})
        return {"ok": True}

    def set_lang(self, p):
        store.save_settings({"language": p.get("lang") or p.get("value") or "en"})
        return {"ok": True}

    def settings_get(self):
        s = store.settings()
        return {"activeSource": (s.get("target") or {}).get("source", ""),
                "alwaysOnTop": s.get("alwaysOnTop", False),
                "bringToFrontHotkey": s.get("bringToFrontHotkey", "F2"),
                "closeToTray": s.get("closeToTray", False),
                "discordRpc": s.get("discordRpc", True),
                "epicPath": self.source_path("Epic"), "steamPath": self.source_path("Steam"),
                "startup": s.get("startup", False)}

    def settings_set(self, p):
        key = p.get("key", "bringToFrontHotkey")
        val = p.get("value")
        if key == "bringToFrontHotkey":
            store.save_settings({"bringToFrontHotkey": val})
            if self.app:
                self.app.reload_qc()
        elif key == "sourcePath":
            store.save_settings({"target": {"cookedDir": val, "source": p.get("source", "Epic")}})
        return {"ok": True}

    def setting_toggle(self, key, p):
        store.save_settings({key: bool(p.get("value"))})
        if key == "alwaysOnTop" and self.app:
            self.app.apply_window_settings()
        return {"ok": True}

    def source_path(self, source):
        s = store.settings()
        target = s.get("target") or {}
        return (s.get("installPaths", {}).get(source) or
                (target.get("cookedDir") if target.get("source") == source else "") or
                (_steam_path() if source == "Steam" else _epic_path()))

    def settings_source_path(self, p):
        source = p.get("source", "Epic")
        if source not in ("Steam", "Epic"):
            raise ValueError("Unknown source")
        path = p.get("path") or p.get("value")
        if not path:
            from PySide6.QtWidgets import QFileDialog
            path = QFileDialog.getExistingDirectory(self.app, "Rocket League install", self.source_path(source))
        if not path:
            return {"cancelled": True}
        candidates = [path, os.path.join(path, "TAGame", "CookedPCConsole"), os.path.join(path, "CookedPCConsole")]
        cooked = next((os.path.abspath(c) for c in candidates if os.path.isdir(c) and os.path.basename(c).lower() == "cookedpcconsole"), None)
        if not cooked:
            raise ValueError("Choose the Rocket League install or its CookedPCConsole folder")
        paths = dict(store.settings().get("installPaths") or {})
        paths[source] = cooked
        store.save_settings({"installPaths": paths, "target": {"source": source, "cookedDir": cooked}})
        self.emit_event("home.changed", {})
        return {"ok": True, "path": cooked}

    def detect_source(self, p):
        source = p.get("source") or "Steam"
        path = self.source_path(source)
        if not path or not os.path.isdir(path):
            raise ValueError("Unknown source")
        return self.settings_source_path({"source": source, "path": path})

    def set_pseudo(self, p):
        v = (p.get("value") or "").strip()
        s = store.settings()
        history = list(dict.fromkeys(([v] if v else []) + s.get("pseudoHistory", []) + ([s["pseudo"]] if s.get("pseudo") else [])))[:50]
        store.save_settings({"pseudo": v, "pseudoHistory": history})
        return {"ok": True, "value": v, "history": history}

    def forget_pseudo(self, p):
        s = store.settings()
        name = p.get("value")
        history = [v for v in s.get("pseudoHistory", [s.get("pseudo", "")]) if v and v != name]
        store.save_settings({"pseudoHistory": history, **({"pseudo": ""} if name is None else {})})
        return {"ok": True, "history": history}

    # ---------- home ----------
    def home_state(self):
        s = store.settings()
        tgt = s.get("target") or {}
        insts = [{"source": source, "active": tgt.get("source") == source}
                 for source in ("Epic", "Steam") if os.path.isdir(self.source_path(source) or "")]
        return {"accountName": s.get("rlAccountName", ""), "active": tgt.get("source", ""),
                "installs": insts, "launchBusy": False, "pseudo": s.get("pseudo", ""),
                "pseudoHistory": s.get("pseudoHistory", [s["pseudo"]] if s.get("pseudo") else []),
                "playtimeBaseSeconds": self._play_base, "ready": True, "rlRunning": _rl_running(),
                "segmentElapsedSeconds": int(time.time() - self._started) if _rl_running() else 0,
                "steamAvatarUrl": "", "steamUrl": "", "version": VERSION}

    def home_launch(self, p):
        if _rl_running():
            raise ValueError("Close Rocket League before preparing a new launch.")
        cooked = store.cooked_dir()
        if not cooked:
            raise ValueError("Set your Rocket League install in Settings first.")
        root = os.path.dirname(os.path.dirname(cooked))
        exe = os.path.join(root, "Binaries", "Win64", "RocketLeague.exe")
        if not os.path.exists(exe):
            raise ValueError("RocketLeague.exe not found")
        if store.settings().get("proxyEnabled", True):
            proxy.start()
            preparation = bootstrap.prepare(cooked, store.DATA_DIR, store.BACKUP_DIR)
            if preparation.get("changed"):
                store.write_json("proxy-bootstrap.json", {"cookedDir": cooked, **preparation})
        proxy.patch_webcache()
        try:
            titles_config.refresh_webcache()
        except Exception:
            pass
        # Apply the garage/underglow UPK patch (byte-exact, hash-guarded, fail-safe).
        try:
            result = garage_patch.apply(cooked, store.garage_patch_dir(), store.garage_backup_dir())
            store.write_json("garage-patch.json", {"cookedDir": cooked, "result": result})
        except FileNotFoundError:
            pass  # no patch data installed for this build
        except Exception as error:
            print("garage patch:", error)
        flip_colour = int(features.get("garage").get("flipColor", -1))
        try:
            flip_patch.prepare(cooked, store.garage_patch_dir(), store.BACKUP_DIR, flip_colour)
        except FileNotFoundError:
            if flip_colour != -1:
                raise ValueError("Install this build's garage patch data before setting flip-reset colour.")
        src = (store.settings().get("target") or {}).get("source")
        if src == "Epic" and not p.get("noEac"):
            try:
                os.startfile("com.epicgames.launcher://apps/Sugar?action=launch&silent=true")
            except Exception:
                args = [exe]
                subprocess.Popen(args, cwd=os.path.dirname(exe), creationflags=0x08000000)
        else:
            args = [exe]
            if p.get("noEac"):
                args.append("-noeac")
            subprocess.Popen(args, cwd=os.path.dirname(exe), creationflags=0x08000000)
        QTimer.singleShot(1500, lambda: self.emit_event("home.launchResult", {"error": ""}))
        return {"ok": True}

    def home_set_install(self, p):
        source = p.get("source", "Epic")
        return self.settings_source_path({"source": source, "path": p.get("path") or self.source_path(source)})

    # ---------- garage UPK patch ----------
    def garage_patch_status(self):
        cooked = store.cooked_dir()
        if not cooked:
            return {"ok": False, "error": "no install set", "packages": {}}
        try:
            return {"ok": True, "packages": garage_patch.status(cooked, store.garage_patch_dir())}
        except FileNotFoundError:
            return {"ok": False, "error": "no patch data for this build", "packages": {}}

    def garage_patch_apply(self):
        cooked = store.cooked_dir()
        if not cooked:
            raise ValueError("Set your Rocket League install in Settings first.")
        if _rl_running():
            raise ValueError("Close Rocket League before patching.")
        result = garage_patch.apply(cooked, store.garage_patch_dir(), store.garage_backup_dir())
        store.write_json("garage-patch.json", {"cookedDir": cooked, "result": result})
        return {"ok": True, "result": result}

    def garage_patch_revert(self):
        cooked = store.cooked_dir()
        if not cooked:
            raise ValueError("Set your Rocket League install in Settings first.")
        if _rl_running():
            raise ValueError("Close Rocket League before reverting.")
        return {"ok": True, "result": garage_patch.revert(cooked, store.garage_patch_dir(), store.garage_backup_dir())}

    # ---------- presets ----------
    def presets_list(self):
        d = store.read_json("presets.json", {"currentPreset": "Default", "presets": [{"name": "Default", "swaps": []}]})
        return d

    def presets_new(self, p):
        d = self.presets_list()
        name = (p.get("name") or "Preset").strip()
        if not name:
            raise ValueError("preset name required")
        if any(x["name"] == name for x in d["presets"]):
            raise ValueError("preset exists")
        d["presets"].append({"name": name, "swaps": swaps.active()})
        store.write_json("presets.json", d)
        self.emit_event("presets.changed", {})
        return d

    def presets_rename(self, p):
        d = self.presets_list()
        name = p.get("name")
        new_name = (p.get("newName") or "").strip()
        if not new_name:
            raise ValueError("preset name required")
        preset = next((x for x in d["presets"] if x["name"] == name), None)
        if preset is None:
            raise ValueError("preset not found")
        if new_name != name and any(x["name"] == new_name for x in d["presets"]):
            raise ValueError("preset exists")
        preset["name"] = new_name
        if d.get("currentPreset") == name:
            d["currentPreset"] = new_name
        store.write_json("presets.json", d)
        self.emit_event("presets.changed", {})
        return d

    def presets_duplicate(self, p):
        d = self.presets_list()
        new_name = (p.get("newName") or "").strip()
        if not new_name:
            raise ValueError("preset name required")
        preset = next((x for x in d["presets"] if x["name"] == p.get("name")), None)
        if preset is None:
            raise ValueError("preset not found")
        if any(x["name"] == new_name for x in d["presets"]):
            raise ValueError("preset exists")
        duplicate = copy.deepcopy(preset)
        duplicate["name"] = new_name
        d["presets"].append(duplicate)
        store.write_json("presets.json", d)
        self.emit_event("presets.changed", {})
        return d

    def presets_delete(self, p):
        d = self.presets_list()
        d["presets"] = [x for x in d["presets"] if x["name"] != p.get("name")]
        store.write_json("presets.json", d)
        return d

    def presets_load(self, p):
        d = self.presets_list()
        name = p.get("name")
        pr = next((x for x in d["presets"] if x["name"] == name), None)
        if not pr:
            raise ValueError("preset not found")
        swaps.revert_all()
        for row in pr.get("swaps", []):
            try:
                swaps.apply_swap({"targetKey": row.get("redirectKey"), "sourceKey": row.get("redirectAssetPath")})
            except Exception:
                pass
        d["currentPreset"] = name
        store.write_json("presets.json", d)
        return {"ok": True}

    def presets_export(self, p):
        import tkinter.filedialog as fd
        path = fd.asksaveasfilename(defaultextension=".json", filetypes=[("Preset", "*.json")])
        if not path:
            return {"ok": False}
        json.dump(self.presets_list(), open(path, "w", encoding="utf-8"), indent=1)
        return {"ok": True}

    def presets_import(self, p):
        import tkinter.filedialog as fd
        path = fd.askopenfilename(filetypes=[("Preset", "*.json")])
        if not path:
            return {"ok": False}
        d = json.load(open(path, encoding="utf-8"))
        store.write_json("presets.json", d)
        return d

    # ---------- custom packs ----------
    def custom_list(self, p):
        kind = p.get("kind")
        if kind not in ("ball", "car", "decal", "hud"):
            raise ValueError("Unknown kind")
        return packs.list_packs(kind)

    def custom_import(self, p):
        import tkinter.filedialog as fd
        kind = p.get("kind")
        path = p.get("path") or fd.askopenfilename(filetypes=[("Packs", "*.zip")])
        if not path:
            return {"cancelled": True}
        result = packs.import_zip(kind, path)
        if kind == "car":
            result.update(cars=[{"name": result["name"], "slug": result["slug"]}], skipped=0)
        return result

    def custom_remove(self, p):
        return packs.remove(p.get("kind"), p.get("slug") or p.get("name"))

    def custom_rename(self, p):
        return packs.rename(p.get("kind"), p.get("slug") or p.get("name"), p.get("newName") or p.get("name"))

    def custom_apply(self, p):
        try:
            return packs.apply_pack(p.get("kind"), p.get("slug") or p.get("name"))
        finally:
            if p.get("kind") == "ball":
                self.emit_event("custom.ball.finished", {})

    def custom_restore(self, p):
        return packs.restore_pack(p.get("kind"), p.get("slug") or p.get("name"))

    def custom_thumbnail(self, p):
        from PySide6.QtWidgets import QFileDialog
        from PySide6.QtGui import QImage
        directory = packs.pack_path(p.get("kind"), p.get("slug") or p.get("name"))
        if not directory.is_dir():
            raise ValueError("Pack not found")
        path = p.get("path")
        if not path:
            path, _ = QFileDialog.getOpenFileName(self.app, "Pack thumbnail", "", "Images (*.png *.jpg *.jpeg *.webp)")
        if not path:
            return {"cancelled": True}
        image = QImage(path)
        temporary = directory / "thumb.tmp.png"
        if image.isNull() or not image.save(str(temporary), "PNG"):
            raise ValueError("Could not read thumbnail image")
        os.replace(temporary, directory / "thumb.png")
        return {"ok": True}

    # ---------- titles/fakerank ----------
    def title_set_enabled(self, p):
        state = features.set_state("titles", {"enabled": bool(p.get("value"))})
        titles_config.refresh_webcache()
        return state

    def title_remove(self, p):
        f = features.get("titles")
        rows = [t for t in f.get("titles", []) if t.get("id") != p.get("id")]
        features.set_state("titles", {"titles": rows})
        titles_config.refresh_webcache()
        return {"ok": True}

    def fakerank_override(self, p):
        f = features.get("fakerank")
        rows = [r for r in f.get("overrides", []) if r.get("playlist") != p.get("playlist")]
        rows.append(p)
        features.set_state("fakerank", {"overrides": rows})
        return {"ok": True}

    def fakerank_clear(self, p):
        f = features.get("fakerank")
        features.set_state("fakerank", {"overrides": [r for r in f.get("overrides", []) if r.get("playlist") != p.get("playlist")]})
        return {"ok": True}

    def items_swap(self, p):
        r = swaps.apply_swap(p)
        self.emit_event("active.changed", {})
        return r

    def active_revert(self, p):
        r = swaps.revert(int(p.get("index") or 0))
        self.emit_event("active.changed", {})
        return r

    def active_revert_all(self, p):
        r = swaps.revert_all()
        self.emit_event("active.changed", {})
        return r

    def qc_set(self, p):
        r = features.set_state("qc", {"entries": p.get("entries", [])})
        try:
            self.app.reload_qc()
        except Exception:
            pass
        return r

    def qc_enabled(self, p):
        result = features.set_state("qc", {"enabled": bool(p.get("value"))})
        if self.app:
            self.app.reload_qc()
        return result

    def qc_key(self, p):
        target = int(p.get("target", -1))
        vk = int(p.get("vk", 0))
        if target not in (0, 1, 2) or not 0 <= vk <= 255:
            raise ValueError("invalid chat target or keyboard key")
        keys = list(features.get("qc").get("keys") or [])
        while len(keys) < 3:
            keys.append({"label": "", "vk": 0})
        label = quickchat.key_label(vk)
        keys[target] = {"label": label, "vk": vk}
        features.set_state("qc", {"keys": keys})
        return {"label": label}

    def overlay_set(self, p):
        r = features.set_state("overlay", p)
        try:
            self.app.apply_overlays()
        except Exception:
            pass
        return r

    def tracker_set(self, p):
        current = features.get("tracker.settings")
        patch = {p["key"]: p.get("value")} if "key" in p else p
        if any(key not in current for key in patch):
            raise ValueError("Unknown tracker setting")
        result = features.set_state("tracker.settings", patch)
        if self.app:
            self.app.apply_overlays()
        return result

    def fps_set(self, p):
        r = features.set_state("fps", p)
        try:
            self.app.apply_overlays()
        except Exception:
            pass
        return r

    # ---------- dispatch ----------
    def dispatch(self, msg):
        typ = msg.get("type")
        payload = msg.get("payload") or {}
        mid = msg.get("id")
        fn = self._handlers.get(typ)
        try:
            if fn is None:
                raise ValueError("Not implemented yet: " + str(typ))
            n = len(inspect.signature(fn).parameters)
            res = fn(payload) if n > 0 else fn()
            if mid:
                self.app.send({"type": "reply", "id": mid, "payload": res if res is not None else {}})
        except Exception as e:
            if mid:
                self.app.send({"type": "reply", "id": mid, "error": str(e)})
            else:
                self.emit_event("app.toast", {"text": str(e), "ok": False})


def functions_title_clear():
    features.set_state("titles", {"titles": []})
    titles_config.refresh_webcache()
    return {"ok": True}





