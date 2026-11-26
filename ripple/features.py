import os, random, time, copy
from . import store

DEFAULTS = {
    "garage": {"camber": {"deg": 0, "lr": 0, "ud": 0}, "flipColor": -1,
               "headlights": [{"color": "#ffffff", "level": 10, "on": False}] * 2,
               "taillights": [{"color": "#ffffff", "level": 10, "on": False}] * 2,
               "underglow": [{"color": "#ffffff", "level": 10, "on": False}] * 2},
    "paint": {"plus": True, "rainbowSpeed": 20,
              "primary": [{"color": "#ffffff", "on": False, "rainbow": False}] * 2,
              "secondary": [{"color": "#ffffff", "on": False, "rainbow": False}] * 2,
              "trim": [{"color": "#ffffff", "on": False, "rainbow": False}] * 2,
              "wheels": [{"color": "#ffffff", "on": False, "rainbow": False}] * 2,
              "boost": [{"color": "#ffffff", "on": False, "rainbow": False}] * 2,
              "headlightsRainbow": [False, False], "taillightsRainbow": [False, False],
              "underglowRainbow": [False, False]},
    "match": {"alphaAll": False, "autoGG": False, "autoSaveReplay": False, "boostResetKey": "",
              "boostUsed": False, "plus": True, "statusOn": False, "statusPlus": True,
              "statusText": "", "unanonymize": False},
    "misc.match": {"binds": [{"label": "F8", "vk": 119}, {"label": "", "vk": 0},
                             {"label": "", "vk": 0}, {"label": "", "vk": 0}],
                   "frozen": False, "teamChangePrivateOnly": True},
    "ping": {"enabled": False, "max": 100, "min": 0, "plus": True, "random": False,
             "showOwn": True, "value": 0, "own": True},
    "platform": {"enabled": False, "goal": True, "hideSteamAvatars": False, "lobby": True,
                 "offsetX": 93, "offsetY": 0, "ranked": False,
                 "scoreboardBtnLbl": "", "scoreboardButton": 0, "scoreboardKey": "",
                 "sizePercent": 122, "steamActive": False, "steamAvatars": True,
                 "steamProfileUrl": "", "uiScale": 0.75, "displayScale": 0.95},
    "qc": {"enabled": True, "entries": [],
           "keys": [{"label": "", "vk": 0}, {"label": "", "vk": 0}, {"label": "", "vk": 0}]},
    "overlay": {"kbm": {"enabled": False, "layout": 0, "onlyInGame": False, "showUnfocused": False},
                "pad": {"controllerCount": 1, "enabled": False, "onlyInGame": False,
                        "showUnfocused": False, "skin": 0,
                        "skins": ["PlayStation 5 (DualSense)", "PlayStation 4 (Black)",
                                  "PlayStation 4 (Red/White)", "Xbox (White)"]}},
    "game": {"ballBounciness": 100, "ballCount": 1, "ballScale": 100, "carMaxSpeed": 100,
             "carScale": 100, "goalMessage": True, "gravity": -650, "hideRanks": False,
             "infiniteJump": False, "mmrPlaylist": "auto",
             "mutators": {k: "" for k in ("AudioRules", "BallGravity", "BallMaxSpeed", "BallType",
                                           "BallWeight", "BoostPadRespawnTime", "BoostRestriction",
                                           "BoosterStrengths", "DodgeInputTimer", "GameMode",
                                           "GameSpeed", "InputRestriction", "Rumble")},
             "showMMR": True, "showRank": True, "weather": "off"},
    "fakerank": {"active": False, "autoStart": False, "overrides": [],
                 "divisions": ["Division I", "Division II", "Division III", "Division IV"],
                 "reward": {"active": False, "level": 0, "wins": 0},
                 "rewardLevels": ["None", "Bronze", "Silver", "Gold", "Platinum", "Diamond",
                                  "Champion", "Grand Champion", "Supersonic Legend"],
                 "playlists": [{"id": i, "name": n} for i, n in (
                     (10, "Duel (1v1)"), (11, "Doubles (2v2)"), (13, "Standard (3v3)"),
                     (27, "Hoops"), (28, "Rumble"), (29, "Dropshot"), (30, "Snow Day"),
                     (34, "Tournaments"), (63, "Heatseeker"), (0, "Casual"))],
                 "tiers": ["Unranked", "Bronze I", "Bronze II", "Bronze III", "Silver I",
                           "Silver II", "Silver III", "Gold I", "Gold II", "Gold III",
                           "Platinum I", "Platinum II", "Platinum III", "Diamond I",
                           "Diamond II", "Diamond III", "Champion I", "Champion II",
                           "Champion III", "Grand Champion I", "Grand Champion II",
                           "Grand Champion III", "Supersonic Legend"]},
    "titles": {"canAdd": True, "enabled": True, "max": 3, "plus": True, "titles": []},
    "tracker": {"blue": 0, "blueTeam": [], "inMatch": False, "orange": 0, "orangeTeam": [],
                "playlist": "2v2", "rank": {"mmr": 0, "name": "Unranked", "tier": 0},
                "ranksHidden": False, "session": {"losses": 0, "streak": 0, "wins": 0}},
    "tracker.settings": {"ballDecimals": 2, "ballEnabled": False, "ballUnit": "kmh",
                         "overlayHotkey": "Tab", "overlayMode": "hidden", "overlayPad": 0,
                         "overlayPadName": "", "overlayPlaylist": "2v2", "overlayStyle": "compact",
                         "playlistHotkey": "Ctrl+G", "playlistPad": 0, "playlistPadName": "",
                         "rosterDisplayScale": 0.95, "rosterEnabled": False, "rosterHotkey": "Tab",
                         "rosterPad": 0, "rosterPadName": "", "rosterShowDivision": True,
                         "rosterShowMmr": True, "rosterShowPlaylist": True,
                         "rosterShowRank": True, "rosterUiScale": 0.75},
    "sounds": {"alphaBoost": False, "cues": [{"applied": "", "id": "flip_reset",
                                             "label": "Flip reset", "volume": 100}]},
    "rlsettings": {"account": "", "canUndo": False, "copies": [], "found": True,
                   "platform": "Epic", "running": False},
    "cust.palette": {"enabled": True, "plus": True},
    "cust.cameraCaps": {"enabled": False, "plus": True},
    "credits": {"amount": 0, "credits": 0, "enabled": False, "live": False, "plus": True,
                "shopPurchases": True, "tournament": 0, "tournamentCurrencyId": 0},
    "mp": {"active": False, "host": False, "map": "", "mapId": 0, "matchUp": False,
           "pseudo": "", "ready": True, "roomMap": ""},
    "invite": {"access": False, "armed": False},
    "fps": {"on": False},
    "spoof": {"friends": [], "running": False, "status": "Stopped"},
}

STADIUMS = None


def get(name):
    d = DEFAULTS.get(name)
    if d is None:
        raise ValueError("unknown feature: " + name)
    cur = store.read_json("feature_" + name.replace(".", "_") + ".json", None)
    if cur is None:
        return copy.deepcopy(d)
    out = copy.deepcopy(d)
    out.update(cur)
    return out


def set_state(name, patch):
    cur = get(name)
    if isinstance(patch, dict):
        cur.update(patch)
    store.write_json("feature_" + name.replace(".", "_") + ".json", cur)
    return {"ok": True, **cur}


def stadiums():
    global STADIUMS
    if STADIUMS is None:
        import json
        try:
            import os
            STADIUMS = json.load(open(os.path.join(store.DATA_DIR, "stadiums.json"), encoding="utf-8"))
        except Exception:
            STADIUMS = []
    return STADIUMS
