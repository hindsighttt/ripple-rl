#include "Backend.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

namespace {
    bool g_wsaReady = false;

    void EnsureWSA() {
        if (!g_wsaReady) {
            WSADATA data;
            if (WSAStartup(MAKEWORD(2, 2), &data) == 0)
                g_wsaReady = true;
        }
    }
}

Backend::~Backend() {
    Disconnect();
}

bool Backend::Connect(const char* host, unsigned short port) {
    EnsureWSA();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return false;
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
    sock_ = static_cast<std::uintptr_t>(s);
    running_.store(true);
    connected_.store(true);
    reader_ = std::thread(&Backend::ReaderLoop, this);
    return true;
}

void Backend::Disconnect() {
    running_.store(false);
    connected_.store(false);
    if (sock_ != static_cast<std::uintptr_t>(~0ull)) {
        shutdown(static_cast<SOCKET>(sock_), SD_BOTH);
        closesocket(static_cast<SOCKET>(sock_));
        sock_ = static_cast<std::uintptr_t>(~0ull);
    }
    if (reader_.joinable())
        reader_.join();
}

void Backend::SendLine(const std::string& line) {
    if (!connected_.load())
        return;
    std::lock_guard<std::mutex> lock(sendMutex_);
    std::string out = line;
    out.push_back('\n');
    const char* p = out.c_str();
    int left = static_cast<int>(out.size());
    while (left > 0) {
        int n = send(static_cast<SOCKET>(sock_), p, left, 0);
        if (n <= 0) {
            connected_.store(false);
            return;
        }
        p += n;
        left -= n;
    }
}

void Backend::Request(const std::string& command, const json& payload, Reply onReply) {
    json msg;
    msg["type"] = command;
    msg["payload"] = payload;
    if (onReply) {
        int id;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            id = nextId_++;
            pending_[id] = std::move(onReply);
        }
        msg["id"] = id;
    }
    SendLine(msg.dump());
}

void Backend::Deliver(const json& msg) {
    const std::string type = msg.value("type", "");
    if (type == "reply" && msg.contains("id")) {
        const int id = msg["id"].get<int>();
        Reply cb;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            auto it = pending_.find(id);
            if (it != pending_.end()) {
                cb = std::move(it->second);
                pending_.erase(it);
            }
        }
        if (cb) {
            std::lock_guard<std::mutex> lock(queueMutex_);
            completed_.emplace_back(std::move(cb), msg);
        }
        return;
    }
    std::lock_guard<std::mutex> lock(queueMutex_);
    events_.push_back(msg);
}

void Backend::ReaderLoop() {
    std::string buf;
    char chunk[65536];
    while (running_.load()) {
        int n = recv(static_cast<SOCKET>(sock_), chunk, sizeof(chunk), 0);
        if (n <= 0)
            break;
        buf.append(chunk, static_cast<size_t>(n));
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (line.empty())
                continue;
            json msg = json::parse(line, nullptr, false);
            if (!msg.is_discarded())
                Deliver(msg);
        }
    }
    connected_.store(false);
}

void Backend::Poll() {
    std::vector<std::pair<Reply, json>> completed;
    std::vector<json> events;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        completed.swap(completed_);
        events.swap(events_);
    }
    for (auto& [cb, msg] : completed) {
        const std::string error = msg.value("error", "");
        const json payload = msg.contains("payload") ? msg["payload"] : json::object();
        cb(payload, error);
    }
    if (eventHandler_) {
        for (auto& msg : events) {
            const std::string type = msg.value("type", "");
            const json payload = msg.contains("payload") ? msg["payload"] : json::object();
            eventHandler_(type, payload);
        }
    }
}
