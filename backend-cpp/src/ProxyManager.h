#pragma once
// Native port of ripple/proxy.py: launches the bundled Python mitmproxy
// (mitmdump.exe) as a child process, installs the mitmproxy root CA into the
// machine store, retargets Rocket League's WebCache, and toggles the Windows
// system proxy. This is the piece that stays Python under the hood per the
// hybrid plan (there is no drop-in C++ equivalent of mitmproxy); the native
// backend just owns its lifecycle.
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

class ProxyManager {
public:
    static constexpr unsigned short PORT = 49320;          // reverse listener RL talks to
    static constexpr unsigned short FORWARD_PORT = 49321;  // regular proxy for the system

    bool Running() const;
    void Start();  // throws std::runtime_error on failure
    void Poll();   // called by the backend worker
    void Stop();
    bool CaInstalled();
    void PatchWebcache();  // retarget RL's WebCache to the proxy (public wrapper)
    void RestoreDirectRouting();  // restore any saved proxy routing before a direct launch
    void RefreshTitlesCache();  // register custom titles in the cached config

    // Heal a system proxy that a previous run left pointing at a now-dead
    // mitmdump (e.g. Ripple or the game crashed before Stop() ran). Call this at
    // startup before anything else: without it the leftover HKCU proxy setting
    // survives reboots and takes the whole machine offline. No-op when clean.
    void RecoverLeakedProxy();

private:
    std::filesystem::path Conf() const;              // %APPDATA%\RippleRL\mitm
    std::filesystem::path Marker() const;            // flag file: "the system proxy is ours and on"
    std::vector<std::wstring> MitmdumpCommand() const;  // leading argv for mitmdump (bundled or dev)
    void InstallCa();
    void EnsureConfigCert();
    void PatchWebcache(unsigned short port);
    void RestoreWebcache();
    void EnableWindowsProxy();
    void RestoreWindowsProxy();
    void StartLocked();
    void StartConfig();
    void* Launch(const std::wstring& arguments, bool config);
    void EnsureJob();          // create the kill-on-close job that owns mitmdump
    bool AcquireOwnership();

    void* proc_ = nullptr;  // HANDLE to the mitmdump process
    void* proc_config_ = nullptr;  // HANDLE to the config listener process
    void* owner_ = nullptr;  // exclusive lock for Windows proxy settings
    void* job_ = nullptr;  // HANDLE to the job object; kills children if we die
    unsigned long long retryMainAt_ = 0;
    unsigned long long retryConfigAt_ = 0;
    unsigned mainBackoffMs_ = 1000;
    unsigned configBackoffMs_ = 1000;
    unsigned mainProbeFailures_ = 0;
    bool caReady_ = false;
    bool caInstallAttempted_ = false;
    std::mutex lifecycle_;
};
