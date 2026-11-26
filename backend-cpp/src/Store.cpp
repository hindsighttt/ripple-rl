#include "Store.h"

#include "WinUtil.h"

#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;

namespace {
std::mutex g_mtx;
fs::path g_app, g_data, g_state, g_backup;
bool g_init = false;

// Mirror store.py's APP_DIR: the folder that contains ripple\ and data\.
// In the native layout the exe lives under backend-cpp\build\bin, so walk up
// until we find that root. RIPPLE_RL_APP_DIR overrides for odd setups.
fs::path FindAppRoot() {
    std::wstring env = win::GetEnvW(L"RIPPLE_RL_APP_DIR");
    if (!env.empty() && fs::exists(fs::path(env) / "ripple")) return fs::path(env);
    fs::path d = win::ExeDir();
    for (int i = 0; i < 8; ++i) {
        if (fs::exists(d / "ripple") && fs::exists(d / "data")) return d;
        if (d.has_parent_path() && d.parent_path() != d)
            d = d.parent_path();
        else
            break;
    }
    return fs::current_path();
}
}  // namespace

namespace store {

void Init() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_init) return;
    g_app = FindAppRoot();
    g_data = g_app / "data";
    std::wstring appdata = win::GetEnvW(L"APPDATA");
    g_state = (appdata.empty() ? g_app : fs::path(appdata)) / "RippleRL";
    g_backup = g_state / "backups";
    std::error_code ec;
    fs::create_directories(g_state, ec);
    fs::create_directories(g_backup, ec);
    fs::create_directories(g_state / "packs", ec);
    fs::create_directories(g_state / "workshop", ec);
    g_init = true;
}

const fs::path& AppDir() { return g_app; }
const fs::path& DataDir() { return g_data; }
const fs::path& StateDir() { return g_state; }
const fs::path& BackupDir() { return g_backup; }

json ReadJson(const std::string& name, const json& def) {
    std::ifstream f(g_state / name, std::ios::binary);
    if (!f) return def;
    try {
        std::stringstream ss;
        ss << f.rdbuf();
        return json::parse(ss.str());
    } catch (...) {
        return def;
    }
}

void WriteJson(const std::string& name, const json& obj, int indent) {
    std::lock_guard<std::mutex> lk(g_mtx);
    fs::path tmp = g_state / (name + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << obj.dump(indent);
    }
    std::error_code ec;
    fs::rename(tmp, g_state / name, ec);
    if (ec) {  // destination exists on some filesystems; replace it
        fs::remove(g_state / name, ec);
        fs::rename(tmp, g_state / name, ec);
    }
}

json DefaultSettings() {
    return json{{"language", "en"},
                {"theme", "dark"},
                {"firstRunComplete", true},
                {"library", json::object()},
                {"target", json::object()},
                {"bringToFrontHotkey", "F2"},
                {"alwaysOnTop", false},
                {"closeToTray", true},
                {"discordRpc", true},
                {"startup", false},
                {"backup", true},
                {"proxyEnabled", true}};
}

json Settings() {
    json s = ReadJson("settings.json", json::object());
    json out = DefaultSettings();
    if (s.is_object())
        for (auto& [k, v] : s.items()) out[k] = v;
    return out;
}

void SaveSettings(const json& patch) {
    json cur = Settings();
    if (patch.is_object())
        for (auto& [k, v] : patch.items()) cur[k] = v;
    WriteJson("settings.json", cur);
}

fs::path CookedDir() {
    json t = Settings().value("target", json::object());
    if (t.is_object() && t.contains("cookedDir") && t["cookedDir"].is_string()) {
        fs::path p = t["cookedDir"].get<std::string>();
        std::error_code ec;
        if (fs::is_directory(p, ec)) return p;
    }
    return fs::path();
}

fs::path GaragePatchDir() {
    const auto captures = g_backup / "ground-truth";
    const auto verified = captures / "verified-epic";
    const auto target = Settings().value("target", json::object());
    if (target.is_object() && target.value("source", std::string()) == "Epic") {
        if (fs::is_regular_file(DataDir() / "garage-recipe.json")) return GarageRecipeDir();
        if (fs::is_regular_file(verified / "patch-manifest.json")) return verified;
    }
    return captures;
}

fs::path GarageRecipeDir() { return g_backup / "garage-recipe-epic-20261126"; }

fs::path GarageBackupDir() {
    const auto patchDir = GaragePatchDir();
    if (patchDir == GarageRecipeDir()) return patchDir / "before";
    return g_backup / (patchDir.filename() == "verified-epic" ? "garage-verified-epic" : "garage");
}
fs::path PacksDir() { return g_state / "packs"; }
fs::path WorkshopDir() { return g_state / "workshop"; }

std::string IsoNow() {
    std::time_t t = std::time(nullptr);
    std::tm g{};
    gmtime_s(&g, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &g);
    return buf;
}

}  // namespace store
