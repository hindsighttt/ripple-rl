#include "Home.h"

#include <windows.h>
#include <shellapi.h>

#include "Bootstrap.h"
#include "Features.h"
#include "Garage.h"
#include "PackagePatches.h"
#include "ProxyManager.h"
#include "Store.h"
#include "WinUtil.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {
const char* kVersion = "1.8.5";

bool IsDir(const std::string& p) {
    if (p.empty()) return false;
    std::error_code ec;
    return fs::is_directory(fs::path(p), ec);
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t z = s.find_last_not_of(" \t\r\n");
    return s.substr(a, z - a + 1);
}
std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string RegStr(HKEY root, const wchar_t* sub, const wchar_t* name) {
    HKEY k;
    if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return "";
    wchar_t buf[1024];
    DWORD sz = sizeof(buf), type = 0;
    LONG r = RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_SZ) return "";
    size_t chars = sz / sizeof(wchar_t);
    if (chars && buf[chars - 1] == L'\0') --chars;
    return win::Narrow(std::wstring(buf, chars));
}

std::string g_steam, g_epic;
bool g_steamDone = false, g_epicDone = false;

std::string SteamPath() {
    if (!g_steamDone) {
        std::string root = RegStr(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
        g_steam = root.empty()
                      ? ""
                      : (fs::path(root) / "steamapps" / "common" / "rocketleague" / "TAGame" / "CookedPCConsole").string();
        g_steamDone = true;
    }
    return g_steam;
}

std::string EpicPath() {
    if (!g_epicDone) {
        g_epicDone = true;
        try {
            std::ifstream f(R"(C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat)", std::ios::binary);
            if (f) {
                std::stringstream ss;
                ss << f.rdbuf();
                json d = json::parse(ss.str());
                for (auto& inst : d.value("InstallationList", json::array())) {
                    if (inst.value("AppName", std::string("")) == "Sugar") {
                        g_epic = (fs::path(inst.value("InstallLocation", std::string(""))) / "TAGame" / "CookedPCConsole").string();
                        break;
                    }
                }
            }
        } catch (...) {
        }
    }
    return g_epic;
}
}  // namespace

namespace home {

std::string SourcePath(const std::string& source) {
    json s = store::Settings();
    json target = s.value("target", json::object());
    json ip = s.value("installPaths", json::object());
    if (ip.is_object() && ip.contains(source) && ip[source].is_string() && !ip[source].get<std::string>().empty())
        return ip[source].get<std::string>();
    if (target.is_object() && target.value("source", std::string("")) == source) {
        std::string cd = target.value("cookedDir", std::string(""));
        if (!cd.empty()) return cd;
    }
    return source == "Steam" ? SteamPath() : EpicPath();
}

bool RlRunning() {
    static double last = -100.0;
    static bool cached = false;
    double now = (double)GetTickCount64() / 1000.0;
    if (now - last < 2.0) return cached;
    std::string out;
    win::RunCapture(L"tasklist /FI \"IMAGENAME eq RocketLeague.exe\"", out);
    cached = out.find("RocketLeague.exe") != std::string::npos;
    last = now;
    return cached;
}

json State() {
    json s = store::Settings();
    json tgt = s.value("target", json::object());
    json insts = json::array();
    for (const char* src : {"Epic", "Steam"}) {
        if (IsDir(SourcePath(src)))
            insts.push_back(json{{"source", src}, {"active", tgt.value("source", std::string("")) == src}});
    }
    json hist;
    if (s.contains("pseudoHistory") && s["pseudoHistory"].is_array()) {
        hist = s["pseudoHistory"];
    } else {
        hist = json::array();
        std::string ps = s.value("pseudo", std::string(""));
        if (!ps.empty()) hist.push_back(ps);
    }
    return json{{"accountName", s.value("rlAccountName", std::string(""))},
                {"active", tgt.value("source", std::string(""))},
                {"installs", insts},
                {"launchBusy", false},
                {"pseudo", s.value("pseudo", std::string(""))},
                {"pseudoHistory", hist},
                {"playtimeBaseSeconds", 0},
                {"ready", true},
                {"rlRunning", RlRunning()},
                {"segmentElapsedSeconds", 0},
                {"steamAvatarUrl", ""},
                {"steamUrl", ""},
                {"version", kVersion}};
}

json SetSourcePath(const json& p) {
    std::string source = p.value("source", "Epic");
    if (source != "Steam" && source != "Epic") throw std::runtime_error("Unknown source");
    std::string path;
    if (p.contains("path") && p["path"].is_string())
        path = p["path"].get<std::string>();
    else if (p.contains("value") && p["value"].is_string())
        path = p["value"].get<std::string>();
    if (path.empty()) return json{{"cancelled", true}};  // native backend has no file dialog

    std::vector<fs::path> cands = {fs::path(path), fs::path(path) / "TAGame" / "CookedPCConsole",
                                   fs::path(path) / "CookedPCConsole"};
    fs::path cooked;
    std::error_code ec;
    for (auto& c : cands) {
        if (fs::is_directory(c, ec) && Lower(c.filename().string()) == "cookedpcconsole") {
            cooked = fs::absolute(c, ec);
            break;
        }
    }
    if (cooked.empty())
        throw std::runtime_error("Choose the Rocket League install or its CookedPCConsole folder");

    json ip = store::Settings().value("installPaths", json::object());
    if (!ip.is_object()) ip = json::object();
    ip[source] = cooked.string();
    store::SaveSettings(json{{"installPaths", ip}, {"target", json{{"source", source}, {"cookedDir", cooked.string()}}}});
    return json{{"ok", true}, {"path", cooked.string()}};
}

json DetectSource(const json& p) {
    std::string source = p.value("source", "Steam");
    std::string path = SourcePath(source);
    if (!IsDir(path)) throw std::runtime_error("Unknown source");
    return SetSourcePath(json{{"source", source}, {"path", path}});
}

json SetInstall(const json& p) {
    std::string source = p.value("source", "Epic");
    std::string path = (p.contains("path") && p["path"].is_string()) ? p["path"].get<std::string>() : SourcePath(source);
    return SetSourcePath(json{{"source", source}, {"path", path}});
}

json SetPseudo(const json& p) {
    std::string v = Trim(p.value("value", std::string("")));
    json s = store::Settings();
    std::vector<std::string> order;
    if (!v.empty()) order.push_back(v);
    if (s.contains("pseudoHistory") && s["pseudoHistory"].is_array())
        for (auto& x : s["pseudoHistory"])
            if (x.is_string()) order.push_back(x.get<std::string>());
    std::string cur = s.value("pseudo", std::string(""));
    if (!cur.empty()) order.push_back(cur);

    json hist = json::array();
    std::set<std::string> seen;
    for (auto& x : order) {
        if (seen.insert(x).second) {
            hist.push_back(x);
            if (hist.size() >= 50) break;
        }
    }
    store::SaveSettings(json{{"pseudo", v}, {"pseudoHistory", hist}});
    return json{{"ok", true}, {"value", v}, {"history", hist}};
}

json ForgetPseudo(const json& p) {
    json s = store::Settings();
    bool hasName = p.contains("value") && !p["value"].is_null();
    std::string name = (hasName && p["value"].is_string()) ? p["value"].get<std::string>() : "";
    json src;
    if (s.contains("pseudoHistory") && s["pseudoHistory"].is_array())
        src = s["pseudoHistory"];
    else {
        src = json::array();
        std::string ps = s.value("pseudo", std::string(""));
        if (!ps.empty()) src.push_back(ps);
    }
    json hist = json::array();
    for (auto& x : src)
        if (x.is_string()) {
            std::string xs = x.get<std::string>();
            if (!xs.empty() && xs != name) hist.push_back(xs);
        }
    json patch = {{"pseudoHistory", hist}};
    if (!hasName) patch["pseudo"] = "";
    store::SaveSettings(patch);
    return json{{"ok", true}, {"history", hist}};
}

void OpenBackupFolder() {
    std::wstring dir = store::BackupDir().wstring();
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

json Launch(const json& p, ProxyManager& proxy) {
    if (RlRunning()) throw std::runtime_error("Close Rocket League before preparing a new launch.");
    fs::path cooked = store::CookedDir();
    if (cooked.empty()) throw std::runtime_error("Set your Rocket League install in Settings first.");
    fs::path root = cooked.parent_path().parent_path();
    fs::path exe = root / "Binaries" / "Win64" / "RocketLeague.exe";
    std::error_code ec;
    if (!fs::exists(exe, ec)) throw std::runtime_error("RocketLeague.exe not found");

    const auto patchResults = package_patches::Apply(cooked);
    const auto gp = store::GaragePatchDir();
    const bool hasGarage = garage::HasPatchData(gp);
    const auto applyGarage = [&] {
        const auto result = garage::Apply(cooked, gp, store::GarageBackupDir());
        store::WriteJson("garage-patch.json", json{{"cookedDir", cooked.string()}, {"result", result}});
    };
    if (hasGarage && gp == store::GarageRecipeDir()) applyGarage();
    json s = store::Settings();
    if (s.value("proxyEnabled", true)) {
        proxy.Start();
        json prep = bootstrap::Prepare(cooked, store::DataDir(), store::BackupDir());
        if (prep.value("changed", false)) {
            json st = prep;
            st["cookedDir"] = cooked.string();
            store::WriteJson("proxy-bootstrap.json", st);
        }
    }
    proxy.PatchWebcache();
    // Register enabled custom titles in the cached PlayerTitleConfig before RL
    // reads it. Safe when the proxy is disabled; the addon also re-applies it.
    try {
        proxy.RefreshTitlesCache();
    } catch (...) {
    }

    // Legacy captured sets retain their existing best-effort launch behavior.
    if (hasGarage && gp != store::GarageRecipeDir()) {
        try {
            applyGarage();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "garage patch: %s\n", e.what());
        }
    }
    // Flip-reset colour patch.
    long long flipColour = -1;
    {
        json g = features::Get("garage");
        if (g.contains("flipColor") && g["flipColor"].is_number()) flipColour = g["flipColor"].get<long long>();
    }
    if (hasGarage) {
        flip::Prepare(cooked, gp, store::BackupDir(), flipColour);
    } else if (flipColour != -1) {
        throw std::runtime_error("Install this build's garage patch data before setting flip-reset colour.");
    }

    std::string src = s.value("target", json::object()).value("source", std::string(""));
    bool noEac = p.value("noEac", false);
    const bool chooseInEpic = src == "Epic" && noEac;
    if (src == "Epic") {
        // A direct game start can redirect through Epic and lose the selected mode.
        const wchar_t* uri = chooseInEpic ? L"com.epicgames.launcher://store/library"
                                         : L"com.epicgames.launcher://apps/Sugar?action=launch&silent=true";
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            throw std::runtime_error("Could not open the Epic Games Launcher.");
    } else {
        std::wstring cmd = L"\"" + exe.wstring() + L"\"";
        if (noEac) cmd += L" -noeac";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring dir = exe.parent_path().wstring();
        std::wstring mut = cmd;
        if (!CreateProcessW(nullptr, mut.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            dir.c_str(), &si, &pi))
            throw std::runtime_error("Could not start Rocket League.");
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return json{{"ok", true}, {"chooseInEpic", chooseInEpic}, {"packagePatches", patchResults}};
}

}  // namespace home
