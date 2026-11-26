#include "Dispatcher.h"

#include "Catalog.h"
#include "Features.h"
#include "Garage.h"
#include "Home.h"
#include "IpcServer.h"
#include "PackagePatches.h"
#include "Packs.h"
#include "Presets.h"
#include "ProxyManager.h"
#include "Quickchat.h"
#include "Spawn.h"
#include "Store.h"
#include "Swaps.h"
#include "WinUtil.h"
#include "Workshop.h"

#include <exception>
#include <stdexcept>

namespace {
const char* kVersion = "1.8.5";
constexpr char kRouletteStateFile[] = "feature_roulette.json";  // shared with garage_wire.py

// bool(p["value"]) with Python-ish truthiness for the common json types.
bool BoolVal(const json& p, const char* key) {
    auto it = p.find(key);
    if (it != p.end()) {
        if (it->is_boolean()) return it->get<bool>();
        if (it->is_number()) return it->get<double>() != 0;
    }
    return false;
}
json Field(const json& p, const char* key) {
    auto it = p.find(key);
    return it != p.end() ? *it : json(nullptr);
}
std::string SG(const json& p, const char* key) {
    auto it = p.find(key);
    return (it != p.end() && it->is_string()) ? it->get<std::string>() : "";
}
std::string SlugOrName(const json& p) {
    std::string s = SG(p, "slug");
    return !s.empty() ? s : SG(p, "name");
}
}  // namespace

json Dispatcher::CustomApply(const json& p, const std::string& kindOverride) {
    std::string kind = kindOverride.empty() ? SG(p, "kind") : kindOverride;
    std::string name = SlugOrName(p);
    std::exception_ptr ep;
    json r;
    try {
        r = packs::ApplyPack(kind, name);
    } catch (...) {
        ep = std::current_exception();
    }
    if (kind == "ball") Emit("custom.ball.finished", json::object());  // mirrors bridge's finally
    if (ep) std::rethrow_exception(ep);
    return r;
}

Dispatcher::Dispatcher(IpcServer* server, ProxyManager* proxy) : server_(server), proxy_(proxy) {
    Register();
}

void Dispatcher::Emit(const std::string& type, const json& payload) {
    server_->Broadcast(json{{"type", type}, {"payload", payload}});
}

void Dispatcher::Register() {
    // ---- app / settings ----
    handlers_["app.state"] = [](const json&) {
        json s = store::Settings();
        return json{{"version", kVersion}, {"licenseType", "plus"},
                    {"itemCount", (long long)catalog::CatalogItemCount()},
                    {"lang", s.value("language", "en")}, {"theme", s.value("theme", "dark")},
                    {"devBuild", false}, {"inviteAccess", false}, {"catalogBuilding", false},
                    {"maximized", false}};
    };
    handlers_["app.setTheme"] = [](const json& p) {
        std::string v = p.contains("theme") ? p.value("theme", "dark") : p.value("value", "dark");
        if (p.contains("dark")) v = p["dark"].get<bool>() ? "dark" : "light";
        store::SaveSettings(json{{"theme", v}});
        return json{{"ok", true}};
    };
    handlers_["app.setLang"] = [](const json& p) {
        std::string v = p.contains("lang") ? p.value("lang", "en") : p.value("value", "en");
        store::SaveSettings(json{{"language", v}});
        return json{{"ok", true}};
    };
    handlers_["settings.get"] = [](const json&) {
        json s = store::Settings();
        json tgt = s.value("target", json::object());
        return json{{"activeSource", tgt.value("source", "")},
                    {"alwaysOnTop", s.value("alwaysOnTop", false)},
                    {"bringToFrontHotkey", s.value("bringToFrontHotkey", "F2")},
                    {"closeToTray", s.value("closeToTray", false)},
                    {"discordRpc", s.value("discordRpc", true)},
                    {"epicPath", ""},
                    {"steamPath", ""},
                    {"startup", s.value("startup", false)}};
    };
    handlers_["settings.setBringToFront"] = [](const json& p) {
        if (p.value("key", std::string("bringToFrontHotkey")) == "bringToFrontHotkey")
            store::SaveSettings(json{{"bringToFrontHotkey", p.value("value", "F2")}});
        return json{{"ok", true}};
    };
    handlers_["settings.setCloseToTray"] = [](const json& p) {
        store::SaveSettings(json{{"closeToTray", BoolVal(p, "value")}});
        return json{{"ok", true}};
    };
    handlers_["settings.setAlwaysOnTop"] = [](const json& p) {
        store::SaveSettings(json{{"alwaysOnTop", BoolVal(p, "value")}});
        return json{{"ok", true}};
    };

    // ---- catalog ----
    handlers_["items.categories"] = [](const json&) { return catalog::Categories(); };
    handlers_["catalog.titles"] = [](const json&) { return catalog::Titles(); };
    handlers_["items.list"] = [](const json& p) { return catalog::ListItems(p); };
    handlers_["items.inventorySync"] = [](const json&) {
        return json{{"ok", true}, {"inventoryKnown", true}, {"inventoryCount", (long long)catalog::InvCount()}};
    };

    // ---- home / install / launch ----
    handlers_["home.state"] = [](const json&) { return home::State(); };
    handlers_["home.setPseudo"] = [](const json& p) { return home::SetPseudo(p); };
    handlers_["home.forgetPseudo"] = [](const json& p) { return home::ForgetPseudo(p); };
    handlers_["home.setInstall"] = [this](const json& p) {
        json r = home::SetInstall(p);
        Emit("home.changed", json::object());
        return r;
    };
    handlers_["settings.setSourcePath"] = [this](const json& p) {
        json r = home::SetSourcePath(p);
        Emit("home.changed", json::object());
        return r;
    };
    handlers_["settings.detectSourcePath"] = [this](const json& p) {
        json r = home::DetectSource(p);
        Emit("home.changed", json::object());
        return r;
    };
    handlers_["settings.openBackupFolder"] = [](const json&) {
        home::OpenBackupFolder();
        return json::object();
    };
    handlers_["settings.refreshCatalog"] = [](const json&) { return json{{"ok", true}}; };
    handlers_["settings.repair"] = [](const json&) { return json{{"ok", true}}; };
    handlers_["home.launch"] = [this](const json& p) {
        json r = home::Launch(p, *proxy_);
        Emit("home.launchResult", json{{"error", ""}, {"chooseInEpic", r.value("chooseInEpic", false)}});
        return r;
    };

    // ---- item swaps (UPK/AES) ----
    handlers_["items.swap"] = [this](const json& p) {
        json r = swaps::ApplySwap(p);
        Emit("active.changed", json::object());
        return r;
    };
    handlers_["items.paint"] = [](const json& p) { return swaps::PaintItem(p); };
    handlers_["active.list"] = [](const json&) { return swaps::ListActive(); };
    handlers_["active.revert"] = [this](const json& p) {
        long long i = (p.contains("index") && p["index"].is_number()) ? p["index"].get<long long>() : 0;
        json r = swaps::Revert(i);
        Emit("active.changed", json::object());
        return r;
    };
    handlers_["active.revertAll"] = [this](const json&) {
        json r = swaps::RevertAll();
        Emit("active.changed", json::object());
        return r;
    };
    handlers_["active.copyToOther"] = [](const json&) { return swaps::CopyToOther(); };

    // ---- garage / flip UPK patches ----
    handlers_["garage.patch.status"] = [](const json&) { return garage::StatusCmd(); };
    handlers_["garage.patch.apply"] = [](const json&) { return garage::ApplyCmd(); };
    handlers_["garage.patch.revert"] = [](const json&) { return garage::RevertCmd(); };
    handlers_["packagePatches.get"] = [](const json&) { return package_patches::Get(); };
    handlers_["packagePatches.set"] = [](const json& p) { return package_patches::Set(p); };
    handlers_["packagePatches.apply"] = [](const json&) {
        const auto results = package_patches::Apply(store::CookedDir());
        auto view = package_patches::Get();
        view["results"] = results;
        return view;
    };

    // ---- workshop maps ----
    handlers_["workshop.list"] = [](const json&) { return workshop::ListMaps(); };
    handlers_["workshop.refresh"] = [](const json&) { return json{{"ok", true}}; };
    handlers_["workshop.import"] = [](const json& p) { return workshop::ImportMap(SG(p, "path"), SG(p, "name")); };
    handlers_["workshop.apply"] = [](const json& p) { return workshop::ApplyMap(SG(p, "name")); };
    handlers_["workshop.play"] = [](const json&) { return json{{"ok", true}}; };
    handlers_["workshop.restore"] = [](const json&) { return workshop::RestoreMap(); };
    handlers_["workshop.delete"] = [](const json& p) { return workshop::DeleteMap(SG(p, "name")); };
    handlers_["workshop.activate"] = [](const json&) { return json{{"ok", true}}; };
    handlers_["workshop.download"] = [](const json&) { return json{{"ok", true}}; };

    // ---- custom packs ----
    handlers_["custom.list"] = [](const json& p) { return packs::ListPacks(SG(p, "kind")); };
    handlers_["custom.import"] = [](const json& p) -> json {
        std::string path = SG(p, "path");
        if (path.empty()) return json{{"cancelled", true}};
        std::string kind = SG(p, "kind");
        json r = packs::ImportZip(kind, path);
        if (kind == "car") {
            r["cars"] = json::array({json{{"name", r.value("name", "")}, {"slug", r.value("slug", "")}}});
            r["skipped"] = 0;
        }
        return r;
    };
    handlers_["custom.remove"] = [](const json& p) { return packs::Remove(SG(p, "kind"), SlugOrName(p)); };
    handlers_["custom.rename"] = [](const json& p) {
        std::string nn = SG(p, "newName");
        if (nn.empty()) nn = SG(p, "name");
        return packs::Rename(SG(p, "kind"), SlugOrName(p), nn);
    };
    handlers_["custom.apply"] = [this](const json& p) { return CustomApply(p, ""); };
    for (const std::string kind : {"ball", "car", "decal", "hud"}) {
        handlers_["custom." + kind + ".apply"] = [this, kind](const json& p) { return CustomApply(p, kind); };
        handlers_["custom." + kind + ".restore"] = [kind](const json& p) { return packs::RestorePack(kind, SlugOrName(p)); };
    }
    handlers_["custom.car.revert"] = [](const json& p) { return packs::RestorePack("car", SlugOrName(p)); };
    handlers_["custom.decal.revertOne"] = [](const json& p) { return packs::RestorePack("decal", SlugOrName(p)); };

    // ---- presets ----
    handlers_["presets.list"] = [](const json&) { return presets::List(); };
    handlers_["presets.new"] = [this](const json& p) {
        json r = presets::New(p);
        Emit("presets.changed", json::object());
        return r;
    };
    handlers_["presets.rename"] = [this](const json& p) {
        json r = presets::Rename(p);
        Emit("presets.changed", json::object());
        return r;
    };
    handlers_["presets.duplicate"] = [this](const json& p) {
        json r = presets::Duplicate(p);
        Emit("presets.changed", json::object());
        return r;
    };
    handlers_["presets.delete"] = [](const json& p) { return presets::Delete(p); };
    handlers_["presets.load"] = [](const json& p) { return presets::Load(p); };
    handlers_["presets.export"] = [](const json& p) { return presets::Export(p); };
    handlers_["presets.import"] = [](const json& p) { return presets::Import(p); };

    // ---- spawn list ----
    handlers_["spawn.status"] = [](const json&) { return spawn::Status(); };
    handlers_["spawn.list"] = [](const json&) { return spawn::ListSpawn(); };
    handlers_["spawn.add"] = [](const json& p) { return spawn::Add(p); };
    handlers_["spawn.addMany"] = [](const json& p) { return spawn::AddMany(p); };
    handlers_["spawn.addList"] = [](const json& p) { return spawn::AddList(p); };
    handlers_["spawn.addTitle"] = [](const json& p) { return spawn::AddTitle(p); };
    handlers_["spawn.remove"] = [](const json& p) { return spawn::Remove(p); };
    handlers_["spawn.removeMany"] = [](const json& p) { return spawn::RemoveMany(p); };
    handlers_["spawn.clear"] = [](const json&) { return spawn::Clear(); };
    handlers_["spawn.paints"] = [](const json&) { return spawn::Paints(); };
    handlers_["spawn.paintable"] = [](const json& p) { return spawn::Paintable(p); };
    handlers_["spawn.titlesOwned"] = [](const json&) { return spawn::TitlesOwned(); };
    handlers_["spawn.getLoadoutDelay"] = [](const json&) { return json{{"delay", 1200}}; };
    handlers_["spawn.setLoadoutDelay"] = [](const json&) { return json{{"ok", true}}; };
    handlers_["spawn.getLoadoutOnlyHold"] = [](const json&) { return json{{"hold", false}}; };
    handlers_["spawn.setLoadoutOnlyHold"] = [](const json&) { return json{{"ok", true}}; };
    handlers_["spawn.sendPriRpc"] = [](const json&) { return json{{"ok", true}}; };
    handlers_["spawn.spectate"] = [](const json&) { return json{{"ok", true}}; };
    handlers_["spawn.joinTeam"] = [](const json&) { return json{{"ok", true}}; };

    // ---- quickchat ----
    handlers_["misc.qc.setEnabled"] = [this](const json& p) {
        json r = features::SetState("qc", json{{"enabled", BoolVal(p, "value")}});
        if (qcReload_) qcReload_();
        return r;
    };
    handlers_["misc.qc.setEntries"] = [this](const json& p) {
        json entries = (p.contains("entries") && p["entries"].is_array()) ? p["entries"] : json::array();
        json r = features::SetState("qc", json{{"entries", entries}});
        if (qcReload_) qcReload_();
        return r;
    };
    handlers_["misc.qc.setKey"] = [this](const json& p) {
        long long target = p.value("target", (long long)-1);
        long long vk = p.value("vk", (long long)0);
        if (!(target == 0 || target == 1 || target == 2) || !(vk >= 0 && vk <= 255))
            throw std::runtime_error("invalid chat target or keyboard key");
        json qc = features::Get("qc");
        json keys = qc.value("keys", json::array());
        if (!keys.is_array()) keys = json::array();
        while (keys.size() < 3) keys.push_back(json{{"label", ""}, {"vk", 0}});
        std::string label = quickchat::KeyLabel(vk);
        keys[(size_t)target] = json{{"label", label}, {"vk", vk}};
        features::SetState("qc", json{{"keys", keys}});
        if (qcReload_) qcReload_();
        return json{{"label", label}};
    };
    handlers_["misc.qc.labels"] = [](const json& p) {
        json labels = json::array();
        if (p.contains("codes") && p["codes"].is_array())
            for (auto& c : p["codes"])
                if (c.is_number()) labels.push_back(quickchat::KeyLabel(c.get<long long>()));
        return json{{"labels", labels}};
    };
    handlers_["misc.qc.scan"] = [](const json&) { return json{{"ok", true}}; };

    // ---- titles.save (social) ----
    handlers_["titles.save"] = [](const json& p) {
        json f = features::Get("titles");
        std::string tid;
        if (p.contains("titleId") && p["titleId"].is_string() && !p["titleId"].get<std::string>().empty())
            tid = p["titleId"].get<std::string>();
        else
            tid = "CustomTitle_" + win::RandomHex(6);
        std::string text = (p.contains("text") && p["text"].is_string()) ? p["text"].get<std::string>() : "";
        size_t a = text.find_first_not_of(" \t\r\n");
        text = (a == std::string::npos) ? "" : text.substr(a, text.find_last_not_of(" \t\r\n") - a + 1);
        if (text.empty()) throw std::runtime_error("title text required");
        json rows = json::array();
        if (f.contains("titles") && f["titles"].is_array())
            for (auto& t : f["titles"])
                if (t.value("id", std::string("")) != tid) rows.push_back(t);
        rows.push_back(json{{"id", tid},
                            {"text", text},
                            {"color", (p.contains("color") && p["color"].is_string()) ? p["color"].get<std::string>() : ""},
                            {"glow", (p.contains("glow") && p["glow"].is_string()) ? p["glow"].get<std::string>() : ""},
                            {"custom", true}});
        features::SetState("titles", json{{"titles", rows}});
        return json{{"ok", true}, {"id", tid}};
    };

    // ---- feature getters ----
    auto getter = [this](const char* command, const char* feature) {
        handlers_[command] = [feature](const json&) { return features::Get(feature); };
    };
    getter("titles.get", "titles");
    getter("fakerank.get", "fakerank");
    getter("misc.ping.get", "ping");
    getter("misc.platform.get", "platform");
    getter("misc.qc.get", "qc");
    getter("misc.match.get", "misc.match");
    getter("overlay.get", "overlay");
    getter("garage.get", "garage");
    getter("paint.get", "paint");
    getter("match.get", "match");
    getter("game.get", "game");
    getter("cust.palette.get", "cust.palette");
    getter("cust.cameraCaps.get", "cust.cameraCaps");
    getter("credits.get", "credits");
    getter("tracker.state", "tracker");
    getter("tracker.settings", "tracker.settings");
    getter("sounds.list", "sounds");
    getter("rlsettings.get", "rlsettings");
    getter("fps.get", "fps");
    getter("spoof.get", "spoof");
    getter("invite.get", "invite");

    // ---- feature setters that persist the whole payload as a patch ----
    auto setWhole = [this](const char* command, const char* feature) {
        handlers_[command] = [feature](const json& p) { return features::SetState(feature, p); };
    };
    setWhole("misc.ping.set", "ping");
    setWhole("misc.platform.set", "platform");
    setWhole("misc.match.setKey", "misc.match");
    setWhole("garage.set", "garage");
    setWhole("paint.set", "paint");
    setWhole("match.set", "match");
    setWhole("game.set", "game");
    setWhole("cust.palette.set", "cust.palette");
    setWhole("cust.cameraCaps.set", "cust.cameraCaps");

    handlers_["roulette.get"] = [](const json&) {
        const json on = Field(store::ReadJson(kRouletteStateFile, json::object()), "on");
        return json{{"on", on != false}};
    };
    handlers_["roulette.set"] = [](const json& p) {
        const json on = Field(p, "on");
        if (!on.is_boolean())
            throw std::runtime_error("Expected a boolean roulette value");
        const json saved{{"on", on}};
        store::WriteJson(kRouletteStateFile, saved);
        if (store::ReadJson(kRouletteStateFile, nullptr) != saved)
            throw std::runtime_error("Could not save the roulette setting");
        return json{{"ok", true}, {"on", on}};
    };

    // overlay.set / fps.set also re-apply overlay visibility.
    handlers_["overlay.set"] = [this](const json& p) {
        json r = features::SetState("overlay", p);
        if (overlayApply_) overlayApply_();
        return r;
    };
    handlers_["fps.set"] = [this](const json& p) {
        json r = features::SetState("fps", p);
        if (overlayApply_) overlayApply_();
        return r;
    };

    // ---- feature setters for a single boolean "value" field ----
    auto setBool = [this](const char* command, const char* feature, const char* field) {
        handlers_[command] = [feature, field](const json& p) {
            return features::SetState(feature, json{{field, BoolVal(p, "value")}});
        };
    };
    setBool("titles.setEnabled", "titles", "enabled");
    setBool("fakerank.setActive", "fakerank", "active");
    setBool("fakerank.setAutoStart", "fakerank", "autoStart");
    setBool("misc.ping.own.set", "ping", "own");
    setBool("misc.match.freeze", "misc.match", "frozen");
    setBool("invite.arm", "invite", "armed");
    setBool("sounds.setAlphaBoost", "sounds", "alphaBoost");

    // ---- feature setters with bespoke shapes ----
    handlers_["fakerank.setReward"] = [](const json& p) {
        return features::SetState("fakerank", json{{"reward", p}});
    };
    handlers_["spoof.setFriends"] = [](const json& p) {
        json fr = p.contains("friends") ? p["friends"] : json::array();
        return features::SetState("spoof", json{{"friends", fr}});
    };
    handlers_["tracker.resetSession"] = [](const json&) {
        return features::SetState(
            "tracker", json{{"session", json{{"losses", 0}, {"streak", 0}, {"wins", 0}}}});
    };
    handlers_["tracker.set"] = [this](const json& p) {
        json cur = features::Get("tracker.settings");
        json patch;
        if (p.contains("key"))
            patch[p["key"].get<std::string>()] = Field(p, "value");
        else
            patch = p;
        if (patch.is_object())
            for (auto& [k, v] : patch.items())
                if (!cur.contains(k)) throw std::runtime_error("Unknown tracker setting");
        json r = features::SetState("tracker.settings", patch);
        if (overlayApply_) overlayApply_();
        return r;
    };

    // fakerank overrides: replace the row for this playlist (match != comparison).
    auto rewriteOverrides = [](const json& p, bool append) {
        json f = features::Get("fakerank");
        json pv = Field(p, "playlist");
        json rows = json::array();
        auto ov = f.find("overrides");
        if (ov != f.end() && ov->is_array())
            for (auto& r : *ov) {
                json rv = r.contains("playlist") ? r["playlist"] : json(nullptr);
                if (rv != pv) rows.push_back(r);
            }
        if (append) rows.push_back(p);
        features::SetState("fakerank", json{{"overrides", rows}});
        return json{{"ok", true}};
    };
    handlers_["fakerank.setOverride"] = [rewriteOverrides](const json& p) { return rewriteOverrides(p, true); };
    handlers_["fakerank.clearOverride"] = [rewriteOverrides](const json& p) { return rewriteOverrides(p, false); };

    handlers_["titles.remove"] = [](const json& p) {
        json f = features::Get("titles");
        json pid = Field(p, "id");
        json rows = json::array();
        auto ts = f.find("titles");
        if (ts != f.end() && ts->is_array())
            for (auto& t : *ts) {
                json tid = t.contains("id") ? t["id"] : json(nullptr);
                if (tid != pid) rows.push_back(t);
            }
        features::SetState("titles", json{{"titles", rows}});
        return json{{"ok", true}};
    };
    handlers_["titles.removeAll"] = [](const json&) {
        features::SetState("titles", json{{"titles", json::array()}});
        return json{{"ok", true}};
    };

    // ---- composite reads ----
    handlers_["mp.state"] = [](const json&) {
        json m = features::Get("mp");
        m["pseudo"] = store::Settings().value("pseudo", "");
        return m;
    };
    handlers_["stadium.get"] = [](const json&) {
        json teams = json::array();
        for (int i = 0; i < 2; ++i)
            teams.push_back(json{{"color", "#ffffff"}, {"on", false}, {"rainbow", false}});
        return json{{"maps", features::Stadiums()}, {"plus", true}, {"teams", teams},
                    {"menuMap", ""}, {"rainbowSpeed", 20}};
    };
}

void Dispatcher::Dispatch(const json& msg) {
    std::string type = msg.value("type", "");
    json payload = (msg.contains("payload") && msg["payload"].is_object()) ? msg["payload"] : json::object();
    bool hasId = msg.contains("id") && !msg["id"].is_null();
    json id = hasId ? msg["id"] : json();

    try {
        // Window-chrome shims: the native window owns its own chrome, so just
        // forward the intent as an event.
        if (type == "win.minimize" || type == "win.maximize" || type == "win.close") {
            Emit("win.command", json{{"action", type.substr(4)}});
            if (hasId) server_->Broadcast(json{{"type", "reply"}, {"id", id}, {"payload", json::object()}});
            return;
        }
        auto it = handlers_.find(type);
        if (it == handlers_.end()) throw std::runtime_error("Not implemented yet: " + type);
        json res = it->second(payload);
        if (hasId)
            server_->Broadcast(
                json{{"type", "reply"}, {"id", id}, {"payload", res.is_null() ? json::object() : res}});
    } catch (const std::exception& e) {
        if (hasId)
            server_->Broadcast(json{{"type", "reply"}, {"id", id}, {"error", std::string(e.what())}});
        else
            Emit("app.toast", json{{"text", std::string(e.what())}, {"ok", false}});
    }
}
