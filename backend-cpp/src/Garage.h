#pragma once
// Native ports of garage_patch.py (ground-truth file swap, hash-guarded) and
// flip_patch.py (TAGame material flip-reset colour, UPK/AES/zlib).
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using json = nlohmann::json;

namespace garage {

bool HasPatchData(const std::filesystem::path& patchDir);  // patch-manifest.json present?
json Status(const std::filesystem::path& cookedDir, const std::filesystem::path& patchDir);
json Apply(const std::filesystem::path& cookedDir, const std::filesystem::path& patchDir,
           const std::filesystem::path& backupDir);
json Revert(const std::filesystem::path& cookedDir, const std::filesystem::path& patchDir,
            const std::filesystem::path& backupDir);

// bridge.py command wrappers (install/RL-running guards + persisted state).
json StatusCmd();
json ApplyCmd();
json RevertCmd();

}  // namespace garage

namespace flip {

// flip_patch.build: recolour the flip-reset material; rgb == -1 returns base.
std::string Build(const std::string& base, long long rgb);
json Prepare(const std::filesystem::path& cookedDir, const std::filesystem::path& patchDir,
             const std::filesystem::path& backupDir, long long rgb);

}  // namespace flip
