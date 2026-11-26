#pragma once
// Native port of ripple/packs.py: custom content packs (ball/car/decal/
// hud) — import from zip, list, rename, remove, and apply/restore the pack's UPK
// packages over the game install (hash-guarded, backed up, state-tracked).
#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace packs {
json ListPacks(const std::string& kind);
json ImportZip(const std::string& kind, const std::string& path, const std::string& name = "");
json Remove(const std::string& kind, const std::string& name);
json Rename(const std::string& kind, const std::string& oldName, const std::string& newName);
json ApplyPack(const std::string& kind, const std::string& name);
json RestorePack(const std::string& kind, const std::string& name = "");
}  // namespace packs
