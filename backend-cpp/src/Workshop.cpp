#include "Workshop.h"

#include "Store.h"
#include "Zip.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {
json LoadCatalog() {
    std::ifstream f(store::DataDir() / "workshop_catalog.json", std::ios::binary);
    if (!f) return json::array();
    try {
        std::stringstream ss;
        ss << f.rdbuf();
        json rows = json::parse(ss.str());
        if (rows.is_array()) return rows;
        if (rows.is_object()) {
            if (rows.contains("maps")) return rows["maps"];
            if (rows.contains("items")) return rows["items"];
        }
    } catch (...) {
    }
    return json::array();
}

bool HasExt(const std::string& name, std::initializer_list<const char*> exts) {
    std::string low = name;
    for (auto& c : low) c = (char)std::tolower((unsigned char)c);
    for (auto* e : exts)
        if (low.size() >= std::string(e).size() && low.compare(low.size() - std::string(e).size(), std::string(e).size(), e) == 0)
            return true;
    return false;
}

void CopyFile(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    fs::copy_file(a, b, fs::copy_options::overwrite_existing, ec);
}
}  // namespace

namespace workshop {

json ListMaps() {
    json imported = json::array();
    std::error_code ec;
    if (fs::is_directory(store::WorkshopDir(), ec))
        for (auto& e : fs::directory_iterator(store::WorkshopDir(), ec))
            if (e.is_directory()) imported.push_back(json{{"name", e.path().filename().string()}, {"imported", true}});
    return json{{"catalog", LoadCatalog()}, {"imported", imported}};
}

json ImportMap(const std::string& path, const std::string& name) {
    std::string n = !name.empty() ? name : fs::path(path).stem().string();
    fs::path dest = store::WorkshopDir() / n;
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (HasExt(path, {".zip"}))
        zip::Extract(path, dest);
    else
        CopyFile(path, dest / fs::path(path).filename());
    return json{{"ok", true}, {"name", n}};
}

json ApplyMap(const std::string& name) {
    fs::path cooked = store::CookedDir();
    if (cooked.empty()) throw std::runtime_error("Rocket League install not set");
    fs::path src = store::WorkshopDir() / name;
    std::error_code ec;
    if (!fs::is_directory(src, ec)) throw std::runtime_error("map not found");
    fs::path target = cooked / "Labs_Utopia_P.upk";
    if (!fs::exists(target, ec)) throw std::runtime_error("Labs_Utopia_P.upk missing");
    fs::path bak = target;
    bak += ".ripplebak";
    if (!fs::exists(bak, ec)) CopyFile(target, bak);
    fs::path pkg;
    for (auto& e : fs::directory_iterator(src, ec))
        if (e.is_regular_file() && HasExt(e.path().filename().string(), {".upk", ".udk"})) {
            pkg = e.path();
            break;
        }
    if (pkg.empty()) throw std::runtime_error("map package missing");
    CopyFile(pkg, target);
    return json{{"ok", true}};
}

json RestoreMap() {
    fs::path cooked = store::CookedDir();
    if (!cooked.empty()) {
        fs::path t = cooked / "Labs_Utopia_P.upk";
        fs::path bak = t;
        bak += ".ripplebak";
        std::error_code ec;
        if (fs::exists(bak, ec)) CopyFile(bak, t);
    }
    return json{{"ok", true}};
}

json DeleteMap(const std::string& name) {
    std::error_code ec;
    fs::remove_all(store::WorkshopDir() / name, ec);
    return json{{"ok", true}};
}

}  // namespace workshop
