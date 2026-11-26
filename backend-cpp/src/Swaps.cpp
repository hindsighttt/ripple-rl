#include "Swaps.h"

#include "Catalog.h"
#include "Store.h"
#include "Upk.h"

#include <cctype>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {
std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string Str(const json& p, const char* k) {
    auto it = p.find(k);
    return (it != p.end() && it->is_string()) ? it->get<std::string>() : "";
}
json Active() {
    json r = store::ReadJson("swaps.json", json::array());
    return r.is_array() ? r : json::array();
}
void Save(const json& rows) { store::WriteJson("swaps.json", rows); }
std::string SettingsSource() {
    json t = store::Settings().value("target", json::object());
    return t.is_object() ? t.value("source", "Epic") : "Epic";
}
}  // namespace

namespace swaps {

json ApplySwap(const json& p) {
    fs::path cooked = store::CookedDir();
    if (cooked.empty()) throw std::runtime_error("Rocket League install not set");
    json tgt = catalog::FindItem(Str(p, "targetKey"));
    json src = catalog::FindItem(Str(p, "sourceKey"));
    if (tgt.value("category", std::string("")) != src.value("category", std::string("")))
        throw std::runtime_error("You can only swap an item for another of the same category.");

    std::string tgtFile = tgt.value("file", std::string(""));
    std::string srcFile = src.value("file", std::string(""));
    fs::path tpath = cooked / tgtFile;
    fs::path spath = cooked / srcFile;
    std::error_code ec;
    if (!fs::exists(tpath, ec)) throw std::runtime_error("target package missing in the game");
    if (!fs::exists(spath, ec)) throw std::runtime_error("source package missing in the game");

    upk::SwapFile(tpath, spath, upk::PkgBasename(tgtFile), store::DataDir(), store::BackupDir() / "Epic");

    std::string source = SettingsSource();
    std::string paint = Str(p, "paint");
    json row = {{"installCookedDir", cooked.string()},
                {"installSource", source},
                {"redirectAssetPath", src.value("asset_path", std::string(""))},
                {"redirectKey", tgt.value("code", std::string(""))},
                {"sourceFile", srcFile},
                {"sourceLabel", src.value("label", std::string(""))},
                {"sourcePackage", upk::PkgBasename(srcFile)},
                {"targetFile", tgtFile},
                {"targetLabel", tgt.value("label", std::string(""))},
                {"paint", paint},
                {"paintOnly", false},
                {"painted", !paint.empty()},
                {"timestamp", store::IsoNow()}};

    json rows = Active();
    json kept = json::array();
    for (auto& r : rows)
        if (!(r.value("targetFile", std::string("")) == tgtFile &&
              r.value("installSource", std::string("")) == source))
            kept.push_back(r);
    kept.push_back(row);
    Save(kept);
    return row;
}

json PaintItem(const json& p) {
    json rows = Active();
    std::string itemKey = Str(p, "itemKey");
    std::string hex = Str(p, "hex");
    std::string prefix = Lower(itemKey.substr(0, itemKey.find('.')));
    for (auto& r : rows) {
        std::string rk = r.value("redirectKey", std::string(""));
        std::string tf = Lower(r.value("targetFile", std::string("")));
        if (rk == itemKey || tf.rfind(prefix, 0) == 0) {
            r["paint"] = hex;
            r["painted"] = true;
        }
    }
    Save(rows);
    return json{{"ok", true}, {"paint", hex}};
}

json Revert(long long index) {
    json rows = Active();
    if (index < 0 || index >= (long long)rows.size()) throw std::runtime_error("bad index");
    json r = rows[(size_t)index];
    rows.erase(rows.begin() + index);
    std::string cs = r.value("installCookedDir", std::string(""));
    fs::path cooked = cs.empty() ? store::CookedDir() : fs::path(cs);
    if (!cooked.empty())
        upk::RestoreFile(cooked / r.value("targetFile", std::string("")), store::BackupDir() / "Epic");
    Save(rows);
    return json{{"ok", true}};
}

json RevertAll() {
    json rows = Active();
    for (auto& r : rows) {
        std::string cs = r.value("installCookedDir", std::string(""));
        fs::path cooked = cs.empty() ? store::CookedDir() : fs::path(cs);
        if (!cooked.empty()) {
            try {
                upk::RestoreFile(cooked / r.value("targetFile", std::string("")),
                                 store::BackupDir() / "Epic");
            } catch (...) {
            }
        }
    }
    Save(json::array());
    return json{{"ok", true}};
}

json ListActive() {
    json rows = Active();
    std::string other = (SettingsSource() == "Epic") ? "Steam" : "Epic";
    return json{{"swaps", rows},
                {"count", (long long)rows.size()},
                {"otherInstall", other},
                {"copyable", other.empty() ? 0 : 1}};
}

json CopyToOther() { return json{{"ok", true}, {"copied", 0}}; }

json CurrentSwaps() { return Active(); }

}  // namespace swaps
