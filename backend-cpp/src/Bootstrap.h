#pragma once
// Native port of ripple/patches.py: the ProjectX.upk "proxy bootstrap".
// It rewrites two guarded script functions (PsyNetConnection_X.GetPsyNetURL and
// PsyNetMessengerWebSocket_X.Init) so Rocket League talks to the local proxy
// instead of live PsyNet. Hash-guarded and self-verifying.
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace bootstrap {

// Rebuild ProjectX.upk bytes patched for the local proxy (or return `data`
// unchanged if already patched). Throws on unsupported game revisions.
std::string Build(const std::string& data, const std::vector<std::string>& keys, int port = 49320);

// Prepare the install's ProjectX.upk in place; {changed:bool, backup?:string}.
json Prepare(const std::filesystem::path& cookedDir, const std::filesystem::path& dataDir,
             const std::filesystem::path& backupDir);

// Restore ProjectX.upk from the recorded backup (proxy-bootstrap.json).
bool RestoreActive();

// Extract a named export's raw bytes (used by the verification CLI).
std::string ExportBytes(const std::string& data, const std::vector<std::string>& keys,
                        const std::string& name);

// Decode an Unreal-tagged compressed chunk to its raw bytes (used by flip_patch).
std::string DecodeChunk(const std::string& chunk);

}  // namespace bootstrap
