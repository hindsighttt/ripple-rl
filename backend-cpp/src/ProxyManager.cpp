#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "ProxyManager.h"

#include "Store.h"
#include "WinUtil.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A loopback port is "open" if we can connect to it within ~300ms.
bool PortOpen(unsigned short port) {
    win::EnsureWinsock();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    connect(s, (sockaddr*)&a, sizeof(a));
    fd_set wf;
    FD_ZERO(&wf);
    FD_SET(s, &wf);
    timeval tv{0, 300000};
    bool ok = false;
    if (select(0, nullptr, &wf, nullptr, &tv) > 0 && FD_ISSET(s, &wf)) {
        int err = 0, len = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
        ok = (err == 0);
    }
    closesocket(s);
    return ok;
}

fs::path WebcacheDir() {
    std::wstring up = win::GetEnvW(L"USERPROFILE");
    return fs::path(up) / "Documents" / "My Games" / "Rocket League" / "TAGame" / "Cache" / "WebCache";
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

}  // namespace

fs::path ProxyManager::Conf() const { return store::StateDir() / "mitm"; }

bool ProxyManager::Running() const {
    if (!proc_) return false;
    DWORD code = 0;
    if (GetExitCodeProcess((HANDLE)proc_, &code)) return code == STILL_ACTIVE;
    return false;
}

std::vector<std::wstring> ProxyManager::MitmdumpCommand() const {
    std::wstring env = win::GetEnvW(L"RIPPLE_RL_MITMDUMP");
    if (!env.empty()) return {env};
    std::error_code ec;
    fs::path dir = win::ExeDir();
    // Preferred bundled layout: a portable embedded Python + a launcher script.
    fs::path py = dir / "runtime" / "python.exe";
    fs::path launch = dir / "runtime" / "mitmdump-launch.py";
    if (fs::exists(py, ec) && fs::exists(launch, ec)) return {py.wstring(), launch.wstring()};
    // Otherwise a mitmdump.exe: bundled, then common dev installs.
    std::vector<fs::path> cands = {
        dir / "runtime" / "Scripts" / "mitmdump.exe",
        fs::path("C:/Python312/Scripts/mitmdump.exe"),
    };
    std::wstring local = win::GetEnvW(L"LOCALAPPDATA");
    if (!local.empty())
        cands.push_back(fs::path(local) / "Programs" / "Python" / "Python312" / "Scripts" / "mitmdump.exe");
    for (auto& c : cands)
        if (fs::exists(c, ec)) return {c.wstring()};
    return {L"mitmdump.exe"};  // last resort: resolve via PATH
}

void ProxyManager::Start() {
    if (Running()) return;
    for (unsigned short p : {PORT, FORWARD_PORT})
        if (PortOpen(p))
            throw std::runtime_error("Port " + std::to_string(p) +
                                     " is occupied. Close the other Ripple RL instance first.");

    std::error_code ec;
    fs::create_directories(Conf(), ec);

    // The mitmdump child inherits our environment; set what proxy_addon.py needs.
    std::wstring ppath = store::AppDir().wstring();
    std::wstring existing = win::GetEnvW(L"PYTHONPATH");
    if (!existing.empty()) ppath += L";" + existing;
    SetEnvironmentVariableW(L"PYTHONPATH", ppath.c_str());
    SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");

    std::vector<std::wstring> cmd = MitmdumpCommand();
    fs::path addon = store::AppDir() / "ripple" / "proxy_addon.py";

    std::wstring cl;
    for (auto& part : cmd) {
        if (!cl.empty()) cl += L" ";
        cl += L"\"" + part + L"\"";
    }
    cl += L" --set flow_detail=0 --set termlog_verbosity=info --listen-host 127.0.0.1";
    cl += L" --mode reverse:http://api.rlpp.psynet.gg@" + std::to_wstring(PORT);
    cl += L" --mode regular@" + std::to_wstring(FORWARD_PORT);
    cl += L" --set http3=false --set connection_strategy=lazy";
    cl += L" --set confdir=" + Conf().wstring();
    cl += L" -s \"" + addon.wstring() + L"\"";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hOut = CreateFileW((store::StateDir() / "mitm_stdout.log").wstring().c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE hErr = CreateFileW((store::StateDir() / "mitm.log").wstring().c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = (hOut != INVALID_HANDLE_VALUE) ? hOut : nullptr;
    si.hStdError = (hErr != INVALID_HANDLE_VALUE) ? hErr : nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring mutableCl = cl;
    std::wstring cwd = store::AppDir().wstring();
    BOOL ok = CreateProcessW(nullptr, mutableCl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             cwd.c_str(), &si, &pi);
    if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
    if (hErr != INVALID_HANDLE_VALUE) CloseHandle(hErr);
    if (!ok)
        throw std::runtime_error("Could not launch mitmdump (" + win::Narrow(cmd[0]) +
                                 "). Is the bundled Python runtime present?");
    CloseHandle(pi.hThread);
    proc_ = pi.hProcess;

    bool up = false;
    for (int i = 0; i < 120; ++i) {
        Sleep(100);
        if (PortOpen(PORT) && PortOpen(FORWARD_PORT)) {
            up = true;
            break;
        }
        DWORD code = 0;
        if (GetExitCodeProcess(pi.hProcess, &code) && code != STILL_ACTIVE) break;
    }
    if (!Running() || !up) {
        if (Running()) TerminateProcess(pi.hProcess, 1);
        throw std::runtime_error("Proxy failed to listen on port " + std::to_string(PORT) + ". See mitm.log.");
    }

    InstallCa();
    PatchWebcache(PORT);
    RegProxy(true, std::string("127.0.0.1:") + std::to_string(FORWARD_PORT));
}

void ProxyManager::Stop() {
    if (proc_) {
        if (Running()) {
            TerminateProcess((HANDLE)proc_, 0);
            WaitForSingleObject((HANDLE)proc_, 3000);
        }
        CloseHandle((HANDLE)proc_);
        proc_ = nullptr;
    }
    RegProxy(false, "");
    RestoreWebcache();
}

bool ProxyManager::CaInstalled() {
    std::error_code ec;
    if (!fs::exists(Conf() / "mitmproxy-ca-cert.cer", ec)) return false;
    std::string out;
    win::RunCapture(L"certutil -store Root", out);
    return out.find("mitmproxy") != std::string::npos;
}

void ProxyManager::InstallCa() {
    fs::path cer = Conf() / "mitmproxy-ca-cert.cer";
    std::error_code ec;
    if (!fs::exists(cer, ec)) return;
    if (CaInstalled()) return;
    win::RunWait(L"certutil -addstore -f Root \"" + cer.wstring() + L"\"", true);
    if (CaInstalled()) return;
    // Non-elevated add to the machine Root store fails without admin; fall back
    // to an elevated certutil (prompts UAC once). Matches proxy.py's behaviour.
    std::wstring ps = L"powershell -NoProfile -Command \"Start-Process certutil -Verb RunAs "
                      L"-ArgumentList '-addstore','-f','Root','\\\"" +
                      cer.wstring() + L"\\\"'\"";
    win::RunWait(ps, true);
}

void ProxyManager::RegProxy(bool enable, const std::string& server) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", 0,
                      KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    DWORD v = enable ? 1 : 0;
    RegSetValueExW(k, L"ProxyEnable", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    std::wstring ws = win::Widen(server);
    RegSetValueExW(k, L"ProxyServer", 0, REG_SZ, (const BYTE*)ws.c_str(),
                   (DWORD)((ws.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

void ProxyManager::PatchWebcache() { PatchWebcache(PORT); }

void ProxyManager::RefreshTitlesCache() {
    std::error_code ec;
    fs::path py = win::ExeDir() / "runtime" / "python.exe";
    if (!fs::exists(py, ec)) py = fs::path("python.exe");
    std::wstring old = win::GetEnvW(L"PYTHONPATH");
    std::wstring root = store::AppDir().wstring();
    std::wstring value = root + (old.empty() ? L"" : L";" + old);
    SetEnvironmentVariableW(L"PYTHONPATH", value.c_str());
    std::wstring cmd = L"\"" + py.wstring() + L"\" -c \"from ripple import titles_config; "
                       L"titles_config.refresh_webcache()\"";
    win::RunWait(cmd, true);
    SetEnvironmentVariableW(L"PYTHONPATH", old.empty() ? nullptr : old.c_str());
}

void ProxyManager::PatchWebcache(unsigned short port) {
    fs::path base = WebcacheDir();
    std::error_code ec;
    if (!fs::is_directory(base, ec)) return;
    std::string endpoint = std::string("127.0.0.1:") + std::to_string(port);
    fs::path backupDir = store::StateDir() / "webcache_backups";
    for (auto& e : fs::directory_iterator(base, ec)) {
        if (!e.is_regular_file()) continue;
        std::string data;
        {
            std::ifstream f(e.path(), std::ios::binary);
            if (!f) continue;
            std::stringstream ss;
            ss << f.rdbuf();
            data = ss.str();
        }
        std::string nb = data;
        ReplaceAll(nb, "127.0.0.1:49320", endpoint);
        ReplaceAll(nb, "127.0.0.1:49321", endpoint);
        if (nb == data) continue;
        fs::create_directories(backupDir, ec);
        fs::path backup = backupDir / e.path().filename();
        if (!fs::exists(backup, ec)) fs::copy_file(e.path(), backup, ec);
        std::ofstream o(e.path(), std::ios::binary | std::ios::trunc);
        o << nb;
    }
}

void ProxyManager::RestoreWebcache() {
    fs::path backupDir = store::StateDir() / "webcache_backups";
    std::error_code ec;
    if (!fs::is_directory(backupDir, ec)) return;
    fs::path base = WebcacheDir();
    for (auto& e : fs::directory_iterator(backupDir, ec)) {
        if (!e.is_regular_file()) continue;
        fs::copy_file(e.path(), base / e.path().filename(), fs::copy_options::overwrite_existing, ec);
    }
    // Title registration creates <cache>.ripplebak copies of the pre-injection
    // files. Put those back last so the cache ends pristine as well.
    for (auto& e : fs::directory_iterator(base, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string name = e.path().filename().string();
        const std::string suffix = ".ripplebak";
        if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix))
            continue;
        fs::copy_file(e.path(), base / name.substr(0, name.size() - suffix.size()),
                      fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::remove(e.path(), ec);
    }
}
