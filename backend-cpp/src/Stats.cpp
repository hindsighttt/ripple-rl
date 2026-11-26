#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "Stats.h"

#include "WinUtil.h"

#include <fstream>

namespace fs = std::filesystem;

namespace stats {

Client::Client(Handler handler) : handler_(std::move(handler)) {}
Client::~Client() { Shutdown(); }

void Client::Start() {
    if (thread_.joinable()) return;
    stop_ = false;
    thread_ = std::thread(&Client::Run, this);
}

void Client::Run() {
    win::EnsureWinsock();
    double retry = 0.5;
    while (!stop_) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        bool receivedData = false;
        if (s != INVALID_SOCKET) {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(49123);
            inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
            if (connect(s, (sockaddr*)&a, sizeof(a)) == 0) {
                DWORD tv = 500;  // recv timeout (ms)
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
                sock_ = (std::uintptr_t)s;
                std::string buf;
                char tmp[65536];
                while (!stop_) {
                    int n = recv(s, tmp, sizeof(tmp), 0);
                    if (n == SOCKET_ERROR) {
                        if (WSAGetLastError() == WSAETIMEDOUT) continue;
                        break;
                    }
                    if (n == 0) break;
                    receivedData = true;
                    buf.append(tmp, n);
                    size_t pos;
                    while ((pos = buf.find('\n')) != std::string::npos) {
                        std::string line = buf.substr(0, pos);
                        buf.erase(0, pos + 1);
                        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                        if (line.empty()) continue;
                        try {
                            json ev = json::parse(line);
                            json data = ev.at("Data");
                            if (data.is_string()) data = json::parse(data.get<std::string>());
                            handler_(ev.value("Event", std::string("")), data);
                        } catch (...) {
                            continue;
                        }
                    }
                }
                if (!stop_ && receivedData)
                    handler_("StatsDisconnected", json::object());
            }
            sock_ = ~(std::uintptr_t)0;
            closesocket(s);
        }
        if (receivedData) retry = 0.5;
        if (stop_) break;
        Sleep((DWORD)(retry * 1000));
        retry = retry * 2 > 5.0 ? 5.0 : retry * 2;
    }
}

void Client::Shutdown() {
    stop_ = true;
    std::uintptr_t s = sock_.load();
    if (s != ~(std::uintptr_t)0) {
        shutdown((SOCKET)s, SD_BOTH);
    }
    if (thread_.joinable()) thread_.join();
}

void EnsureIni(const fs::path& cookedDir, int port) {
    fs::path ini = (cookedDir / ".." / "Config" / "DefaultStatsAPI.ini").lexically_normal();
    std::error_code ec;
    fs::create_directories(ini.parent_path(), ec);
    fs::path bak = ini;
    bak += ".ripple-bak";
    if (fs::exists(ini, ec) && !fs::exists(bak, ec)) {
        std::ifstream in(ini, std::ios::binary);
        std::ofstream out(bak, std::ios::binary);
        out << in.rdbuf();
    }
    std::string body = "[TAGame.MatchStatsExporter_TA]\r\n; Ripple RL - Stats API\r\nPort=" +
                       std::to_string(port) + "\r\nPacketSendRate=10\r\n";
    std::ofstream f(ini, std::ios::binary | std::ios::trunc);
    f << body;
}

}  // namespace stats
