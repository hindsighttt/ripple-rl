#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

namespace package_patches {

nlohmann::json Get();
nlohmann::json Set(const nlohmann::json& request);
nlohmann::json Apply(const std::filesystem::path& cookedDir);

}  // namespace package_patches
