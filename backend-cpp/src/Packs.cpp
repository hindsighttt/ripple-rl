#include "Packs.h"

#include <windows.h>

#include "Hash.h"
#include "Store.h"
#include "Zip.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::mutex g_applyLock;

bool ValidKind(const std::string& k) { return k == "ball" || k == "car" || k == "decal" || k == "hud"; }

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
bool IsUpk(const fs::path& p) { return Lower(p.extension().string()) == ".upk"; }

fs::path PackPath(const std::string& kind, const std::string& name) {
    if (!ValidKind(kind) || Trim(name).empty()) throw std::runtime_error("Unknown pack");
    if (name.find_first_of("/\\:") != std::string::npos) throw std::runtime_error("Invalid pack name");
    return store::PacksDir() / kind / name;
}

json State() { return store::ReadJson("custom-pack-state.json", json::object()); }

std::string Sha256File(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("packs: cannot open " + p.string());
    std::stringstream ss;
    ss << f.rdbuf();
    return hashing::Sha256Hex(ss.str());
}
void CopyOver(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    fs::copy_file(a, b, fs::copy_options::overwrite_existing, ec);
}
std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            o += (char)c;
        else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 0xF];
        }
    }
    return o;
}

// Unlocked restore (callers that already hold g_applyLock use this).
json RestoreImpl(const std::string& kind, const std::string& name) {
    if (!ValidKind(kind)) throw std::runtime_error("Unknown kind");
    json state = State();
    if (!state.contains(kind) || !state[kind].is_object()) return json{{"ok", true}, {"restored", 0}};
    json record = state[kind];
    if (!name.empty() && name != record.value("name", std::string("")))
        return json{{"ok", true}, {"restored", 0}};
    fs::path cooked = record.value("cookedDir", std::string(""));
    json files = record.value("files", json::object());
    std::vector<std::pair<fs::path, fs::path>> targets;
    for (auto& [filename, digest] : files.items()) {
        fs::path target = cooked / filename;
        if (target.parent_path() != cooked) throw std::runtime_error("Invalid saved pack target");
        if (Sha256File(target) != digest.get<std::string>())
            throw std::runtime_error("A pack file changed outside the rebuild: " + filename);
        fs::path backup = target;
        backup += ".ripplebak";
        std::error_code ec;
        if (!fs::is_regular_file(backup, ec)) throw std::runtime_error("Pack backup missing: " + filename);
        targets.push_back({target, backup});
    }
    std::error_code ec;
    for (auto& [target, backup] : targets) {
        fs::path tmp = target;
        tmp += ".ripple-restore";
        CopyOver(backup, tmp);
        fs::rename(tmp, target, ec);
        if (ec) {
            fs::remove(target, ec);
            fs::rename(tmp, target, ec);
        }
    }
    state.erase(kind);
    store::WriteJson("custom-pack-state.json", state);
    return json{{"ok", true}, {"restored", (long long)targets.size()}};
}
}  // namespace

namespace packs {

json ListPacks(const std::string& kind) {
    if (!ValidKind(kind)) return json{{"packs", json::array()}, {"activeName", ""}};
    fs::path dir = store::PacksDir() / kind;
    json active = State().value(kind, json::object());
    std::string activeName = active.is_object() ? active.value("name", std::string("")) : "";

    std::vector<std::string> names;
    std::error_code ec;
    if (fs::is_directory(dir, ec))
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.is_directory()) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());

    json packs = json::array();
    for (auto& name : names) {
        fs::path thumb = dir / name / "thumb.png";
        std::string thumbUrl = fs::exists(thumb, ec)
                                   ? "ripple://local/__packs/" + kind + "/" + UrlEncode(name) + "/thumb.png"
                                   : "";
        packs.push_back(json{{"name", name}, {"slug", name}, {"active", activeName == name}, {"thumb", thumbUrl}});
    }
    return json{{"packs", packs}, {"activeName", activeName}};
}

json Rename(const std::string& kind, const std::string& oldName, const std::string& newName) {
    fs::path source = PackPath(kind, oldName), dest = PackPath(kind, newName);
    std::error_code ec;
    if (fs::exists(dest, ec)) throw std::runtime_error("Pack name already exists");
    fs::rename(source, dest, ec);
    if (ec) throw std::runtime_error("Could not rename pack");
    json state = State();
    if (state.contains(kind) && state[kind].is_object() && state[kind].value("name", std::string("")) == oldName) {
        state[kind]["name"] = newName;
        store::WriteJson("custom-pack-state.json", state);
    }
    return json{{"ok", true}};
}

json Remove(const std::string& kind, const std::string& name) {
    fs::path path = PackPath(kind, name);
    json state = State();
    if (state.contains(kind) && state[kind].is_object() && state[kind].value("name", std::string("")) == name)
        throw std::runtime_error("Restore this pack before deleting it");
    std::error_code ec;
    if (fs::is_directory(path, ec)) fs::remove_all(path, ec);
    return json{{"ok", true}};
}

json ImportZip(const std::string& kind, const std::string& path, const std::string& name) {
    fs::path kindDir = store::PacksDir() / kind;
    if (!ValidKind(kind)) throw std::runtime_error("Unknown pack");
    std::string n = !name.empty() ? name : fs::path(path).stem().string();
    fs::path dest = PackPath(kind, n);
    std::error_code ec;
    if (fs::exists(dest, ec)) throw std::runtime_error("Pack name already exists");
    fs::create_directories(kindDir, ec);
    fs::path staging = kindDir / (".ripple-import-" + std::to_string(GetTickCount64()));
    fs::create_directories(staging, ec);
    try {
        zip::Extract(path, staging);
        bool hasUpk = false;
        for (auto& e : fs::recursive_directory_iterator(staging, ec))
            if (e.is_regular_file() && IsUpk(e.path())) {
                hasUpk = true;
                break;
            }
        if (!hasUpk) throw std::runtime_error("Pack contains no UPK packages");
        fs::copy(staging, dest, fs::copy_options::recursive, ec);
        if (ec) throw std::runtime_error("Could not install pack");
    } catch (...) {
        fs::remove_all(staging, ec);
        throw;
    }
    fs::remove_all(staging, ec);
    return json{{"ok", true}, {"name", n}, {"slug", n}};
}

json RestorePack(const std::string& kind, const std::string& name) {
    std::lock_guard<std::mutex> lk(g_applyLock);
    return RestoreImpl(kind, name);
}

json ApplyPack(const std::string& kind, const std::string& name) {
    fs::path cooked = store::CookedDir();
    if (cooked.empty()) throw std::runtime_error("Rocket League install not set");
    fs::path src = PackPath(kind, name);
    std::error_code ec;
    if (!fs::is_directory(src, ec)) throw std::runtime_error("pack not found");

    std::vector<fs::path> packages;
    for (auto& e : fs::recursive_directory_iterator(src, ec))
        if (e.is_regular_file() && IsUpk(e.path()) && fs::is_regular_file(cooked / e.path().filename(), ec))
            packages.push_back(e.path());
    if (packages.empty()) throw std::runtime_error("No pack package matches this game install");
    std::set<std::string> lowNames;
    for (auto& p : packages) lowNames.insert(Lower(p.filename().string()));
    if (lowNames.size() != packages.size()) throw std::runtime_error("Pack contains duplicate package filenames");

    std::lock_guard<std::mutex> lk(g_applyLock);
    fs::path staging = cooked / (".ripple-pack-" + std::to_string(GetTickCount64()));
    fs::create_directories(staging, ec);
    try {
        for (auto& s : packages) CopyOver(s, staging / s.filename());

        json current = State();
        for (auto& [other, record] : current.items()) {
            if (other != kind && record.is_object() && record.value("cookedDir", std::string("")) == cooked.string()) {
                json files = record.value("files", json::object());
                for (auto& p : packages)
                    if (files.contains(p.filename().string()))
                        throw std::runtime_error("Another active pack uses these packages");
            }
        }

        RestoreImpl(kind, "");  // restore any prior pack of this kind first

        for (auto& s : packages) {
            fs::path target = cooked / s.filename();
            fs::path backup = target;
            backup += ".ripplebak";
            if (!fs::exists(backup, ec)) CopyOver(target, backup);
            fs::rename(staging / s.filename(), target, ec);
            if (ec) {
                fs::remove(target, ec);
                fs::rename(staging / s.filename(), target, ec);
            }
        }

        json files = json::object();
        for (auto& s : packages) files[s.filename().string()] = Sha256File(cooked / s.filename());
        current = State();
        current[kind] = json{{"name", name}, {"cookedDir", cooked.string()}, {"files", files}};
        store::WriteJson("custom-pack-state.json", current);
    } catch (...) {
        fs::remove_all(staging, ec);
        throw;
    }
    fs::remove_all(staging, ec);
    return json{{"ok", true}, {"copied", (long long)packages.size()}};
}

}  // namespace packs
