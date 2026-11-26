#include "Spawn.h"

#include "Store.h"
#include "WinUtil.h"

#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr long long kTitlePid = 3036;
const std::vector<std::string> kPaints = {
    "Unpainted", "Crimson", "Lime", "Black", "Sky Blue", "Cobalt", "Burnt Sienna",
    "Forest Green", "Purple", "Pink", "Orange", "Grey", "Titanium White", "Saffron",
    "Gold", "Rose Gold", "White Gold", "Onyx", "Platinum"};

std::mutex g_mtx;
bool g_itemsLoaded = false, g_paintLoaded = false;
std::unordered_map<long long, std::pair<std::string, std::string>> g_itemById;  // ID -> (Slot, Product)
std::unordered_set<long long> g_paintable;

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

void LoadItems() {
    if (g_itemsLoaded) return;
    json items = LoadFile(store::DataDir() / "items.json");
    if (items.is_array())
        for (auto& it : items) {
            if (!it.is_object() || !it.contains("ID") || !it["ID"].is_number()) continue;
            long long id = it["ID"].get<long long>();
            std::string slot = it.contains("Slot") && it["Slot"].is_string() ? it["Slot"].get<std::string>() : "Misc";
            std::string product = it.contains("Product") && it["Product"].is_string() ? it["Product"].get<std::string>() : "";
            g_itemById[id] = {slot, product};
        }
    g_itemsLoaded = true;
}

void LoadPaintable() {
    if (g_paintLoaded) return;
    json p = LoadFile(store::DataDir() / "paintable.json");
    if (p.is_array())
        for (auto& v : p)
            if (v.is_number()) g_paintable.insert(v.get<long long>());
    g_paintLoaded = true;
}

long long IntOf(const json& p, const char* k) {
    auto it = p.find(k);
    if (it == p.end()) return 0;
    if (it->is_number()) return it->get<long long>();
    if (it->is_string()) {
        try {
            return std::stoll(it->get<std::string>());
        } catch (...) {
        }
    }
    return 0;
}

json Load() {
    json d = store::ReadJson("spawn.json", json{{"items", json::array()}});
    if (!d.is_object()) d = json{{"items", json::array()}};
    if (!d.contains("items") || !d["items"].is_array()) d["items"] = json::array();
    return d;
}
void Save(const json& d) { store::WriteJson("spawn.json", d); }

bool IsPaintable(long long pid) {
    LoadPaintable();
    return g_paintable.count(pid) > 0;
}

json Row(long long pid, long long paint, long long fromDrop, const std::string& titleId) {
    LoadItems();
    auto it = g_itemById.find(pid);
    std::string slot = it != g_itemById.end() ? it->second.first : "Misc";
    std::string product = it != g_itemById.end() && !it->second.second.empty()
                              ? it->second.second
                              : ("Item " + std::to_string(pid));
    std::string paintName = (paint >= 0 && (size_t)paint < kPaints.size()) ? kPaints[(size_t)paint] : "Unpainted";
    return json{{"category", slot},
                {"fromDrop", fromDrop},
                {"instanceId", win::RandomHex(16)},
                {"label", product},
                {"paint", paint},
                {"paintName", paintName},
                {"productId", pid},
                {"titleId", titleId},
                {"addedTimestamp", (long long)std::time(nullptr)}};
}
}  // namespace

namespace spawn {

json Status() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return json{{"available", true}, {"count", (long long)Load()["items"].size()}};
}

json ListSpawn() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return json{{"items", Load()["items"]}};
}

json Add(const json& p) {
    std::lock_guard<std::mutex> lk(g_mtx);
    json d = Load();
    json row = Row(IntOf(p, "productId"), IntOf(p, "paint"), IntOf(p, "fromDrop"), "");
    d["items"].push_back(row);
    Save(d);
    return json{{"ok", true}, {"item", row}};
}

json AddMany(const json& p) {
    long long added = 0;
    if (p.contains("items") && p["items"].is_array())
        for (auto& it : p["items"]) {
            Add(it);
            ++added;
        }
    return json{{"ok", true}, {"added", added}};
}

json AddList(const json& p) {
    std::vector<long long> productIds;
    std::unordered_set<long long> seenP;
    if (p.contains("productIds") && p["productIds"].is_array())
        for (auto& v : p["productIds"]) {
            long long id = v.is_number() ? v.get<long long>() : 0;
            if (seenP.insert(id).second) productIds.push_back(id);
        }
    std::vector<long long> paints;
    std::unordered_set<long long> seenPaint;
    if (p.contains("paints") && p["paints"].is_array())
        for (auto& v : p["paints"]) {
            long long paint = v.is_number() ? v.get<long long>() : 0;
            if (seenPaint.insert(paint).second) paints.push_back(paint);
        }
    for (long long paint : paints)
        if (paint < 0 || (size_t)paint >= kPaints.size()) throw std::runtime_error("unknown paint");

    std::lock_guard<std::mutex> lk(g_mtx);
    json d = Load();
    long long count = 0;
    if (!paints.empty())
        for (long long pid : productIds) {
            std::vector<long long> forProduct = IsPaintable(pid) ? paints : std::vector<long long>{0};
            for (long long paint : forProduct) {
                d["items"].push_back(Row(pid, paint, 0, ""));
                ++count;
            }
        }
    Save(d);
    return json{{"ok", true}, {"count", count}};
}

json AddTitle(const json& p) {
    std::string tid = p.contains("titleId") && p["titleId"].is_string() ? p["titleId"].get<std::string>() : "";
    size_t a = tid.find_first_not_of(" \t\r\n");
    tid = (a == std::string::npos) ? "" : tid.substr(a, tid.find_last_not_of(" \t\r\n") - a + 1);
    if (tid.empty()) throw std::runtime_error("titleId required");
    std::lock_guard<std::mutex> lk(g_mtx);
    json d = Load();
    for (auto& x : d["items"])
        if (x.value("titleId", std::string("")) == tid)
            return json{{"ok", true}, {"item", x}, {"already", true}};
    json row = Row(kTitlePid, 0, 0, tid);
    d["items"].push_back(row);
    Save(d);
    return json{{"ok", true}, {"item", row}};
}

json TitlesOwned() {
    std::lock_guard<std::mutex> lk(g_mtx);
    json d = Load();
    json tids = json::array();
    std::unordered_set<std::string> seen;
    for (auto& x : d["items"]) {
        std::string t = x.value("titleId", std::string(""));
        if (!t.empty() && seen.insert(t).second) tids.push_back(t);
    }
    return json{{"titles", tids}, {"known", true}};
}

json Remove(const json& p) {
    std::lock_guard<std::mutex> lk(g_mtx);
    json d = Load();
    std::string iid = p.contains("instanceId") && p["instanceId"].is_string() ? p["instanceId"].get<std::string>() : "";
    bool hasPid = p.contains("productId") && !p["productId"].is_null();
    long long pid = hasPid ? IntOf(p, "productId") : 0;
    if (iid.empty() && !hasPid) throw std::runtime_error("instanceId or productId required");
    long long before = (long long)d["items"].size();
    json kept = json::array();
    for (auto& x : d["items"]) {
        bool drop = !iid.empty() ? (x.value("instanceId", std::string("")) == iid)
                                 : (x.value("productId", (long long)0) == pid);
        if (!drop) kept.push_back(x);
    }
    d["items"] = kept;
    Save(d);
    return json{{"ok", true}, {"removed", before - (long long)kept.size()}};
}

json RemoveMany(const json& p) {
    std::lock_guard<std::mutex> lk(g_mtx);
    json d = Load();
    std::unordered_set<std::string> insts;
    if (p.contains("instanceIds") && p["instanceIds"].is_array())
        for (auto& v : p["instanceIds"])
            if (v.is_string()) insts.insert(v.get<std::string>());
    std::unordered_set<long long> pids;
    if (p.contains("ids") && p["ids"].is_array())
        for (auto& v : p["ids"])
            if (v.is_number()) pids.insert(v.get<long long>());
    long long before = (long long)d["items"].size();
    json kept = json::array();
    for (auto& x : d["items"]) {
        bool drop = insts.count(x.value("instanceId", std::string(""))) ||
                    pids.count(x.value("productId", (long long)0));
        if (!drop) kept.push_back(x);
    }
    d["items"] = kept;
    Save(d);
    return json{{"ok", true}, {"removed", before - (long long)kept.size()}};
}

json Clear() {
    std::lock_guard<std::mutex> lk(g_mtx);
    long long count = (long long)Load()["items"].size();
    Save(json{{"items", json::array()}});
    return json{{"ok", true}, {"removed", count}};
}

json Paints() {
    json arr = json::array();
    for (size_t i = 0; i < kPaints.size(); ++i) arr.push_back(json{{"id", (long long)i}, {"name", kPaints[i]}});
    return json{{"paints", arr}};
}

json Paintable(const json& p) {
    return json{{"paintable", IsPaintable(IntOf(p, "productId"))}};
}

}  // namespace spawn
