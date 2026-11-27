#include "Bootstrap.h"

#include <windows.h>

#include "Aes.h"
#include "Hash.h"
#include "Store.h"
#include "Upk.h"
#include "Zlib.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

// ---- little-endian field access (bounds-checked) ----
int32_t RdI32(const std::string& b, long long o) {
    if (o < 0 || (size_t)(o + 4) > b.size()) throw std::runtime_error("projectx: read past end");
    int32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
uint32_t RdU32(const std::string& b, long long o) {
    if (o < 0 || (size_t)(o + 4) > b.size()) throw std::runtime_error("projectx: read past end");
    uint32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
int64_t RdI64(const std::string& b, long long o) {
    if (o < 0 || (size_t)(o + 8) > b.size()) throw std::runtime_error("projectx: read past end");
    int64_t v;
    std::memcpy(&v, b.data() + o, 8);
    return v;
}
void WrI32(std::string& b, long long o, int32_t v) {
    if (o < 0 || (size_t)(o + 4) > b.size()) throw std::runtime_error("projectx: write past end");
    std::memcpy(&b[o], &v, 4);
}
void WrU32(std::string& b, long long o, uint32_t v) {
    if (o < 0 || (size_t)(o + 4) > b.size()) throw std::runtime_error("projectx: write past end");
    std::memcpy(&b[o], &v, 4);
}
void AppI32(std::string& b, int32_t v) { b.append((const char*)&v, 4); }
void AppI64(std::string& b, int64_t v) { b.append((const char*)&v, 8); }

constexpr uint32_t kUnrealTag = 0x9E2A83C1u;
const char* kGuardGetUrl = "f3c9120da415906a138ad2a125013108dc2e85125f65f0b2c4b34db99e4e38df";
const char* kGuardInit = "859c94b1fe6b5d5be301da415d7f94631c0ae9cf92ab9a82155c12a9c242eec1";
const char* kNameGetUrl = "PsyNetConnection_X.GetPsyNetURL";
const char* kNameInit = "PsyNetMessengerWebSocket_X.Init";

std::string DecodeChunkImpl(const std::string& chunk) {
    uint32_t tag = RdU32(chunk, 0), block_size = RdU32(chunk, 4);
    uint32_t compressed_size = RdU32(chunk, 8), size = RdU32(chunk, 12);
    if (tag != kUnrealTag || block_size < 1 || block_size > 1048576)
        throw std::runtime_error("Unsupported ProjectX compressed chunk");
    long long count = (size + (long long)block_size - 1) / block_size;  // ceil
    long long cursor = 16 + count * 8;
    std::string result;
    for (long long i = 0; i < count; ++i) {
        uint32_t comp = RdU32(chunk, 16 + i * 8);
        uint32_t unc = RdU32(chunk, 16 + i * 8 + 4);
        if (cursor < 0 || (size_t)(cursor + comp) > chunk.size())
            throw std::runtime_error("ProjectX compressed block out of range");
        std::string raw = zlibx::Inflate(chunk.substr((size_t)cursor, comp), unc);
        if (raw.size() != unc) throw std::runtime_error("ProjectX compression block length mismatch");
        result += raw;
        cursor += comp;
    }
    if (cursor != (long long)chunk.size() || result.size() != size ||
        compressed_size != (uint32_t)((long long)chunk.size() - 16 - count * 8))
        throw std::runtime_error("ProjectX compressed chunk boundary mismatch");
    return result;
}

std::string EncodeChunk(const std::string& data) {
    const long long block_size = 131072;
    std::vector<std::string> blocks, compressed;
    for (size_t off = 0; off < data.size() || (data.empty() && blocks.empty()); off += block_size) {
        blocks.push_back(data.substr(off, (size_t)block_size));
        if (data.empty()) break;
    }
    if (data.empty()) blocks = {std::string()};
    long long total = 0;
    for (auto& b : blocks) {
        compressed.push_back(zlibx::Store(b));
        total += (long long)compressed.back().size();
    }
    std::string out;
    AppI32(out, (int32_t)kUnrealTag);
    AppI32(out, (int32_t)block_size);
    AppI32(out, (int32_t)total);
    AppI32(out, (int32_t)data.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        AppI32(out, (int32_t)compressed[i].size());
        AppI32(out, (int32_t)blocks[i].size());
    }
    for (auto& c : compressed) out += c;
    return out;
}

// _function(record, script, reference_count)
std::string PatchFunction(const std::string& record, const std::string& script, int reference_count) {
    uint32_t stored_size = RdU32(record, 44);
    std::string prefix = record.substr(0, 48);
    WrU32(prefix, 40, (uint32_t)(script.size() + reference_count * 4));
    WrU32(prefix, 44, (uint32_t)script.size());
    std::string tail = (48 + (long long)stored_size <= (long long)record.size())
                           ? record.substr(48 + stored_size)
                           : std::string();
    return prefix + script + tail;
}

// Port of patches.ProjectX.
struct ProjectX {
    upk::Header h;
    std::string decoded;
    std::string key;
    std::vector<long long> export_positions;
    long long map_offset = 0;
    std::vector<std::string> records;                       // 36-byte chunk-map records
    std::vector<std::pair<long long, std::string>> regions;  // (start, decompressed)
    std::vector<std::string> names;
    std::map<std::string, long long> exports;

    ProjectX(const std::string& data, const std::vector<std::string>& keys) {
        upk::Decrypted d = upk::TryDecrypt(data, keys);
        h = d.h;
        decoded = std::move(d.decoded);
        key = std::move(d.key);

        long long cursor = h.eo - h.no;
        for (long long i = 0; i < h.ec; ++i) {
            if (cursor < 0 || cursor + 72 > (long long)decoded.size())
                throw std::runtime_error("Truncated script-package export table");
            int32_t net_count = RdI32(decoded, cursor + 48);
            if (net_count < 0 || net_count > 65536)
                throw std::runtime_error("Invalid script-package export net count");
            export_positions.push_back(cursor);
            cursor += 72 + (long long)net_count * 4;
        }
        if (cursor + h.no != h.dep)
            throw std::runtime_error("Unsupported script-package export table boundary");

        map_offset = h.no + (long long)RdU32(data, h.no - 20);
        uint32_t count = RdU32(decoded, map_offset - h.no);
        if (count < 1 || count > 4096) throw std::runtime_error("Invalid ProjectX chunk map");
        for (uint32_t index = 0; index < count; ++index) {
            long long base = map_offset - h.no + 4 + (long long)index * 36;
            if (base < 0 || (size_t)(base + 36) > decoded.size())
                throw std::runtime_error("ProjectX chunk record out of range");
            std::string record = decoded.substr((size_t)base, 36);
            int64_t start = RdI64(record, 0);
            int32_t size = RdI32(record, 8);
            int64_t comp_off = RdI64(record, 12);
            int32_t comp_size = RdI32(record, 20);
            if (comp_off < 0 || (size_t)(comp_off + comp_size) > data.size())
                throw std::runtime_error("ProjectX chunk slice out of range");
            std::string raw = DecodeChunkImpl(data.substr((size_t)comp_off, comp_size));
            if ((int32_t)raw.size() != size) throw std::runtime_error("ProjectX mapped chunk length mismatch");
            records.push_back(record);
            regions.push_back({start, raw});
        }

        cursor = 0;
        for (long long i = 0; i < h.nc; ++i) {
            int32_t length = RdI32(decoded, cursor);
            if (length < 1 || length > 65536) throw std::runtime_error("Invalid ProjectX name");
            names.push_back(decoded.substr((size_t)(cursor + 4), (size_t)(length - 1)));
            cursor += length + 12;
        }
        if (cursor + h.no != h.io) throw std::runtime_error("ProjectX name table boundary mismatch");

        for (long long index = 0; index < h.ec; ++index) exports[ObjectPath(index + 1)] = index;
    }

    std::string ObjectPath(long long index) {
        std::vector<std::string> parts;
        std::set<long long> seen;
        while (index != 0) {
            if (seen.count(index)) throw std::runtime_error("Cyclic ProjectX object path");
            seen.insert(index);
            long long entry;
            int name_offset, parent_offset;
            if (index < 0) {
                entry = h.io - h.no + (-index - 1) * 28;
                name_offset = 20;
                parent_offset = 16;
            } else {
                entry = export_positions[(size_t)(index - 1)];
                name_offset = 12;
                parent_offset = 8;
            }
            int32_t name = RdI32(decoded, entry + name_offset);
            if (name < 0 || (size_t)name >= names.size())
                throw std::runtime_error("ProjectX name index out of range");
            parts.push_back(names[(size_t)name]);
            index = RdI32(decoded, entry + parent_offset);
        }
        std::string out;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!out.empty()) out += ".";
            out += *it;
        }
        return out;
    }

    long long ExportPosition(const std::string& name) {
        auto it = exports.find(name);
        if (it == exports.end()) throw std::runtime_error("ProjectX export not found: " + name);
        return export_positions[(size_t)it->second];
    }

    std::string Export(const std::string& name) {
        long long entry = ExportPosition(name);
        int32_t size = RdI32(decoded, entry + 32);
        int32_t offset = RdI32(decoded, entry + 36);
        if (size == 0) return "";
        for (auto& [start, raw] : regions)
            if (start <= offset && offset + size <= start + (long long)raw.size())
                return raw.substr((size_t)(offset - start), (size_t)size);
        throw std::runtime_error("Unmapped ProjectX export " + name);
    }
};

// max(offset + len(raw) for offset, raw in regions)
long long RegionsEnd(const ProjectX& p) {
    long long end = 0;
    bool first = true;
    for (auto& [off, raw] : p.regions) {
        long long e = off + (long long)raw.size();
        if (first || e > end) {
            end = e;
            first = false;
        }
    }
    return end;
}

std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("bootstrap: cannot open " + p.string());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void WriteAtomic(const fs::path& target, const std::string& data) {
    fs::path tmp = target;
    tmp += ".ripple-bootstrap-tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out || !MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw std::runtime_error("Could not replace " + target.filename().string() + ".");
    }
}

}  // namespace

namespace bootstrap {

std::string Build(const std::string& data, const std::vector<std::string>& keys, int port) {
    ProjectX package(data, keys);
    std::string rec_get = package.Export(kNameGetUrl);
    std::string rec_init = package.Export(kNameInit);

    std::string http_url = "http://127.0.0.1:" + std::to_string(port);
    std::string socket_url = "ws://127.0.0.1:" + std::to_string(port) + "/ws/gc2";
    if (rec_get.find(http_url) != std::string::npos && rec_init.find(socket_url) != std::string::npos)
        return data;  // already patched

    if (hashing::Sha256Hex(rec_get) != kGuardGetUrl)
        throw std::runtime_error(std::string("Unsupported game function revision: ") + kNameGetUrl);
    if (hashing::Sha256Hex(rec_init) != kGuardInit)
        throw std::runtime_error(std::string("Unsupported game function revision: ") + kNameInit);

    // get_url: 0x04 0x1f <http_url> 0x00 0x4c
    std::string script_get;
    script_get.push_back((char)0x04);
    script_get.push_back((char)0x1f);
    script_get += http_url;
    script_get.push_back((char)0x00);
    script_get.push_back((char)0x4c);
    std::string get_url = PatchFunction(rec_get, script_get, 0);

    // init: old_script[:6] 0x1f <socket_url> 0x00 old_script[11:]
    std::string old_script = rec_init.substr(48, 25);  // [48:73]
    int32_t dest = RdI32(old_script, 2);
    int32_t source = RdI32(old_script, 7);
    if (package.ObjectPath(dest) != "PsyNetMessengerWebSocket_X.URL" ||
        package.ObjectPath(source) != "PsyNetMessengerWebSocket_X.Init.InURL")
        throw std::runtime_error("Unsupported WebSocket URL assignment");
    std::string script_init = old_script.substr(0, 6);
    script_init.push_back((char)0x1f);
    script_init += socket_url;
    script_init.push_back((char)0x00);
    script_init += old_script.substr(11);
    std::string init_fn = PatchFunction(rec_init, script_init, 3);

    long long start = RegionsEnd(package);
    std::string payload = get_url + init_fn;
    std::string chunk = EncodeChunk(payload);

    upk::Header h = package.h;
    long long table_start = package.map_offset - h.no;
    long long old_table_end = package.map_offset + 4 + (long long)package.records.size() * 36;
    uint32_t garbage = RdU32(data, h.no - 24);
    if (old_table_end + (long long)garbage != h.hs || garbage < 36)
        throw std::runtime_error("Unsupported ProjectX chunk-map padding");

    WrI32(package.decoded, package.ExportPosition(kNameGetUrl) + 32, (int32_t)get_url.size());
    WrI32(package.decoded, package.ExportPosition(kNameGetUrl) + 36, (int32_t)start);
    WrI32(package.decoded, package.ExportPosition(kNameInit) + 32, (int32_t)init_fn.size());
    WrI32(package.decoded, package.ExportPosition(kNameInit) + 36, (int32_t)(start + (long long)get_url.size()));
    WrU32(package.decoded, table_start, (uint32_t)(package.records.size() + 1));

    std::string new_map;
    AppI64(new_map, start);
    AppI32(new_map, (int32_t)payload.size());
    AppI64(new_map, (int64_t)data.size());
    AppI32(new_map, (int32_t)chunk.size());
    AppI32(new_map, 0);
    AppI32(new_map, 0);
    AppI32(new_map, 0);
    long long position = old_table_end - h.no;
    if (position < 0 || (size_t)(position + 36) > package.decoded.size())
        throw std::runtime_error("ProjectX new chunk-map record out of range");
    package.decoded.replace((size_t)position, 36, new_map);

    std::string result = data;
    WrU32(result, h.no - 24, garbage - 36);
    long long encrypted_size = (h.hs - h.no) / 16 * 16;
    std::string plain = package.decoded.substr(0, (size_t)std::min<long long>(encrypted_size, (long long)package.decoded.size()));
    std::string enc = aes::EcbEncrypt(package.key, plain);
    result.replace((size_t)h.no, (size_t)encrypted_size, enc);
    result += chunk;

    ProjectX rebuilt(result, keys);
    if (rebuilt.Export(kNameGetUrl) != get_url || rebuilt.Export(kNameInit) != init_fn)
        throw std::runtime_error("ProjectX patch verification failed");
    return result;
}

std::string ExportBytes(const std::string& data, const std::vector<std::string>& keys,
                        const std::string& name) {
    ProjectX package(data, keys);
    return package.Export(name);
}

std::string DecodeChunk(const std::string& chunk) { return DecodeChunkImpl(chunk); }

json Prepare(const fs::path& cookedDir, const fs::path& dataDir, const fs::path& backupDir) {
    const auto active = store::ReadJson("proxy-bootstrap.json", json::object());
    if (active.value("changed", false) &&
        fs::path(active.value("cookedDir", std::string{})) != cookedDir)
        RestoreActive();
    fs::path target = cookedDir / "ProjectX.upk";
    std::string original = ReadAll(target);
    std::string modified = Build(original, upk::LoadKeys(dataDir));
    if (modified == original) return json{{"changed", false}};
    std::string hashHex = hashing::Sha256Hex(original);
    fs::path backup = backupDir / "proxy-bootstrap" / hashHex / "ProjectX.upk";
    fs::create_directories(backup.parent_path());
    if (!fs::exists(backup)) WriteAtomic(backup, original);
    if (ReadAll(backup) != original)
        throw std::runtime_error("The ProjectX proxy backup does not match the original.");
    const json state{{"changed", true}, {"backup", backup.string()}, {"cookedDir", cookedDir.string()},
                     {"originalSha256", hashHex}, {"patchedSha256", hashing::Sha256Hex(modified)}};
    WriteAtomic(store::StateDir() / "proxy-bootstrap.json", state.dump());
    WriteAtomic(target, modified);
    return state;
}

bool RestoreActive() {
    json state = store::ReadJson("proxy-bootstrap.json", json::object());
    if (!state.value("changed", false) || !state.contains("backup")) return false;
    fs::path backup = state.value("backup", std::string(""));
    std::string original = ReadAll(backup);
    const auto originalHash = state.value("originalSha256", std::string{});
    if (!originalHash.empty() && hashing::Sha256Hex(original) != originalHash)
        throw std::runtime_error("The saved ProjectX backup has changed.");
    fs::path target = fs::path(state.value("cookedDir", std::string(""))) / "ProjectX.upk";
    std::string current = ReadAll(target);
    if (current != original) {
        const auto patchedHash = state.value("patchedSha256", std::string{});
        const bool knownPatch = patchedHash.empty() ? Build(original, upk::LoadKeys(store::DataDir())) == current
                                                    : hashing::Sha256Hex(current) == patchedHash;
        if (!knownPatch)
            throw std::runtime_error("ProjectX changed after proxy preparation; retained its backup");
        WriteAtomic(target, original);
    }
    state["changed"] = false;
    state["restored"] = true;
    WriteAtomic(store::StateDir() / "proxy-bootstrap.json", state.dump());
    return true;
}

}  // namespace bootstrap
