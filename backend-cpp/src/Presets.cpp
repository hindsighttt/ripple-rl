#include "Presets.h"

#include "Store.h"
#include "Swaps.h"

#include <stdexcept>
#include <string>

namespace {
std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::string SG(const json& p, const char* k) {
    auto it = p.find(k);
    return (it != p.end() && it->is_string()) ? it->get<std::string>() : "";
}
json* Find(json& presets, const std::string& name) {
    for (auto& x : presets)
        if (x.value("name", std::string("")) == name) return &x;
    return nullptr;
}
json DefaultDoc() {
    return json{{"currentPreset", "Default"},
                {"presets", json::array({json{{"name", "Default"}, {"swaps", json::array()}}})}};
}
}  // namespace

namespace presets {

json List() {
    json d = store::ReadJson("presets.json", DefaultDoc());
    if (!d.is_object() || !d.contains("presets") || !d["presets"].is_array()) d = DefaultDoc();
    return d;
}

json New(const json& p) {
    json d = List();
    std::string raw = SG(p, "name");
    std::string name = Trim(raw.empty() ? std::string("Preset") : raw);
    if (name.empty()) throw std::runtime_error("preset name required");
    if (Find(d["presets"], name)) throw std::runtime_error("preset exists");
    d["presets"].push_back(json{{"name", name}, {"swaps", swaps::CurrentSwaps()}});
    store::WriteJson("presets.json", d);
    return d;
}

json Rename(const json& p) {
    json d = List();
    std::string name = SG(p, "name");
    std::string newName = Trim(SG(p, "newName"));
    if (newName.empty()) throw std::runtime_error("preset name required");
    json* preset = Find(d["presets"], name);
    if (!preset) throw std::runtime_error("preset not found");
    if (newName != name && Find(d["presets"], newName)) throw std::runtime_error("preset exists");
    (*preset)["name"] = newName;
    if (d.value("currentPreset", std::string("")) == name) d["currentPreset"] = newName;
    store::WriteJson("presets.json", d);
    return d;
}

json Duplicate(const json& p) {
    json d = List();
    std::string newName = Trim(SG(p, "newName"));
    if (newName.empty()) throw std::runtime_error("preset name required");
    json* preset = Find(d["presets"], SG(p, "name"));
    if (!preset) throw std::runtime_error("preset not found");
    if (Find(d["presets"], newName)) throw std::runtime_error("preset exists");
    json dup = *preset;
    dup["name"] = newName;
    d["presets"].push_back(dup);
    store::WriteJson("presets.json", d);
    return d;
}

json Delete(const json& p) {
    json d = List();
    std::string name = SG(p, "name");
    json kept = json::array();
    for (auto& x : d["presets"])
        if (x.value("name", std::string("")) != name) kept.push_back(x);
    d["presets"] = kept;
    store::WriteJson("presets.json", d);
    return d;
}

json Load(const json& p) {
    json d = List();
    std::string name = SG(p, "name");
    json* preset = Find(d["presets"], name);
    if (!preset) throw std::runtime_error("preset not found");
    swaps::RevertAll();
    if (preset->contains("swaps") && (*preset)["swaps"].is_array())
        for (auto& row : (*preset)["swaps"]) {
            try {
                swaps::ApplySwap(json{{"targetKey", row.value("redirectKey", std::string(""))},
                                      {"sourceKey", row.value("redirectAssetPath", std::string(""))}});
            } catch (...) {
            }
        }
    d["currentPreset"] = name;
    store::WriteJson("presets.json", d);
    return json{{"ok", true}};
}

json Export(const json&) { return json{{"ok", false}}; }
json Import(const json&) { return json{{"ok", false}}; }

}  // namespace presets
