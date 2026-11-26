#include <windows.h>

#include "Quickchat.h"

#include "WinUtil.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
constexpr long long QC_KB = 1048576;  // SPA keycode flag
constexpr UINT QC_RELOAD = WM_APP + 7;

std::mutex g_sendMtx;  // one chat typed at a time (quickchat.py _send_lock)

double NowSec() { return (double)GetTickCount64() / 1000.0; }

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

const std::unordered_map<std::string, int>& Mods() {
    static const std::unordered_map<std::string, int> m = {{"alt", 1}, {"ctrl", 2}, {"shift", 4}, {"win", 8}};
    return m;
}
const std::unordered_map<std::string, int>& VkNames() {
    static std::unordered_map<std::string, int> m = [] {
        std::unordered_map<std::string, int> v = {{"tab", 0x09}, {"space", 0x20}, {"enter", 0x0D}, {"f1", 0x70}};
        for (int i = 1; i <= 24; ++i) v["f" + std::to_string(i)] = 0x6F + i;
        for (char c = 'A'; c <= 'Z'; ++c) v[std::string(1, (char)std::tolower(c))] = c;
        for (char c = '0'; c <= '9'; ++c) v[std::string(1, c)] = c;
        return v;
    }();
    return m;
}

// quickchat.py parse(combo) -> (mods, vk)
std::pair<int, int> Parse(const std::string& combo) {
    if (combo.find('@') != std::string::npos) {
        std::string tail = combo.substr(combo.rfind('@') + 1);
        size_t colon = tail.find(':');
        if (colon != std::string::npos) {
            try {
                int key = std::stoi(tail.substr(0, colon));
                int mods = std::stoi(tail.substr(colon + 1));
                if (mods >= 0 && mods <= 15 && key >= 1 && key <= 255) return {mods, key};
            } catch (...) {
            }
        }
        return {0, 0};
    }
    int mods = 0, key = 0;
    size_t start = 0;
    while (start <= combo.size()) {
        size_t plus = combo.find('+', start);
        std::string part = Lower(Trim(combo.substr(start, plus == std::string::npos ? std::string::npos : plus - start)));
        auto mi = Mods().find(part);
        if (mi != Mods().end())
            mods |= mi->second;
        else if (!part.empty()) {
            auto vi = VkNames().find(part);
            if (vi != VkNames().end()) key = vi->second;
        }
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    return {mods, key};
}

HWND GameWindow() {
    HWND w = GetForegroundWindow();
    if (!w) return nullptr;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return nullptr;
    HWND res = nullptr;
    wchar_t path[1024];
    DWORD sz = 1024;
    if (QueryFullProcessImageNameW(proc, 0, path, &sz)) {
        std::wstring p(path, sz);
        size_t slash = p.find_last_of(L"\\/");
        std::wstring base = (slash == std::wstring::npos) ? p : p.substr(slash + 1);
        for (auto& c : base) c = (wchar_t)towlower(c);
        if (base == L"rocketleague.exe") res = w;
    }
    CloseHandle(proc);
    return res;
}

void SendKey(WORD vk, WORD scan, DWORD flags) {
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[0].ki.wScan = scan;
    in[0].ki.dwFlags = flags;
    in[1].ki.wVk = vk;
    in[1].ki.wScan = scan;
    in[1].ki.dwFlags = flags | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

template <class Focused>
bool TypeUnicode(const std::string& text, Focused focused) {
    std::wstring w = win::Widen(text);
    for (wchar_t ch : w) {
        if (!focused()) return false;
        SendKey(0, (WORD)ch, KEYEVENTF_UNICODE);
        Sleep(4);
    }
    return true;
}

// quickchat.py _send_chat
void SendChat(bool enabled, std::array<int, 3> keys, std::string text, int target, int triggerVk) {
    HWND window = GameWindow();
    if (!enabled || window == nullptr) return;
    if (!g_sendMtx.try_lock()) return;
    struct Unlock {
        ~Unlock() { g_sendMtx.unlock(); }
    } unlock;

    double deadline = NowSec() + 0.5;
    std::vector<int> release = {0x10, 0x11, 0x12};
    if (triggerVk) release.push_back(triggerVk);
    for (;;) {
        bool anyDown = false;
        for (int vk : release)
            if (GetAsyncKeyState(vk) & 0x8000) anyDown = true;
        if (!anyDown) break;
        if (NowSec() >= deadline) return;
        Sleep(10);
    }

    int t = (target >= 0 && target <= 2) ? target : 0;
    static const int kDefault[3] = {'T', 'Y', 'U'};
    int vk = keys[t] ? keys[t] : kDefault[t];
    auto focused = [window]() { return GetForegroundWindow() == window; };
    if (!focused()) return;
    SendKey((WORD)vk, 0, 0);
    Sleep(80);
    if (TypeUnicode(text, focused) && focused()) SendKey(0x0D, 0, 0);
}

struct Bind {
    std::string text;
    int mods, vk, target;
};
struct Seq {
    std::vector<int> vks;
    std::string text;
    int target;
};
}  // namespace

namespace quickchat {

std::string KeyLabel(long long code) {
    long long vk = (code & QC_KB) ? (code & 0xFFFF) : code;
    if (!(vk >= 1 && vk <= 255)) return "";
    UINT scan = MapVirtualKeyW((UINT)vk, 0);
    static const int ext[] = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E, 0x6F};
    bool extended = false;
    for (int e : ext)
        if (vk == e) extended = true;
    LONG value = (LONG)(scan << 16) | ((extended ? 1 : 0) << 24);
    wchar_t label[128] = {};
    GetKeyNameTextW(value, label, 128);
    return win::Narrow(label);
}

struct Engine::Impl {
    std::thread pump, watcher;
    DWORD tid = 0;
    std::atomic<bool> ready{false}, stop{false};

    std::mutex mtx;
    std::atomic<bool> enabled{false};
    std::vector<Bind> binds;
    std::vector<Seq> seqs;
    std::array<int, 3> keys{0, 0, 0};

    // pump-thread-owned hotkey registry
    std::unordered_map<int, std::pair<std::string, int>> hotById;  // id -> (text, target) ; +vk below
    std::unordered_map<int, int> hotVk;                            // id -> trigger vk
    int nextId = 1;

    void Rebind() {
        for (auto& [id, _] : hotById) UnregisterHotKey(nullptr, id);
        hotById.clear();
        hotVk.clear();
        if (!enabled.load()) return;
        std::vector<Bind> b;
        {
            std::lock_guard<std::mutex> lk(mtx);
            b = binds;
        }
        for (auto& bind : b) {
            if (!bind.vk) continue;
            int id = nextId++;
            if (RegisterHotKey(nullptr, id, (UINT)bind.mods, (UINT)bind.vk)) {
                hotById[id] = {bind.text, bind.target};
                hotVk[id] = bind.vk;
            }
        }
    }

    void Pump() {
        tid = GetCurrentThreadId();
        MSG msg;
        PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // force the queue to exist
        ready = true;
        while (!stop && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == QC_RELOAD) {
                Rebind();
            } else if (msg.message == WM_HOTKEY) {
                int id = (int)msg.wParam;
                auto it = hotById.find(id);
                if (it != hotById.end()) {
                    bool en = enabled.load();
                    std::array<int, 3> k;
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        k = keys;
                    }
                    std::thread(SendChat, en, k, it->second.first, it->second.second, hotVk[id]).detach();
                }
            }
        }
        for (auto& [id, _] : hotById) UnregisterHotKey(nullptr, id);
    }

    void Watch() {
        std::vector<int> history;
        std::vector<int> down;
        double lastPress = 0.0;
        while (!stop) {
            Sleep(10);
            std::vector<Seq> seqs_;
            bool en;
            std::array<int, 3> k;
            {
                std::lock_guard<std::mutex> lk(mtx);
                seqs_ = seqs;
                k = keys;
            }
            en = enabled.load();
            if (!en || seqs_.empty()) {
                history.clear();
                continue;
            }
            std::vector<int> keyset;
            size_t longest = 0;
            for (auto& s : seqs_) {
                longest = (std::max)(longest, s.vks.size());
                for (int vk : s.vks) keyset.push_back(vk);
            }
            std::vector<int> pressed;
            for (int vk : keyset)
                if (GetAsyncKeyState(vk) & 0x8000) pressed.push_back(vk);
            // edges = pressed - down
            std::vector<int> edges;
            for (int vk : pressed)
                if (std::find(down.begin(), down.end(), vk) == down.end()) edges.push_back(vk);
            down = pressed;

            if (g_sendMtx.try_lock()) {
                g_sendMtx.unlock();
            } else {
                history.clear();
                continue;
            }
            if (GameWindow() == nullptr) {
                history.clear();
                continue;
            }
            if (edges.empty()) continue;
            double now = NowSec();
            if (now - lastPress > 1.0) history.clear();
            lastPress = now;
            std::sort(edges.begin(), edges.end());
            for (int vk : edges) {
                history.push_back(vk);
                if (history.size() > longest) history.erase(history.begin(), history.end() - longest);
                for (auto& s : seqs_) {
                    if (history.size() >= s.vks.size() &&
                        std::equal(s.vks.begin(), s.vks.end(), history.end() - s.vks.size())) {
                        std::thread(SendChat, true, k, s.text, s.target, 0).detach();
                        history.clear();
                        break;
                    }
                }
            }
        }
    }
};

Engine::Engine() : p_(new Impl) {
    p_->pump = std::thread([this] { p_->Pump(); });
    for (int i = 0; i < 2000 && !p_->ready.load(); ++i) Sleep(1);
    p_->watcher = std::thread([this] { p_->Watch(); });
}

Engine::~Engine() {
    p_->stop = true;
    if (p_->tid) PostThreadMessageW(p_->tid, WM_NULL, 0, 0);  // wake the pump
    if (p_->pump.joinable()) p_->pump.join();
    if (p_->watcher.joinable()) p_->watcher.join();
    delete p_;
}

void Engine::Reload(const json& qc) {
    bool enabled = qc.value("enabled", true);
    std::vector<Bind> binds;
    std::vector<Seq> seqs;
    for (auto& e : qc.value("entries", json::array())) {
        std::string text = Trim(e.value("text", std::string("")));
        if (text.empty()) continue;
        int target = e.value("target", 0);
        auto [mods, vk] = Parse(e.value("key", std::string("")));
        if (vk) binds.push_back({text, mods, vk, target});
        if (e.contains("seq") && e["seq"].is_array()) {
            std::vector<int> codes;
            bool ok = true;
            for (auto& c : e["seq"]) {
                if (!c.is_number()) {
                    ok = false;
                    break;
                }
                long long v = c.get<long long>();
                if (!(v > QC_KB && v < QC_KB + 256)) {
                    ok = false;
                    break;
                }
                codes.push_back((int)(v & 0xFFFF));
            }
            if (ok && !codes.empty()) seqs.push_back({codes, text, target});
        }
    }
    std::array<int, 3> keys{0, 0, 0};
    json ks = qc.value("keys", json::array());
    for (int i = 0; i < 3 && i < (int)ks.size(); ++i)
        if (ks[i].is_object()) keys[i] = ks[i].value("vk", 0);

    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->binds = std::move(binds);
        p_->seqs = std::move(seqs);
        p_->keys = keys;
    }
    p_->enabled = enabled;
    if (p_->tid) PostThreadMessageW(p_->tid, QC_RELOAD, 0, 0);
}

}  // namespace quickchat
