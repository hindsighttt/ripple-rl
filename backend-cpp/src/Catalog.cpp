#include "Catalog.h"

#include "Store.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::mutex g_mtx;
bool g_loaded = false, g_rarityBuilt = false, g_preparedBuilt = false;
json g_cat, g_items, g_inv;
std::unordered_set<std::string> g_owned;
std::unordered_map<std::string, std::string> g_rarity;

struct Row {
    json itemId;
    std::string category, code, key, label, rarity, ll, lc;
};
std::vector<Row> g_prepared;

const std::vector<std::pair<std::string, std::string>> kCategories = {
    {"Bodies", "Bodies"},
    {"Boosts", "Boosts"},
    {"Wheels", "Wheels"},
    {"Decals", "Decals"},
    {"Toppers", "Toppers"},
    {"Antennas", "Antennas"},
    {"Goal Explosions", "GoalExplosions"},
    {"Engine Sounds", "EngineSounds"},
    {"Anthems", "Anthems"},
    {"Player Banners", "PlayerBanners"},
    {"Avatar Borders", "AvatarBorders"},
    {"Paint Finishes", "PaintFinishes"},
    {"Trails", "Trails"},
};

const std::unordered_map<std::string, int> kRarityOrder = {
    {"common", 0},  {"uncommon", 1},   {"rare", 2},    {"veryrare", 3}, {"import", 4},
    {"exotic", 5},  {"blackmarket", 6}, {"limited", 7}, {"legacy", 8}};

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string S(const json& o, const char* k) {
    auto it = o.find(k);
    if (it != o.end() && it->is_string()) return it->get<std::string>();
    return "";
}
bool B(const json& o, const char* k) {
    auto it = o.find(k);
    if (it != o.end()) {
        if (it->is_boolean()) return it->get<bool>();
        if (it->is_number()) return it->get<double>() != 0;
    }
    return false;
}
long long N(const json& o, const char* k, long long def) {
    auto it = o.find(k);
    if (it != o.end() && it->is_number()) return it->get<long long>();
    return def;
}
json Id(const json& o) {
    auto it = o.find("id");
    return it != o.end() ? *it : json(nullptr);
}
// A stable string key for an item id (ids are ints here, but this works for
// strings too), so owned-set membership and the rarity map agree on type.
std::string IdKey(const json& v) { return v.is_null() ? std::string("\x01null") : v.dump(); }

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

void Load() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_loaded) return;
    g_cat = LoadFile(store::DataDir() / "catalog.json");
    if (!g_cat.is_object()) g_cat = json::object();
    g_items = LoadFile(store::DataDir() / "items.json");
    if (!g_items.is_array()) g_items = json::array();
    g_inv = store::ReadJson("inventory.json", json(nullptr));
    if (!g_inv.is_object() || g_inv.empty()) {
        json d = LoadFile(store::DataDir() / "inventory.json");
        g_inv = d.is_object() ? d : json::object();
    }
    g_owned.clear();
    auto ids = g_inv.find("ids");
    if (ids != g_inv.end() && ids->is_array())
        for (auto& id : *ids) g_owned.insert(IdKey(id));
    g_loaded = true;
}

void BuildRarity() {
    if (g_rarityBuilt) return;
    for (auto& it : g_items) {
        if (!it.is_object()) continue;
        std::string q = S(it, "Quality");
        if (q.empty()) q = "Common";
        auto idIt = it.find("ID");
        g_rarity[IdKey(idIt != it.end() ? *idIt : json(nullptr))] = Lower(q);
    }
    g_rarityBuilt = true;
}

void BuildPrepared() {
    if (g_preparedBuilt) return;
    BuildRarity();
    g_prepared.clear();
    auto items = g_cat.find("items");
    if (items != g_cat.end() && items->is_array()) {
        for (auto& it : *items) {
            if (!it.is_object()) continue;
            json iid = Id(it);
            auto r = g_rarity.find(IdKey(iid));
            Row row;
            row.itemId = iid;
            row.category = S(it, "category");
            row.code = S(it, "code");
            row.key = S(it, "asset_path");
            row.label = S(it, "label");
            row.rarity = (r != g_rarity.end()) ? r->second : "common";
            row.ll = Lower(row.label);
            row.lc = Lower(row.code);
            g_prepared.push_back(std::move(row));
        }
    }
    std::sort(g_prepared.begin(), g_prepared.end(), [](const Row& a, const Row& b) {
        auto ra = kRarityOrder.find(a.rarity);
        auto rb = kRarityOrder.find(b.rarity);
        int oa = ra != kRarityOrder.end() ? ra->second : 9;
        int ob = rb != kRarityOrder.end() ? rb->second : 9;
        if (oa != ob) return oa < ob;
        return a.ll < b.ll;
    });
    g_preparedBuilt = true;
}
}  // namespace

namespace catalog {

std::size_t CatalogItemCount() {
    Load();
    auto it = g_cat.find("items");
    return (it != g_cat.end() && it->is_array()) ? it->size() : 0;
}

std::size_t InvCount() {
    Load();
    return g_owned.size();
}

json Categories() {
    Load();
    std::unordered_map<std::string, long long> counts;
    long long total = 0;
    auto items = g_cat.find("items");
    if (items != g_cat.end() && items->is_array())
        for (auto& it : *items) {
            counts[S(it, "category")]++;
            total++;
        }
    json arr = json::array();
    for (auto& [label, slug] : kCategories) {
        auto f = counts.find(slug);
        if (f != counts.end() && f->second > 0)
            arr.push_back(json{{"label", label}, {"slug", slug}, {"count", f->second}});
    }
    return json{{"categories", arr}, {"total", total}};
}

json FindItem(const std::string& key) {
    Load();
    std::string k = Lower(key);
    auto items = g_cat.find("items");
    if (items != g_cat.end() && items->is_array())
        for (auto& it : *items)
            if (Lower(S(it, "asset_path")) == k || Lower(S(it, "code")) == k) return it;
    throw std::runtime_error("item not found: " + key);
}

json Titles() {
    json t = LoadFile(store::DataDir() / "titles.json");
    if (!t.is_array() && !t.is_object()) t = json::array();
    json c = LoadFile(store::DataDir() / "categories.json");
    if (c.is_null()) c = json::array();
    return json{{"titles", t}, {"categories", c}};
}

json ListItems(const json& p) {
    Load();
    BuildPrepared();
    std::string cat = S(p, "category");
    std::string rq = S(p, "rarity");
    std::string q = Lower(S(p, "query"));
    bool only = B(p, "ownedOnly");
    bool notOwned = B(p, "notOwnedOnly");

    json res = json::array();
    for (auto& row : g_prepared) {
        if (!cat.empty() && row.category != cat) continue;
        if (!rq.empty() && row.rarity != rq) continue;
        if (!q.empty() && row.ll.find(q) == std::string::npos && row.lc.find(q) == std::string::npos)
            continue;
        bool owned = g_owned.count(IdKey(row.itemId)) > 0;
        if (only && !owned) continue;
        if (notOwned && owned) continue;
        res.push_back(json{{"category", row.category},
                           {"code", row.code},
                           {"itemId", row.itemId},
                           {"key", row.key},
                           {"label", row.label},
                           {"rarity", row.rarity}});
    }

    long long off = N(p, "offset", 0);
    long long lim = N(p, "limit", 100);
    if (off < 0) off = 0;
    if (lim < 0) lim = 0;
    long long total = (long long)res.size();
    json items = json::array();
    for (long long i = off; i < off + lim && i < total; ++i) items.push_back(res[(size_t)i]);

    return json{{"items", items},
                {"total", total},
                {"offset", off},
                {"inventoryKnown", true},
                {"inventoryCount", (long long)g_owned.size()}};
}

}  // namespace catalog
