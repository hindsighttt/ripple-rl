#pragma once
// Native port of bridge.py's home/install surface: Steam/Epic install detection,
// home.state, the launch orchestration (proxy + ProjectX bootstrap + start RL),
// and install-path settings.
#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

class ProxyManager;

namespace home {

std::string SourcePath(const std::string& source);  // "Epic" | "Steam" -> CookedPCConsole path
bool RlRunning();

json State();
json SetSourcePath(const json& p);
json DetectSource(const json& p);
json SetInstall(const json& p);
json SetPseudo(const json& p);
json ForgetPseudo(const json& p);
void OpenBackupFolder();

// Full launch: proxy.start + bootstrap.prepare + start Rocket League.
// (garage/flip UPK patches are not ported yet and are skipped here.)
json Launch(const json& p, ProxyManager& proxy);

}  // namespace home
