#pragma once
// Native port of ripple/store.py: the app/data/state directory layout
// and atomic JSON read/write for settings and feature state. Paths resolve the
// same locations the Python backend used (%APPDATA%\RippleRL, <root>\data).
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using json = nlohmann::json;

namespace store {

void Init();  // resolve roots and create the state directories; call once at startup

const std::filesystem::path& AppDir();     // project root (holds ripple\ and data\)
const std::filesystem::path& DataDir();    // <root>\data
const std::filesystem::path& StateDir();   // %APPDATA%\RippleRL
const std::filesystem::path& BackupDir();  // %APPDATA%\RippleRL\backups

json ReadJson(const std::string& name, const json& def);
void WriteJson(const std::string& name, const json& obj, int indent = 1);

json DefaultSettings();
json Settings();                      // defaults merged with settings.json
void SaveSettings(const json& patch);  // merge a patch into settings.json

std::filesystem::path CookedDir();         // settings.target.cookedDir if it exists, else empty
std::filesystem::path GaragePatchDir();    // generated package cache or legacy capture directory
std::filesystem::path GarageRecipeDir();   // cache for the shipped Epic recipe
std::filesystem::path GarageBackupDir();   // backups\garage (pre-patch vanilla backups)
std::filesystem::path PacksDir();          // %APPDATA%\RippleRL\packs
std::filesystem::path WorkshopDir();       // %APPDATA%\RippleRL\workshop
std::string IsoNow();                      // "YYYY-MM-DDTHH:MM:SSZ" (UTC)

}  // namespace store
