#include "IpcServer.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "WinUtil.h"

#include <algorithm>
#include <string>

IpcServer::~IpcServer() { Stop(); }

bool IpcServer::Start(unsigned short port) {
    win::EnsureWinsock();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }
    if (listen(s, 8) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }
    listen_ = (std::uintptr_t)s;
    running_ = true;
    accept_ = std::thread(&IpcServer::AcceptLoop, this);
    return true;
}

void IpcServer::AcceptLoop() {
    while (running_) {
        SOCKET c = accept((SOCKET)listen_, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        int one = 1;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
        std::lock_guard<std::mutex> lk(clientsMtx_);
        clients_.push_back((std::uintptr_t)c);
        readers_.emplace_back(&IpcServer::ReadLoop, this, (std::uintptr_t)c);
    }
}

void IpcServer::ReadLoop(std::uintptr_t client) {
    SOCKET c = (SOCKET)client;
    std::string buf;
    char tmp[65536];
    while (running_) {
        int n = recv(c, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;
            try {
                json m = json::parse(line);
                std::lock_guard<std::mutex> lk(qMtx_);
                queue_.push_back(std::move(m));
            } catch (...) {
            }
        }
    }
    {
        std::lock_guard<std::mutex> lk(clientsMtx_);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), client), clients_.end());
    }
    closesocket(c);
}

void IpcServer::Broadcast(const json& obj) {
    std::string data = obj.dump();
    data.push_back('\n');
    std::lock_guard<std::mutex> lk(clientsMtx_);
    for (auto it = clients_.begin(); it != clients_.end();) {
        SOCKET c = (SOCKET)*it;
        if (send(c, data.data(), (int)data.size(), 0) == SOCKET_ERROR) {
            closesocket(c);
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

void IpcServer::Drain(const std::function<void(const json&)>& fn) {
    for (;;) {
        json m;
        {
            std::lock_guard<std::mutex> lk(qMtx_);
            if (queue_.empty()) return;
            m = std::move(queue_.front());
            queue_.pop_front();
        }
        fn(m);
    }
}

void IpcServer::Stop() {
    running_ = false;
    if (listen_ != kInvalid) {
        closesocket((SOCKET)listen_);
        listen_ = kInvalid;
    }
    if (accept_.joinable()) accept_.join();
    {
        std::lock_guard<std::mutex> lk(clientsMtx_);
        for (auto c : clients_) closesocket((SOCKET)c);
        clients_.clear();
    }
    for (auto& t : readers_)
        if (t.joinable()) t.join();
    readers_.clear();
}
