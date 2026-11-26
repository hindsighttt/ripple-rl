#include "Garage.h"

#include "Aes.h"
#include "Bootstrap.h"
#include "Hash.h"
#include "Home.h"
#include "Store.h"
#include "Upk.h"
#include "WinUtil.h"
#include "Zlib.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* kPackages[] = {"TAGame.upk", "Engine.upk", "GFxShell_SF.upk",
                           "GFX_PremiumContainerMenu_SF.upk", "GFX_Hud_SF.upk"};
// flip_patch constants
const long long kPhysicalNames = 3865;
// Native material's exact default floats.
const unsigned char kDefaultValuesHex[] = {
    0xb5, 0x0e, 0x13, 0x3f, 0x83, 0x19, 0x7f, 0x3f, 0xb5, 0x0e, 0x13, 0x3f, 0x35, 0x0f, 0x18, 0x3f,
    0x83, 0x19, 0x7f, 0x3f, 0xb5, 0x0e, 0x13, 0x3f, 0x35, 0x0f, 0x18, 0x3f, 0x83, 0x19, 0x7f, 0x3f};

std::string DefaultValues() { return std::string((const char*)kDefaultValuesHex, 32); }

// ---- little-endian helpers ----
int32_t RdI32(const std::string& b, long long o) {
    if (o < 0 || (size_t)(o + 4) > b.size()) throw std::runtime_error("flip: read past end");
    int32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
uint32_t RdU32(const std::string& b, long long o) {
    if (o < 0 || (size_t)(o + 4) > b.size()) throw std::runtime_error("flip: read past end");
    uint32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
int64_t RdI64(const std::string& b, long long o) {
    if (o < 0 || (size_t)(o + 8) > b.size()) throw std::runtime_error("flip: read past end");
    int64_t v;
    std::memcpy(&v, b.data() + o, 8);
    return v;
}
void WrI32(std::string& b, long long o, int32_t v) {
    if (o < 0 || (size_t)(o + 4) > b.size()) throw std::runtime_error("flip: write past end");
    std::memcpy(&b[o], &v, 4);
}
void AppU32(std::string& b, uint32_t v) { b.append((const char*)&v, 4); }

std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void WriteAtomic(const fs::path& target, const std::string& data) {
    const auto tmp = target.parent_path() / (".ripple-garage-" + win::RandomHex(12));
    try {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(data.data(), (std::streamsize)data.size());
        f.close();
        if (!f || !MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot safely replace " + target.string());
    } catch (...) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw;
    }
}
std::string Sha256File(const fs::path& p) { return hashing::Sha256Hex(ReadAll(p)); }

bool UsesRecipe(const fs::path& patchDir) { return patchDir == store::GarageRecipeDir(); }

fs::path ManifestPath(const fs::path& patchDir) {
    return UsesRecipe(patchDir) ? store::DataDir() / "garage-recipe.json" : patchDir / "patch-manifest.json";
}

json LoadManifest(const fs::path& patchDir) {
    auto manifest = json::parse(ReadAll(ManifestPath(patchDir)));
    if (UsesRecipe(patchDir) && manifest.value("schema", 0) != 1)
        throw std::runtime_error("Unsupported Garage recipe schema");
    return manifest;
}

bool ManagedFlip(const fs::path& live, const std::string& base, const std::string& current, const json& state) {
    return live.filename() == "TAGame.upk" && state.is_object() &&
           state.value("path", std::string()) == fs::absolute(live).string() &&
           state.value("baseHash", std::string()) == base && state.value("installedHash", std::string()) == current;
}

// _state(live, entry)
std::string StateOf(const fs::path& live, const json& entry, const json& flip) {
    std::error_code ec;
    if (!fs::is_regular_file(live, ec)) return "missing";
    std::string sha = Sha256File(live);
    const auto patched = entry.value("patchedSha256", std::string());
    if (sha == patched || sha == entry.value("previousPatchedSha256", std::string()) ||
        ManagedFlip(live, patched, sha, flip)) return "patched";
    if (sha == entry.value("vanillaSha256", std::string(""))) return "vanilla";
    return "unknown";
}

fs::path FindCleanBackup(const fs::path& patchDir, const fs::path& backupDir,
                         const fs::path& name, const std::string& hash) {
    const auto matches = [&hash](const fs::path& p) { return fs::is_regular_file(p) && Sha256File(p) == hash; };
    const auto backup = backupDir / name;
    if (matches(backup)) return backup;
    if (!UsesRecipe(patchDir)) {
        const auto captured = patchDir / "before" / name;
        return matches(captured) ? captured : fs::path();
    }
    // Restore installations patched by the previous native release if their clean backup still exists.
    const auto previous = store::BackupDir() / "garage-verified-epic" / name;
    if (matches(previous)) return previous;
    const auto original = store::BackupDir() / "garage" / name;
    return matches(original) ? original : fs::path();
}

std::string GeneratedPackage(const fs::path& patchDir, const fs::path& baseline,
                             const fs::path& name, const std::string& wanted) {
    const fs::path cache = patchDir / "after" / name;
    if (fs::is_regular_file(cache)) {
        auto bytes = ReadAll(cache);
        if (hashing::Sha256Hex(bytes) == wanted) return bytes;
    }
    fs::create_directories(cache.parent_path());
    const fs::path temporary = cache.parent_path() / (".garage-build-" + win::RandomHex(12));
    try {
        std::string output;
        if (win::RunPythonModule(L"ripple.garage_recipe", store::AppDir(),
                                 {ManifestPath(patchDir), baseline, temporary, store::DataDir()}, output) != 0)
            throw std::runtime_error("Garage recipe failed for " + name.string() + ": " + output);
        auto bytes = ReadAll(temporary);
        if (hashing::Sha256Hex(bytes) != wanted)
            throw std::runtime_error("Garage writer output failed verification: " + name.string());
        if (!MoveFileExW(temporary.c_str(), cache.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot publish generated Garage package: " + name.string());
        return bytes;
    } catch (...) {
        std::error_code ec;
        fs::remove(temporary, ec);
        throw;
    }
}

// flip_patch _colour_bytes: floats [r,b,r,g,b,r,g,b], each red/green/blue / 255.
std::string ColourBytes(long long rgb) {
    double r = ((rgb >> 16) & 255) / 255.0;
    double g = ((rgb >> 8) & 255) / 255.0;
    double b = (rgb & 255) / 255.0;
    float arr[8] = {(float)r, (float)b, (float)r, (float)g, (float)b, (float)r, (float)g, (float)b};
    std::string o(32, '\0');
    std::memcpy(&o[0], arr, 32);
    return o;
}

long long CountOccurrences(const std::string& hay, const std::string& needle) {
    long long n = 0;
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

}  // namespace

namespace garage {

bool HasPatchData(const fs::path& patchDir) {
    std::error_code ec;
    return fs::is_regular_file(ManifestPath(patchDir), ec);
}

json Status(const fs::path& cookedDir, const fs::path& patchDir) {
    json man = LoadManifest(patchDir);
    json packages = man.value("packages", json::object());
    const auto flip = store::ReadJson("flip-reset-patch.json", json::object());
    json out = json::object();
    for (const char* name : kPackages) {
        if (!packages.contains(name)) {
            out[name] = "no-patch-data";
            continue;
        }
        out[name] = StateOf(cookedDir / name, packages[name], flip);
    }
    return out;
}

json Apply(const fs::path& cookedDir, const fs::path& patchDir, const fs::path& backupDir) {
    json man = LoadManifest(patchDir);
    json packages = man.value("packages", json::object());
    const auto afterDir = patchDir / "after";
    const auto flip = store::ReadJson("flip-reset-patch.json", json::object());
    const bool recipe = UsesRecipe(patchDir);
    fs::create_directories(backupDir);
    json results = json::object();
    struct Change {
        fs::path live, backup;
        std::string vanilla, patched, data;
    };
    std::vector<Change> changes;
    for (const char* name : kPackages) {
        fs::path live = cookedDir / name;
        if (!packages.contains(name)) {
            results[name] = "no-patch-data";
            continue;
        }
        json entry = packages[name];
        const auto vanillaHash = entry.at("vanillaSha256").get<std::string>();
        const auto patchedHash = entry.at("patchedSha256").get<std::string>();
        std::string st = StateOf(live, entry, flip);
        if (st == "patched") {
            if (recipe && std::string_view(name) == "TAGame.upk" && !fs::is_regular_file(afterDir / name)) {
                const auto bytes = ReadAll(live);
                if (hashing::Sha256Hex(bytes) == patchedHash) {
                    fs::create_directories(afterDir);
                    WriteAtomic(afterDir / name, bytes);
                } else {
                    const auto backup = FindCleanBackup(patchDir, backupDir, name, vanillaHash);
                    if (!backup.empty()) GeneratedPackage(patchDir, backup, name, patchedHash);
                }
            }
            results[name] = "already-patched";
            continue;
        }
        if (st != "vanilla") {
            results[name] = "skipped-" + st;
            continue;
        }
        fs::path backup = backupDir / name;
        if (!fs::exists(backup)) {
            auto original = ReadAll(live);
            if (hashing::Sha256Hex(original) == vanillaHash) WriteAtomic(backup, original);
        }
        if (!fs::is_regular_file(backup) || Sha256File(backup) != vanillaHash) {
            results[name] = "baseline-changed";
            continue;
        }
        auto patched = recipe ? GeneratedPackage(patchDir, backup, name, patchedHash) : ReadAll(afterDir / name);
        if (hashing::Sha256Hex(patched) != patchedHash) {
            results[name] = "bad-patch-data";
            continue;
        }
        changes.push_back({live, backup, vanillaHash, patchedHash, std::move(patched)});
        results[name] = "patched";
    }
    for (const auto& change : changes) {
        if (Sha256File(change.live) != change.vanilla)
            throw std::runtime_error("Garage baseline changed while preparing " + change.live.filename().string());
    }
    if (!changes.empty() && home::RlRunning())
        throw std::runtime_error("Close Rocket League before installing the prepared Garage patches");
    size_t committed = 0;
    try {
        for (const auto& change : changes) {
            if (Sha256File(change.live) != change.vanilla)
                throw std::runtime_error("Garage package changed before installation");
            WriteAtomic(change.live, change.data);
            ++committed;
            if (Sha256File(change.live) != change.patched)
                throw std::runtime_error("Garage package failed verification after installation");
        }
    } catch (...) {
        const auto failure = std::current_exception();
        bool restored = true;
        while (committed) {
            const auto& change = changes[--committed];
            try {
                const auto original = ReadAll(change.backup);
                if (Sha256File(change.live) == change.patched && hashing::Sha256Hex(original) == change.vanilla)
                    WriteAtomic(change.live, original);
                if (Sha256File(change.live) != change.vanilla) restored = false;
            } catch (...) {
                restored = false;
            }
        }
        if (!restored) throw std::runtime_error("Garage apply failed and rollback could not be verified. Backups retained.");
        std::rethrow_exception(failure);
    }
    return results;
}

json Revert(const fs::path& cookedDir, const fs::path& patchDir, const fs::path& backupDir) {
    json man = LoadManifest(patchDir);
    json packages = man.value("packages", json::object());
    json results = json::object();
    std::error_code ec;
    const auto flip = store::ReadJson("flip-reset-patch.json", json::object());
    for (const char* name : kPackages) {
        fs::path live = cookedDir / name;
        if (!packages.contains(name) || !fs::is_regular_file(live, ec)) {
            results[name] = "missing";
            continue;
        }
        std::string vanilla = packages[name].value("vanillaSha256", std::string(""));
        const auto current = Sha256File(live);
        if (current == vanilla) {
            results[name] = "already-vanilla";
            continue;
        }
        const auto patched = packages[name].value("patchedSha256", std::string(""));
        if (current != patched && current != packages[name].value("previousPatchedSha256", std::string()) &&
            !ManagedFlip(live, patched, current, flip)) {
            results[name] = "skipped-unknown";
            continue;
        }
        const auto backup = FindCleanBackup(patchDir, backupDir, name, vanilla);
        if (!backup.empty() && Sha256File(live) == current) {
            auto original = ReadAll(backup);
            if (hashing::Sha256Hex(original) != vanilla)
                throw std::runtime_error("Garage restore backup changed: " + std::string(name));
            WriteAtomic(live, original);
            if (Sha256File(live) != vanilla)
                throw std::runtime_error("Verification failed after restoring " + std::string(name));
            results[name] = "reverted";
        } else {
            results[name] = "no-clean-backup";
        }
    }
    return results;
}

json StatusCmd() {
    fs::path cooked = store::CookedDir();
    if (cooked.empty()) return json{{"ok", false}, {"error", "no install set"}, {"packages", json::object()}};
    if (!HasPatchData(store::GaragePatchDir()))
        return json{{"ok", false}, {"error", "no patch data for this build"}, {"packages", json::object()}};
    return json{{"ok", true}, {"packages", Status(cooked, store::GaragePatchDir())}};
}

json ApplyCmd() {
    fs::path cooked = store::CookedDir();
    if (cooked.empty()) throw std::runtime_error("Set your Rocket League install in Settings first.");
    if (home::RlRunning()) throw std::runtime_error("Close Rocket League before patching.");
    json result = Apply(cooked, store::GaragePatchDir(), store::GarageBackupDir());
    store::WriteJson("garage-patch.json", json{{"cookedDir", cooked.string()}, {"result", result}});
    return json{{"ok", true}, {"result", result}};
}

json RevertCmd() {
    fs::path cooked = store::CookedDir();
    if (cooked.empty()) throw std::runtime_error("Set your Rocket League install in Settings first.");
    if (home::RlRunning()) throw std::runtime_error("Close Rocket League before reverting.");
    return json{{"ok", true}, {"result", Revert(cooked, store::GaragePatchDir(), store::GarageBackupDir())}};
}

}  // namespace garage

namespace flip {

std::string Build(const std::string& base, long long rgb) {
    if (rgb == -1) return base;
    if (rgb < 0 || rgb > 0xFFFFFF) throw std::runtime_error("Invalid flip-reset RGB colour");

    long long no = kPhysicalNames;
    uint32_t hs = RdU32(base, 8);
    if (!(no < (long long)hs && (long long)hs <= (long long)base.size()))
        throw std::runtime_error("Unsupported flip-reset package header");
    long long encrypted_size = ((long long)hs - no) / 16 * 16;
    std::string decoded = aes::EcbDecrypt(upk::DefaultKey(), base.substr((size_t)no, (size_t)encrypted_size));

    uint32_t map_position = RdU32(base, no - 20);
    if ((long long)map_position + 4 > (long long)decoded.size())
        throw std::runtime_error("Invalid flip-reset compression map");
    uint32_t count = RdU32(decoded, map_position);
    if (!(count >= 1 && count <= 4096) || (long long)map_position + 4 + (long long)count * 36 > (long long)decoded.size())
        throw std::runtime_error("Invalid flip-reset compression records");

    long long entry = (long long)map_position + 4 + ((long long)count - 1) * 36;  // last chunk record
    int32_t raw_size = RdI32(decoded, entry + 8);
    int64_t offset = RdI64(decoded, entry + 12);
    int32_t size = RdI32(decoded, entry + 20);
    if (offset < (long long)hs || offset + size > (long long)base.size())
        throw std::runtime_error("Invalid flip-reset chunk bounds");

    std::string chunk = base.substr((size_t)offset, (size_t)size);
    std::string raw = bootstrap::DecodeChunk(chunk);
    std::string defaults = DefaultValues();
    if ((int32_t)raw.size() != raw_size || CountOccurrences(raw, defaults) != 1)
        throw std::runtime_error("Unsupported flip-reset material revision");
    size_t position = raw.find(defaults);

    uint32_t block_size = RdU32(chunk, 4);
    uint32_t total_size = RdU32(chunk, 12);
    long long block_index = (long long)position / block_size;
    long long within = (long long)position % block_size;
    if (within + 32 > block_size) throw std::runtime_error("Flip-reset colour crosses compression blocks");
    long long blocks_count = ((long long)total_size + block_size - 1) / block_size;

    long long cursor = 16 + blocks_count * 8;
    std::vector<std::pair<std::string, uint32_t>> blocks;
    for (long long index = 0; index < blocks_count; ++index) {
        uint32_t compressed = RdU32(chunk, 16 + index * 8);
        uint32_t uncompressed = RdU32(chunk, 16 + index * 8 + 4);
        std::string encoded = chunk.substr((size_t)cursor, compressed);
        cursor += compressed;
        if (index == block_index) {
            std::string block = zlibx::Inflate(encoded, uncompressed);
            block.replace((size_t)within, 32, ColourBytes(rgb));
            encoded = zlibx::Store(block);  // valid zlib; verified below by decode
        }
        blocks.push_back({encoded, uncompressed});
    }

    long long sumlen = 0;
    for (auto& b : blocks) sumlen += (long long)b.first.size();
    std::string changed;
    AppU32(changed, 0x9E2A83C1);
    AppU32(changed, block_size);
    AppU32(changed, (uint32_t)sumlen);
    AppU32(changed, total_size);
    for (auto& b : blocks) {
        AppU32(changed, (uint32_t)b.first.size());
        AppU32(changed, b.second);
    }
    for (auto& b : blocks) changed += b.first;

    WrI32(decoded, entry + 20, (int32_t)changed.size());

    std::string result = base.substr(0, (size_t)offset);
    std::string enc = aes::EcbEncrypt(upk::DefaultKey(), decoded);
    result.replace((size_t)no, (size_t)encrypted_size, enc);
    result += changed;
    result += base.substr((size_t)(offset + size));

    std::string expected = raw.substr(0, position) + ColourBytes(rgb) + raw.substr(position + 32);
    if (bootstrap::DecodeChunk(changed) != expected)
        throw std::runtime_error("Flip-reset compression verification failed");
    return result;
}

json Prepare(const fs::path& cookedDir, const fs::path& patchDir, const fs::path& backupDir, long long rgb) {
    fs::path target = cookedDir / "TAGame.upk";
    fs::path source = patchDir / "after" / "TAGame.upk";
    json manifest = LoadManifest(patchDir);
    std::string want = manifest.value("packages", json::object())
                           .value("TAGame.upk", json::object())
                           .value("patchedSha256", std::string(""));

    std::string current = ReadAll(target);
    std::string current_hash = hashing::Sha256Hex(current);
    if (current_hash == want && rgb == -1) return json{{"changed", false}};
    const auto state = store::ReadJson("flip-reset-patch.json", json::object());
    std::error_code ec;
    std::string tpath = fs::absolute(target, ec).string();
    if (current_hash != want && !ManagedFlip(target, want, current_hash, state)) {
        if (rgb == -1) return json{{"changed", false}, {"skipped", "unknown-package"}};
        throw std::runtime_error("TAGame changed outside the rebuild; flip-reset colour was not applied");
    }
    const auto base = current_hash == want ? current : ReadAll(source);
    if (hashing::Sha256Hex(base) != want) throw std::runtime_error("Garage patch data failed flip-reset hash verification");
    std::string desired = Build(base, rgb);
    if (current == desired) return json{{"changed", false}};

    fs::path backup = backupDir / "flip-reset" / current_hash / "TAGame.upk";
    fs::create_directories(backup.parent_path(), ec);
    if (!fs::exists(backup, ec)) fs::copy_file(target, backup, ec);
    WriteAtomic(target, desired);
    json info = {{"path", tpath},
                 {"baseHash", want},
                 {"installedHash", hashing::Sha256Hex(desired)},
                 {"colour", rgb},
                 {"backup", backup.string()}};
    store::WriteJson("flip-reset-patch.json", info);
    json out = info;
    out["changed"] = true;
    return out;
}

}  // namespace flip
