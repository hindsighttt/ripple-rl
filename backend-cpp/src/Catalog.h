#pragma once
// Native port of ripple/catalog.py: loads data/catalog.json +
// data/items.json (+ inventory), builds the rarity-sorted projection once, and
// serves the item/category/title/inventory queries the UI's catalog panels use.
#include <nlohmann/json.hpp>

#include <cstddef>

using json = nlohmann::json;

namespace catalog {

std::size_t CatalogItemCount();  // number of catalog items (for app.state)
std::size_t InvCount();          // number of owned item ids

json Categories();               // {categories:[{label,slug,count}], total}
json Titles();                   // {titles:[...], categories:[...]}
json ListItems(const json& p);   // filtered/sorted/paged item list
json FindItem(const std::string& key);  // by asset_path or code; throws if missing

}  // namespace catalog
