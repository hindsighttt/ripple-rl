#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wininet.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <shlobj.h>

#include "ProxyManager.h"

#include "Bootstrap.h"
#include "Home.h"
#include "Store.h"
#include "WinUtil.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A loopback port is "open" if we can connect to it within ~300ms.
bool PortOpen(unsigned short port, bool ipv6 = false) {
    win::EnsureWinsock();
    SOCKET s = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    sockaddr_in6 a6{};
    a6.sin6_family = AF_INET6;
    a6.sin6_port = htons(port);
    a6.sin6_addr = in6addr_loopback;
    const int result = ipv6 ? connect(s, reinterpret_cast<sockaddr*>(&a6), sizeof(a6))
                            : connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    fd_set wf;
    FD_ZERO(&wf);
    FD_SET(s, &wf);
    timeval tv{0, 300000};
    bool ok = result == 0;
    if (!ok && select(0, nullptr, &wf, nullptr, &tv) > 0 && FD_ISSET(s, &wf)) {
        int err = 0, len = sizeof(err);
        ok = getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len) == 0 && err == 0;
    }
    closesocket(s);
    return ok;
}

bool Alive(void* process) {
    return process && WaitForSingleObject(static_cast<HANDLE>(process), 0) == WAIT_TIMEOUT;
}

void CloseChild(void*& process) {
    if (!process) return;
    HANDLE handle = static_cast<HANDLE>(process);
    if (Alive(process)) {
        TerminateProcess(handle, 0);
        WaitForSingleObject(handle, 2000);
    }
    CloseHandle(handle);
    process = nullptr;
}

void LogProxy(const std::string& message) {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char stamp[32]{};
    std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::ofstream(store::StateDir() / "proxy_lifecycle.log", std::ios::app) << stamp << ' ' << message << '\n';
}

std::string ChildStatus(void* process) {
    DWORD code = 0;
    if (!process) return "no process";
    if (Alive(process)) return "listener unhealthy";
    if (GetExitCodeProcess(static_cast<HANDLE>(process), &code))
        return "process exit " + std::to_string(code);
    return "could not read process exit code";
}

bool AddonReady(bool forward = false) {
    const std::wstring proxy = L"127.0.0.1:" + std::to_wstring(ProxyManager::FORWARD_PORT);
    HINTERNET internet = InternetOpenW(L"Ripple proxy health",
        forward ? INTERNET_OPEN_TYPE_PROXY : INTERNET_OPEN_TYPE_DIRECT,
        forward ? proxy.c_str() : nullptr, nullptr, 0);
    if (!internet) return false;
    DWORD timeout = 600;
    InternetSetOptionW(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(internet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    const std::wstring url = forward ? L"http://ripple-health.invalid/__ripple_health" :
        L"http://127.0.0.1:" + std::to_wstring(ProxyManager::PORT) + L"/__ripple_health";
    HINTERNET request = InternetOpenUrlW(internet, url.c_str(), nullptr, 0,
        INTERNET_FLAG_NO_AUTO_REDIRECT | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD, 0);
    DWORD status = 0, size = sizeof(status);
    wchar_t identity[32] = L"X-Ripple-Health";
    DWORD identitySize = sizeof(identity);
    bool ready = request &&
        HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr) &&
        status == 204 && HttpQueryInfoW(request, HTTP_QUERY_CUSTOM, identity, &identitySize, nullptr) &&
        std::wstring(identity) == L"ready";
    if (request) InternetCloseHandle(request);
    InternetCloseHandle(internet);
    return ready;
}

bool MainListenersReady() {
    return PortOpen(ProxyManager::PORT) && PortOpen(ProxyManager::FORWARD_PORT) &&
           AddonReady() && AddonReady(true);
}

fs::path WebcacheDir() {
    wchar_t documents[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents))) return {};
    return fs::path(documents) / "My Games" / "Rocket League" / "TAGame" / "Cache" / "WebCache";
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

fs::path ProxyManager::Marker() const { return store::StateDir() / "proxy.active"; }

void ProxyManager::EnsureJob() {
    if (job_) return;
    HANDLE j = CreateJobObjectW(nullptr, nullptr);
    if (!j) throw std::runtime_error("Could not create the Ripple proxy process job.");
    // KILL_ON_JOB_CLOSE ties the children's lifetime to our handle: if Ripple
    // (or the whole process) dies without running Stop(), the handle closes and
    // Windows terminates mitmdump too, instead of leaving it orphaned and still
    // intercepting all system traffic.
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof(li))) {
        CloseHandle(j);
        throw std::runtime_error("Could not configure the Ripple proxy process job.");
    }
    job_ = j;
}

bool ProxyManager::AcquireOwnership() {
    if (owner_) return true;
    HANDLE lock = CreateFileW((store::StateDir() / "proxy.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    owner_ = lock;
    return true;
}

void ProxyManager::RecoverLeakedProxy() {
    if (!AcquireOwnership()) return;
    try {
        if (!PortOpen(FORWARD_PORT)) RestoreWindowsProxy();
    } catch (const std::exception& e) {
        LogProxy(std::string("Windows proxy recovery failed: ") + e.what());
    }
    CloseHandle(static_cast<HANDLE>(owner_));
    owner_ = nullptr;
}

bool ProxyManager::Running() const {
    return Alive(proc_);
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

void* ProxyManager::Launch(const std::wstring& arguments, bool config) {
    const auto cmd = MitmdumpCommand();
    const auto addon = store::AppDir() / "ripple" / "proxy_addon.py";
    if (!fs::is_regular_file(addon))
        throw std::runtime_error("The Ripple proxy addon is missing.");
    std::wstring command;
    for (const auto& part : cmd)
        command += L"\"" + part + L"\" ";
    command += L"--set flow_detail=0 --set termlog_verbosity=info --set http3=false";
    command += L" --set connection_strategy=lazy --set \"confdir=" + Conf().wstring() + L"\"";
    command += L" -s \"" + addon.wstring() + L"\" " + arguments;

    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    const auto log = store::StateDir() / (config ? "mitm_config.log" : "mitm_stdout.log");
    HANDLE output = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &inherit, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open the proxy log.");
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        CloseHandle(output);
        throw std::runtime_error("Could not open the proxy standard input.");
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION child{};
    BOOL launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                   store::AppDir().c_str(), &startup, &child);
    const DWORD launchError = GetLastError();
    CloseHandle(input);
    CloseHandle(output);
    if (!launched)
        throw std::runtime_error("Could not launch mitmdump (Windows error " + std::to_string(launchError) + ").");
    if (!AssignProcessToJobObject(static_cast<HANDLE>(job_), child.hProcess) ||
        ResumeThread(child.hThread) == static_cast<DWORD>(-1)) {
        CloseHandle(child.hThread);
        void* process = child.hProcess;
        CloseChild(process);
        throw std::runtime_error("Could not start mitmdump under Ripple's process job.");
    }
    CloseHandle(child.hThread);
    LogProxy(std::string(config ? "config" : "main") + " started pid=" + std::to_string(child.dwProcessId));
    return child.hProcess;
}

void ProxyManager::Start() {
    std::lock_guard lock(lifecycle_);
    try {
        StartLocked();
    } catch (const std::exception& e) {
        LogProxy(std::string("main startup failed: ") + e.what());
        throw;
    }
}

void ProxyManager::StartLocked() {
    if (!AcquireOwnership()) throw std::runtime_error("Another Ripple instance owns the proxy.");
    EnsureJob();
    if (!Running() || !MainListenersReady()) {
        RestoreWindowsProxy();
        CloseChild(proc_);
        for (unsigned short port : {PORT, FORWARD_PORT})
            if (PortOpen(port))
                throw std::runtime_error("Proxy port " + std::to_string(port) + " is occupied.");
        fs::create_directories(Conf());
        proc_ = Launch(L"--listen-host 127.0.0.1 --mode reverse:http://api.rlpp.psynet.gg@" +
                       std::to_wstring(PORT) + L" --mode regular@" + std::to_wstring(FORWARD_PORT), false);
        const auto deadline = GetTickCount64() + 12000;
        bool ready = false;
        while (Running() && GetTickCount64() < deadline) {
            if (MainListenersReady()) {
                ready = Running();
                break;
            }
            Sleep(100);
        }
        if (!ready) {
            auto reason = ChildStatus(proc_);
            CloseChild(proc_);
            throw std::runtime_error("Main proxy did not become ready (" + reason + "). See mitm_stdout.log.");
        }
        LogProxy("main reverse and forward listeners ready");
    }
    try {
        if (!caReady_ || !CaInstalled()) {
            InstallCa();
            caReady_ = true;
        }
        EnableWindowsProxy();
    } catch (...) {
        try {
            RestoreWindowsProxy();
        } catch (const std::exception& e) {
            LogProxy(e.what());
        }
        CloseChild(proc_);
        throw;
    }

    mainProbeFailures_ = 0;
    try {
        StartConfig();
    } catch (const std::exception& e) {
        LogProxy(std::string("config unavailable: ") + e.what());
        retryConfigAt_ = GetTickCount64() + configBackoffMs_;
    }
}

void ProxyManager::StartConfig() {
    if (Alive(proc_config_) && PortOpen(443, true)) return;
    CloseChild(proc_config_);
    if (PortOpen(443, true)) throw std::runtime_error("IPv6 loopback port 443 is occupied.");
    EnsureConfigCert();
    const auto cert = (Conf() / "cfgcert-combined.pem").wstring();
    proc_config_ = Launch(L"--listen-host [::1] --mode reverse:https://config.psynet.gg@443"
                          L" --certs config.localhost=\"" + cert + L"\""
                          L" --certs config.psynet.gg=\"" + cert + L"\"", true);
    const auto deadline = GetTickCount64() + 8000;
    while (Alive(proc_config_) && GetTickCount64() < deadline) {
        if (PortOpen(443, true) && Alive(proc_config_)) {
            LogProxy("config IPv6 listener ready");
            return;
        }
        Sleep(100);
    }
    auto reason = ChildStatus(proc_config_);
    CloseChild(proc_config_);
    throw std::runtime_error("Config proxy did not become ready (" + reason + "). See mitm_config.log.");
}

void ProxyManager::Poll() {
    std::lock_guard lock(lifecycle_);
    const auto now = GetTickCount64();
    if (proc_) {
        const bool alive = Running();
        const bool healthy = alive && MainListenersReady();
        mainProbeFailures_ = healthy ? 0 : mainProbeFailures_ + 1;
        if (!alive || mainProbeFailures_ >= 3) {
            LogProxy("main unavailable: " + ChildStatus(proc_));
            try {
                RestoreWindowsProxy();
            } catch (const std::exception& e) {
                LogProxy(e.what());
            }
            CloseChild(proc_);
            retryMainAt_ = now + mainBackoffMs_;
            mainBackoffMs_ = (std::min)(mainBackoffMs_ * 2, 30000u);
        } else if (healthy && now >= retryMainAt_ + 60000) {
            mainBackoffMs_ = 1000;
        }
    }
    if (!proc_ && now >= retryMainAt_) {
        try {
            StartLocked();
            retryMainAt_ = GetTickCount64();
        } catch (const std::exception& e) {
            LogProxy(std::string("main restart failed: ") + e.what());
            retryMainAt_ = GetTickCount64() + mainBackoffMs_;
            mainBackoffMs_ = (std::min)(mainBackoffMs_ * 2, 30000u);
        }
    }
    if (!Running()) return;
    if (proc_config_ && (!Alive(proc_config_) || !PortOpen(443, true))) {
        LogProxy("config unavailable: " + ChildStatus(proc_config_));
        CloseChild(proc_config_);
        retryConfigAt_ = now + configBackoffMs_;
    }
    if (!proc_config_ && now >= retryConfigAt_) {
        try {
            StartConfig();
            configBackoffMs_ = 1000;
        } catch (const std::exception& e) {
            LogProxy(std::string("config restart failed: ") + e.what());
            retryConfigAt_ = GetTickCount64() + configBackoffMs_;
            configBackoffMs_ = (std::min)(configBackoffMs_ * 2, 30000u);
        }
    }
}

void ProxyManager::Stop() {
    std::lock_guard lock(lifecycle_);
    try {
        if (owner_) RestoreWindowsProxy();
    } catch (const std::exception& e) {
        LogProxy(e.what());
    }
    if (proc_ || proc_config_) LogProxy("Ripple requested proxy shutdown");
    CloseChild(proc_config_);
    CloseChild(proc_);
    if (job_) {
        CloseHandle(static_cast<HANDLE>(job_));
        job_ = nullptr;
    }
    if (owner_) {
        try {
            if (!home::RlRunning()) {
                RestoreWebcache();
                bootstrap::RestoreActive();
            }
        } catch (const std::exception& e) {
            LogProxy(std::string("Game routing cleanup failed: ") + e.what());
        }
        CloseHandle(static_cast<HANDLE>(owner_));
        owner_ = nullptr;
    }
}

bool ProxyManager::CaInstalled() {
    const auto path = Conf() / "mitmproxy-ca-cert.cer";
    const void* certificate = nullptr;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(), CERT_QUERY_CONTENT_FLAG_CERT,
                          CERT_QUERY_FORMAT_FLAG_ALL, 0, nullptr, nullptr, nullptr,
                          nullptr, nullptr, &certificate)) return false;
    auto context = static_cast<PCCERT_CONTEXT>(certificate);
    bool trusted = false;
    for (DWORD location : {CERT_SYSTEM_STORE_CURRENT_USER, CERT_SYSTEM_STORE_LOCAL_MACHINE}) {
        auto roots = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                   location | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG, L"Root");
        if (!roots) continue;
        auto match = CertFindCertificateInStore(roots, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, context, nullptr);
        if (match) {
            trusted = match->cbCertEncoded == context->cbCertEncoded &&
                std::equal(match->pbCertEncoded, match->pbCertEncoded + match->cbCertEncoded, context->pbCertEncoded);
            CertFreeCertificateContext(match);
        }
        CertCloseStore(roots, 0);
        if (trusted) break;
    }
    CertFreeCertificateContext(context);
    return trusted;
}

void ProxyManager::InstallCa() {
    const auto certificate = Conf() / "mitmproxy-ca-cert.cer";
    if (!fs::exists(certificate)) throw std::runtime_error("mitmdump did not create its root certificate.");
    if (CaInstalled()) return;
    if (caInstallAttempted_) throw std::runtime_error("The mitmdump root certificate is not trusted.");
    caInstallAttempted_ = true;
    const std::wstring arguments = L"-addstore -f Root \"" + certificate.wstring() + L"\"";
    win::RunWait(L"certutil " + arguments, true, 10000);
    if (CaInstalled()) return;
    SHELLEXECUTEINFOW elevated{};
    elevated.cbSize = sizeof(elevated);
    elevated.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    elevated.lpVerb = L"runas";
    elevated.lpFile = L"certutil.exe";
    elevated.lpParameters = arguments.c_str();
    elevated.nShow = SW_HIDE;
    if (ShellExecuteExW(&elevated) && elevated.hProcess) {
        WaitForSingleObject(elevated.hProcess, 30000);
        CloseHandle(elevated.hProcess);
    }
    if (!CaInstalled()) throw std::runtime_error("The mitmdump root certificate is not trusted.");
}

namespace {

constexpr const wchar_t* ProxySettingNames[] = {L"ProxyEnable", L"ProxyServer", L"ProxyOverride"};

class InternetSettings {
public:
    InternetSettings() {
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
                          0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key_) != ERROR_SUCCESS)
            throw std::runtime_error("Could not open Windows proxy settings.");
    }
    ~InternetSettings() { RegCloseKey(key_); }

    json Read(const wchar_t* name) const {
        DWORD type = 0, size = 0;
        LSTATUS result = RegQueryValueExW(key_, name, nullptr, &type, nullptr, &size);
        if (result == ERROR_FILE_NOT_FOUND) return nullptr;
        if (result != ERROR_SUCCESS) throw std::runtime_error("Could not read Windows proxy settings.");
        std::vector<BYTE> bytes(size);
        if (RegQueryValueExW(key_, name, nullptr, &type, bytes.data(), &size) != ERROR_SUCCESS)
            throw std::runtime_error("Could not read Windows proxy settings.");
        bytes.resize(size);
        return json{{"type", type}, {"bytes", bytes}};
    }

    void Write(const wchar_t* name, const json& value) const {
        LSTATUS result;
        if (value.is_null()) {
            result = RegDeleteValueW(key_, name);
            if (result == ERROR_FILE_NOT_FOUND) return;
        } else {
            const auto bytes = value.at("bytes").get<std::vector<BYTE>>();
            result = RegSetValueExW(key_, name, 0, value.at("type").get<DWORD>(), bytes.data(),
                                    static_cast<DWORD>(bytes.size()));
        }
        if (result != ERROR_SUCCESS) throw std::runtime_error("Could not write Windows proxy settings.");
    }

private:
    HKEY key_ = nullptr;
};

json RegistryValue(DWORD type, const void* data, size_t size) {
    const auto* bytes = static_cast<const BYTE*>(data);
    return json{{"type", type}, {"bytes", std::vector<BYTE>(bytes, bytes + size)}};
}

json RippleProxySettings() {
    const DWORD enabled = 1;
    const std::wstring endpoint = L"127.0.0.1:" + std::to_wstring(ProxyManager::FORWARD_PORT);
    const wchar_t bypass[] = L"localhost;127.0.0.1;[::1];<local>";
    return json::array({RegistryValue(REG_DWORD, &enabled, sizeof(enabled)),
                        RegistryValue(REG_SZ, endpoint.c_str(), (endpoint.size() + 1) * sizeof(wchar_t)),
                        RegistryValue(REG_SZ, bypass, sizeof(bypass))});
}

void RefreshWindowsProxy() {
    InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}

void WriteAtomic(const fs::path& path, const std::string& bytes) {
    auto temp = path;
    temp += L".ripple-tmp";
    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), bytes.size());
    stream.close();
    if (!stream || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        fs::remove(temp, ec);
        throw std::runtime_error("Could not atomically write " + path.filename().string() + ".");
    }
}

}  // namespace

void ProxyManager::EnableWindowsProxy() {
    InternetSettings registry;
    const auto desired = RippleProxySettings();
    const bool saved = fs::exists(Marker());
    if (saved) {
        bool matches = true;
        for (size_t i = 0; i < desired.size(); ++i)
            matches = (registry.Read(ProxySettingNames[i]) == desired[i]) && matches;
        if (matches) return;
        RestoreWindowsProxy();
    }
    json previous = json::array();
    for (const auto* name : ProxySettingNames) previous.push_back(registry.Read(name));
    WriteAtomic(Marker(), previous.dump());
    // Write the endpoint first so rollback can identify partially applied settings.
    for (size_t i : {1u, 2u, 0u}) registry.Write(ProxySettingNames[i], desired[i]);
    RefreshWindowsProxy();
    LogProxy("Windows proxy enabled");
}

void ProxyManager::RestoreWindowsProxy() {
    if (!fs::exists(Marker())) return;
    const auto previous = store::ReadJson("proxy.active", nullptr);
    const auto desired = RippleProxySettings();
    InternetSettings registry;
    if (registry.Read(ProxySettingNames[1]) == desired[1]) {
        if (previous.is_array() && previous.size() == desired.size()) {
            // Keep individual settings that were changed by another application.
            for (size_t i : {0u, 2u, 1u})
                if (registry.Read(ProxySettingNames[i]) == desired[i])
                    registry.Write(ProxySettingNames[i], previous[i]);
        } else if (previous == 1) {
            const DWORD disabled = 0;
            registry.Write(ProxySettingNames[0], RegistryValue(REG_DWORD, &disabled, sizeof(disabled)));
            if (registry.Read(ProxySettingNames[2]) == desired[2])
                registry.Write(ProxySettingNames[2], nullptr);
            registry.Write(ProxySettingNames[1], nullptr);
        } else {
            throw std::runtime_error("Could not read the saved Windows proxy settings.");
        }
        RefreshWindowsProxy();
        LogProxy("Windows proxy restored");
    }
    fs::remove(Marker());
}

void ProxyManager::EnsureConfigCert() {
    auto python = win::ExeDir() / "runtime" / "python.exe";
    if (!fs::is_regular_file(python)) python = L"python.exe";
    const auto script = store::AppDir() / "ripple" / "proxy_cert.py";
    const auto command = L"\"" + python.wstring() + L"\" -I -B \"" +
                         script.wstring() + L"\" \"" + Conf().wstring() + L"\"";
    if (win::RunWait(command, true, 8000) != 0)
        throw std::runtime_error("Could not validate or generate the config listener certificate.");
}

void ProxyManager::PatchWebcache() {
    std::lock_guard lock(lifecycle_);
    if (owner_) PatchWebcache(PORT);
}

void ProxyManager::RestoreDirectRouting() {
    std::lock_guard lock(lifecycle_);
    const bool alreadyOwned = owner_ != nullptr;
    if (!AcquireOwnership()) throw std::runtime_error("Another Ripple instance owns the game proxy.");
    const auto release = [&] {
        if (!alreadyOwned) {
            CloseHandle(static_cast<HANDLE>(owner_));
            owner_ = nullptr;
        }
    };
    try {
        if (!Running() && !PortOpen(FORWARD_PORT)) RestoreWindowsProxy();
        RestoreWebcache();
        bootstrap::RestoreActive();
    } catch (...) {
        release();
        throw;
    }
    release();
}

void ProxyManager::RefreshTitlesCache() {
    std::lock_guard lock(lifecycle_);
    std::error_code ec;
    fs::path py = win::ExeDir() / "runtime" / "python.exe";
    if (!fs::exists(py, ec)) py = fs::path("python.exe");
    std::wstring old = win::GetEnvW(L"PYTHONPATH");
    std::wstring root = store::AppDir().wstring();
    std::wstring value = root + (old.empty() ? L"" : L";" + old);
    SetEnvironmentVariableW(L"PYTHONPATH", value.c_str());
    std::wstring cmd = L"\"" + py.wstring() + L"\" -c \"from ripple import titles_config; "
                       L"titles_config.refresh_webcache()\"";
    win::RunWait(cmd, true, 10000);
    SetEnvironmentVariableW(L"PYTHONPATH", old.empty() ? nullptr : old.c_str());
}

namespace {

std::string ReadCache(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not read " + path.filename().string() + ".");
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) throw std::runtime_error("Could not read " + path.filename().string() + ".");
    return contents.str();
}

std::string ProxyCache(std::string data, unsigned short port) {
    const std::string endpoint = "127.0.0.1:" + std::to_string(port);
    ReplaceAll(data, "127.0.0.1:49320", endpoint);
    ReplaceAll(data, "127.0.0.1:49321", endpoint);
    return data;
}

}  // namespace

void ProxyManager::PatchWebcache(unsigned short port) {
    const auto base = WebcacheDir();
    std::error_code ec;
    if (!fs::is_directory(base, ec)) return;
    const auto backups = store::StateDir() / "webcache_backups";
    for (const auto& entry : fs::directory_iterator(base, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() == ".ripplebak" ||
            entry.path().extension() == ".ripple-tmp") continue;
        try {
            const auto original = ReadCache(entry.path());
            const auto patched = ProxyCache(original, port);
            if (patched == original) continue;
            fs::create_directories(backups);
            WriteAtomic(backups / entry.path().filename(), original);
            if (ReadCache(entry.path()) == original) WriteAtomic(entry.path(), patched);
        } catch (const std::exception& e) {
            LogProxy(std::string("WebCache patch skipped: ") + e.what());
        }
    }
}

void ProxyManager::RestoreWebcache() {
    const auto backups = store::StateDir() / "webcache_backups";
    std::error_code ec;
    if (!fs::is_directory(backups, ec)) return;
    const auto base = WebcacheDir();
    if (base.empty()) return;
    for (const auto& entry : fs::directory_iterator(backups, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        try {
            const auto target = base / entry.path().filename();
            if (!fs::is_regular_file(target)) continue;
            const auto original = ReadCache(entry.path());
            const auto current = ReadCache(target);
            if (current != ProxyCache(original, PORT)) continue;
            if (current != original) WriteAtomic(target, original);
            fs::remove(entry.path());
        } catch (const std::exception& e) {
            LogProxy(std::string("WebCache restore skipped: ") + e.what());
        }
    }
}
