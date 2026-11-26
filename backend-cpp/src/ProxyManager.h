#pragma once
// Native port of ripple/proxy.py: launches the bundled Python mitmproxy
// (mitmdump.exe) as a child process, installs the mitmproxy root CA into the
// machine store, retargets Rocket League's WebCache, and toggles the Windows
// system proxy. This is the piece that stays Python under the hood per the
// hybrid plan (there is no drop-in C++ equivalent of mitmproxy); the native
// backend just owns its lifecycle.
#include <filesystem>
#include <string>
#include <vector>

class ProxyManager {
public:
    static constexpr unsigned short PORT = 49320;          // reverse listener RL talks to
    static constexpr unsigned short FORWARD_PORT = 49321;  // regular proxy for the system

    bool Running() const;
    void Start();  // throws std::runtime_error on failure
    void Stop();
    bool CaInstalled();
    void PatchWebcache();  // retarget RL's WebCache to the proxy (public wrapper)
    void RefreshTitlesCache();  // register custom titles in the cached config

private:
    std::filesystem::path Conf() const;              // %APPDATA%\RippleRL\mitm
    std::vector<std::wstring> MitmdumpCommand() const;  // leading argv for mitmdump (bundled or dev)
    void InstallCa();
    void PatchWebcache(unsigned short port);
    void RestoreWebcache();
    void RegProxy(bool enable, const std::string& server);

    void* proc_ = nullptr;  // HANDLE to the mitmdump process
};
