#pragma once
// Native port of ripple/workshop.py: import custom workshop maps (zip or
// loose .upk) and swap them in as Labs_Utopia_P.upk.
#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace workshop {
json ListMaps();
json ImportMap(const std::string& path, const std::string& name);
json ApplyMap(const std::string& name);
json RestoreMap();
json DeleteMap(const std::string& name);
}  // namespace workshop
