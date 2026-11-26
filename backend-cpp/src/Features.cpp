#include "Features.h"

#include "Store.h"

#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
std::mutex g_mtx;

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

std::string FileFor(const std::string& name) {
    std::string s = "feature_" + name + ".json";
    for (auto& c : s)
        if (c == '.') c = '_';
    return s;
}
}  // namespace

namespace features {

json Get(const std::string& name) {
    const json& defs = Defaults();
    if (!defs.contains(name)) throw std::runtime_error("unknown feature: " + name);
    json out = defs.at(name);
    json cur = store::ReadJson(FileFor(name), json(nullptr));
    if (cur.is_object())
        for (auto& [k, v] : cur.items()) out[k] = v;
    return out;
}

json SetState(const std::string& name, const json& patch) {
    json cur = Get(name);
    if (patch.is_object())
        for (auto& [k, v] : patch.items()) cur[k] = v;
    store::WriteJson(FileFor(name), cur);
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
