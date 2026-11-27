#include "WinUtil.h"

#include <winsock2.h>
#include <windows.h>

#include <random>

namespace win {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string o(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), o.data(), n, nullptr, nullptr);
    return o;
}

std::string RandomHex(size_t bytes) {
    std::random_device rd;
    static const char* h = "0123456789abcdef";
    std::string o;
    o.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i) {
        unsigned v = rd() & 0xFF;
        o.push_back(h[v >> 4]);
        o.push_back(h[v & 0xF]);
    }
    return o;
}

std::wstring GetEnvW(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return L"";
    std::wstring v(n, L'\0');
    DWORD m = GetEnvironmentVariableW(name, v.data(), n);
    v.resize(m);
    return v;
}

std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

void EnsureWinsock() {
    static bool done = false;
    if (done) return;
    WSADATA d;
    WSAStartup(MAKEWORD(2, 2), &d);
    done = true;
}

int RunWait(const std::wstring& cmdline, bool hidden, unsigned long timeoutMs) {
    std::wstring cl = cmdline;  // CreateProcessW may modify the buffer
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    DWORD flags = hidden ? CREATE_NO_WINDOW : 0;
    if (!CreateProcessW(nullptr, cl.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi))
        return -1;
    DWORD code = static_cast<DWORD>(-1);
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &code);
    } else {
        TerminateProcess(pi.hProcess, 1);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

int RunCapture(const std::wstring& cmdline, std::string& out) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cl = cmdline;
    BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return -1;
    }
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) out.append(buf, got);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

int RunPythonModule(const std::wstring& module, const std::filesystem::path& appDir,
                    std::initializer_list<std::filesystem::path> arguments, std::string& out) {
    const auto quote = [](const std::filesystem::path& path) {
        std::wstring value = path.wstring();
        if (!value.empty() && value.back() == L'\\') value += L'\\';
        return L"\"" + value + L"\"";
    };
    auto python = ExeDir() / "runtime" / "python.exe";
    if (!std::filesystem::is_regular_file(python)) python = L"python.exe";
    auto command = quote(python) + L" -I -B -c "
        L"\"import runpy,sys; sys.path.insert(0,sys.argv.pop(1)); "
        L"runpy.run_module(sys.argv.pop(1),run_name='__main__')\" " + quote(appDir) + L" " + module;
    for (const auto& path : arguments) command += L" " + quote(path);
    return RunCapture(command, out);
}

}  // namespace win
