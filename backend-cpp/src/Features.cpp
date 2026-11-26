#include "Features.h"

#include "Store.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
json LoadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return json(nullptr);
    try {
        std::stringstream ss;
        ss << f.rdbuf();
        return json::parse(ss.str());
    } catch (...) {
        return json(nullptr);
    }
}

const json& Defaults() {
    static json defs = [] {
        json d = LoadFile(store::DataDir() / "feature_defaults.json");
        if (!d.is_object()) d = json::object();
        return d;
    }();
    return defs;
}

std::string FileStem(const std::string& name) {
    auto stem = "feature_" + name;
    std::replace(stem.begin(), stem.end(), '.', '_');
    return stem;
}
}  // namespace

namespace features {

void MigrateLegacyState() {
    for (const auto& [name, defaults] : Defaults().items()) {
        const auto stem = FileStem(name);
        const auto legacy = store::StateDir() / (stem + "_json");
        const auto filename = stem + ".json";
        const auto canonical = store::StateDir() / filename;
        std::error_code legacyError, canonicalError;
        const auto legacyTime = fs::last_write_time(legacy, legacyError);
        if (legacyError) continue;
        const auto canonicalTime = fs::last_write_time(canonical, canonicalError);
        if (!canonicalError && canonicalTime >= legacyTime && LoadFile(canonical).is_object()) continue;
        const auto saved = LoadFile(legacy);
        if (saved.is_object()) store::WriteJson(filename, saved);
    }
}

json Get(const std::string& name) {
    const json& defs = Defaults();
    if (!defs.contains(name)) throw std::runtime_error("unknown feature: " + name);
    json out = defs.at(name);
    json cur = store::ReadJson(FileStem(name) + ".json", json(nullptr));
    if (cur.is_object())
        for (auto& [k, v] : cur.items()) out[k] = v;
    return out;
}

json SetState(const std::string& name, const json& patch) {
    json cur = Get(name);
    if (patch.is_object())
        for (auto& [k, v] : patch.items()) cur[k] = v;
    store::WriteJson(FileStem(name) + ".json", cur);
    json res = cur;
    res["ok"] = true;
    return res;
}

const json& Stadiums() {
    static json stadiums = [] {
        json s = LoadFile(store::DataDir() / "stadiums.json");
        if (!s.is_array()) s = json::array();
        return s;
    }();
    return stadiums;
}

}  // namespace features
