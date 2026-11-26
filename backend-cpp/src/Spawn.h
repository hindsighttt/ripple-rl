#pragma once
// Native port of ripple/spawn.py: the spawn list (items/titles the proxy
// injects into GetPlayerProducts), persisted in spawn.json.
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace spawn {
json Status();
json ListSpawn();
json Add(const json& p);
json AddMany(const json& p);
json AddList(const json& p);
json AddTitle(const json& p);
json Remove(const json& p);
json RemoveMany(const json& p);
json Clear();
json Paints();
json Paintable(const json& p);
json TitlesOwned();
}  // namespace spawn
