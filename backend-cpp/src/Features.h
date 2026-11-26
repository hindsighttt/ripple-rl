#pragma once
// Native port of ripple/features.py: per-feature JSON state with
// defaults. Defaults are loaded from data/feature_defaults.json, which is
// generated verbatim from features.DEFAULTS (so there is no hand-transcription
// of that large dict). State lives in %APPDATA%\RippleRL\feature_<name>.json
// where dots in the name become underscores.
#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace features {

json Get(const std::string& name);                            // throws if unknown
json SetState(const std::string& name, const json& patch);    // merge + persist, returns {ok,...state}
const json& Stadiums();                                       // data/stadiums.json (cached)

}  // namespace features
