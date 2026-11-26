#include "PackagePatches.h"

#include "Hash.h"
#include "Store.h"
#include "WinUtil.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr char kSelection[] = "package-patch-selection.json";
using Selection = std::set<std::string>;

std::string Read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read " + win::Narrow(path.wstring()));
    std::ostringstream data;
    data << stream.rdbuf();
    if (stream.bad() || data.bad()) throw std::runtime_error("Cannot finish reading " + win::Narrow(path.wstring()));
    return data.str();
}

void WriteAtomic(const fs::path& path, const std::string& value) {
    fs::path staged = path.parent_path() / (".ripple-patch-" + win::RandomHex(12));
    try {
        std::ofstream stream(staged, std::ios::binary);
        stream.write(value.data(), static_cast<std::streamsize>(value.size()));
        stream.close();
        if (!stream || !MoveFileExW(staged.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot safely replace " + win::Narrow(path.wstring()));
    } catch (...) {
        std::error_code error;
        fs::remove(staged, error);
        throw;
    }
}

bool IsHash(const std::string& value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}

std::string PackageKey(std::string filename) {
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return filename;
}

json Recipes() {
    fs::path file = store::DataDir() / "package-patches.json";
    if (!fs::exists(file)) return json::array();
    json catalog = json::parse(Read(file));
    if (catalog.at("schema") != 1 || !catalog.at("recipes").is_array())
        throw std::runtime_error("Unsupported package patch manifest");
    std::set<std::string> ids;
    for (const auto& recipe : catalog.at("recipes")) {
        const auto id = recipe.at("id").get<std::string>();
        const auto name = recipe.at("package").get<std::string>();
        const auto base = recipe.at("baselineSha256").get<std::string>();
        if (id.empty() || !ids.insert(id).second || !PackageKey(name).ends_with(".upk") ||
            name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") != std::string::npos ||
            !IsHash(base) || !recipe.at("edits").is_array() || recipe.at("edits").empty())
            throw std::runtime_error("Invalid package patch recipe: " + id);
    }
    return catalog.at("recipes");
}

Selection Enabled() {
    const auto path = store::StateDir() / kSelection;
    if (!fs::exists(path)) return {};
    return json::parse(Read(path)).at("enabled").get<Selection>();
}

struct Plan {
    std::string filename;
    std::string baselineHash;
    std::map<int, json> edits;
};

std::map<std::string, Plan> Plans(const json& recipes, const Selection& enabled) {
    std::map<std::string, Plan> plans;
    for (const auto& recipe : recipes) {
        const auto filename = recipe.at("package").get<std::string>();
        const auto hash = recipe.at("baselineSha256").get<std::string>();
        auto& plan = plans[PackageKey(filename)];
        if (plan.filename.empty()) {
            plan.filename = filename;
            plan.baselineHash = hash;
        } else if (plan.baselineHash != hash) {
            throw std::runtime_error("Conflicting supported baselines for " + filename);
        }
        if (!enabled.contains(recipe.at("id").get<std::string>())) continue;
        for (const auto& edit : recipe.at("edits")) {
            const int number = edit.at("export").get<int>();
            if (number < 1 || !plan.edits.emplace(number, edit).second)
                throw std::runtime_error("Package patch conflict in " + filename + ", export " + std::to_string(number));
        }
    }
    return plans;
}

bool GameRunning() {
    HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot check whether Rocket League is running");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(processes, &entry)) {
        CloseHandle(processes);
        throw std::runtime_error("Cannot enumerate running processes");
    }
    bool found = false;
    do {
        found = _wcsicmp(entry.szExeFile, L"RocketLeague.exe") == 0 ||
                _wcsicmp(entry.szExeFile, L"RocketLeague_EAC.exe") == 0;
    } while (!found && Process32NextW(processes, &entry));
    CloseHandle(processes);
    return found;
}

fs::path Library(const fs::path& cooked, const Plan& plan) {
    std::wstring directory = cooked.wstring();
    CharLowerBuffW(directory.data(), static_cast<DWORD>(directory.size()));
    const auto scope = win::Narrow(directory) + "\n" + PackageKey(plan.filename) + "\n" + plan.baselineHash;
    return store::BackupDir() / "package-patches" / hashing::Sha256Hex(scope).substr(0, 32);
}

fs::path VariantPath(const fs::path& library, const std::string& hash) {
    return library / "variants" / (hash.substr(0, 32) + ".upk");
}

std::string Generate(const fs::path& library, const fs::path& baseline, const Plan& plan) {
    fs::path scratch = library / ("build-" + win::RandomHex(12));
    fs::path request = scratch;
    request += ".json";
    fs::path output = scratch;
    output += ".upk";
    json edits = json::array();
    for (const auto& [number, edit] : plan.edits) edits.push_back(edit);
    auto cleanup = [&] {
        std::error_code error;
        fs::remove(request, error);
        fs::remove(output, error);
    };
    try {
        WriteAtomic(request, json{{"baselineSha256", plan.baselineHash}, {"edits", edits}}.dump());
        std::string result;
        if (win::RunPythonModule(L"ripple.patch_recipe", store::AppDir(),
                                 {request, baseline, output, store::DataDir()}, result) != 0)
            throw std::runtime_error("Package writer failed for " + plan.filename + ": " + result);
        auto bytes = Read(output);
        if (json::parse(result).at("outputSha256") != hashing::Sha256Hex(bytes))
            throw std::runtime_error("Generated package failed hash verification");
        cleanup();
        return bytes;
    } catch (...) {
        cleanup();
        throw;
    }
}

std::string FileState(const fs::path& cooked, const Plan& plan) {
    if (cooked.empty()) return "no-install";
    const auto target = cooked / plan.filename;
    if (!fs::is_regular_file(target)) return "missing";
    const auto hash = hashing::Sha256Hex(Read(target));
    if (hash == plan.baselineHash) return "original";
    const auto variant = VariantPath(Library(cooked, plan), hash);
    if (fs::is_regular_file(variant) && hashing::Sha256Hex(Read(variant)) == hash)
        return "patched";
    return "unsupported";
}

json View(const json& recipes, const Selection& enabled) {
    auto cooked = store::CookedDir();
    if (!cooked.empty()) cooked = fs::canonical(cooked);
    const auto plans = Plans(recipes, enabled);
    std::map<std::string, std::string> states;
    for (const auto& [key, plan] : plans) states[key] = FileState(cooked, plan);
    json rows = json::array();
    for (const auto& recipe : recipes) {
        const auto id = recipe.at("id").get<std::string>();
        rows.push_back({{"id", id}, {"label", recipe.at("label")}, {"enabled", enabled.contains(id)},
                        {"state", states.at(PackageKey(recipe.at("package").get<std::string>()))}});
    }
    return {{"ok", true}, {"patches", rows}};
}

struct Change {
    fs::path target;
    std::string before;
    std::string after;
};

}  // namespace

namespace package_patches {

json Get() {
    return View(Recipes(), Enabled());
}

json Set(const json& request) {
    const auto id = request.at("id").get<std::string>();
    const bool on = request.at("enabled").get<bool>();
    const auto recipes = Recipes();
    if (std::none_of(recipes.begin(), recipes.end(), [&id](const auto& recipe) { return recipe.at("id") == id; }))
        throw std::runtime_error("Unknown package patch");
    auto enabled = Enabled();
    if (on) enabled.insert(id);
    else enabled.erase(id);
    auto view = View(recipes, enabled);  // Also checks selected recipes for conflicts.
    WriteAtomic(store::StateDir() / kSelection, json{{"enabled", enabled}}.dump(2));
    return view;
}

json Apply(const fs::path& cookedDir) {
    if (cookedDir.empty()) throw std::runtime_error("Set your Rocket League install in Settings first");
    const auto plans = Plans(Recipes(), Enabled());
    const auto cooked = fs::canonical(cookedDir);
    json results = json::object();
    std::vector<Change> changes;
    for (const auto& [key, plan] : plans) {
        const auto library = Library(cooked, plan);
        if (plan.edits.empty() && !fs::is_regular_file(library / "original.upk")) continue;
        const auto target = cooked / plan.filename;
        if (!fs::is_regular_file(target) || fs::canonical(target).parent_path() != cooked) {
            results[plan.filename] = "missing-or-redirected";
            continue;
        }
        auto current = Read(target);
        const auto currentHash = hashing::Sha256Hex(current);
        const auto variants = library / "variants";
        const auto recognized = VariantPath(library, currentHash);
        if (currentHash != plan.baselineHash &&
            (!fs::is_regular_file(recognized) || hashing::Sha256Hex(Read(recognized)) != currentHash)) {
            results[plan.filename] = "unsupported";
            continue;
        }
        if (plan.edits.empty() && currentHash == plan.baselineHash) {
            results[plan.filename] = "original";
            continue;
        }
        if (GameRunning()) throw std::runtime_error("Close Rocket League before applying package patches");
        fs::create_directories(variants);
        const auto original = library / "original.upk";
        if (!fs::exists(original) && currentHash == plan.baselineHash) WriteAtomic(original, current);
        auto baseline = Read(original);
        if (hashing::Sha256Hex(baseline) != plan.baselineHash)
            throw std::runtime_error("Package backup failed verification: " + plan.filename);
        const bool restore = plan.edits.empty();
        auto desired = restore ? std::move(baseline) : Generate(library, original, plan);
        if (!restore) {
            const auto destination = VariantPath(library, hashing::Sha256Hex(desired));
            if (!fs::exists(destination)) WriteAtomic(destination, desired);
            if (Read(destination) != desired) throw std::runtime_error("Package variant failed verification");
        }
        if (desired == current) {
            results[plan.filename] = "already-patched";
            continue;
        }
        changes.push_back({target, std::move(current), std::move(desired)});
        results[plan.filename] = restore ? "restored" : "patched";
    }
    size_t committed = 0;
    try {
        for (const auto& change : changes) {
            if (GameRunning() || Read(change.target) != change.before)
                throw std::runtime_error("Game or package changed while preparing the patch");
            WriteAtomic(change.target, change.after);
            ++committed;
            if (Read(change.target) != change.after) throw std::runtime_error("Installed package failed verification");
        }
    } catch (...) {
        auto failure = std::current_exception();
        bool restored = true;
        while (committed > 0) {
            const auto& change = changes[--committed];
            try {
                const auto current = Read(change.target);
                if (current == change.after) WriteAtomic(change.target, change.before);
                if (Read(change.target) != change.before) restored = false;
            } catch (...) {
                restored = false;
            }
        }
        if (!restored)
            throw std::runtime_error("Package update failed and its rollback could not be verified. The verified backups were retained.");
        std::rethrow_exception(failure);
    }
    return results;
}

}  // namespace package_patches
