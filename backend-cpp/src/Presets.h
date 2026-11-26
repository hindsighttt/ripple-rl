#pragma once
// Native port of bridge.py's preset methods: named swap sets in presets.json,
// loaded by replaying them through the swaps engine.
#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace presets {
json List();
json New(const json& p);
json Rename(const json& p);
json Duplicate(const json& p);
json Delete(const json& p);
json Load(const json& p);
json Export(const json& p);  // no native file dialog -> {ok:false}
json Import(const json& p);  // no native file dialog -> {ok:false}
}  // namespace presets
