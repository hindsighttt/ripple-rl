#pragma once
// Native port of ripple/ipcserver.py (server side): a loopback TCP
// server speaking line-delimited JSON. The existing ImGui UI (ui-cpp/src/
// Backend.*) connects to this unchanged.
//
//   client -> server request : {"type": "<command>", "payload": {...}, "id": <int>}
//   server -> client reply    : {"type": "reply", "id": <int>, "payload": {...}}  (or "error")
//   server -> client event    : {"type": "<event>", "payload": {...}}             (no id)
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using json = nlohmann::json;

class IpcServer {
public:
    ~IpcServer();

    bool Start(unsigned short port);
    void Stop();

    // Serialize `obj` as one JSON line and send it to every connected client.
    void Broadcast(const json& obj);

    // Call on the main thread: run every queued request through `fn`.
    void Drain(const std::function<void(const json&)>& fn);

private:
    void AcceptLoop();
    void ReadLoop(std::uintptr_t client);

    static constexpr std::uintptr_t kInvalid = ~static_cast<std::uintptr_t>(0);

    std::uintptr_t listen_ = kInvalid;  // SOCKET
    std::atomic<bool> running_{false};
    std::thread accept_;
    std::vector<std::thread> readers_;

    std::mutex clientsMtx_;
    std::vector<std::uintptr_t> clients_;  // SOCKETs

    std::mutex qMtx_;
    std::deque<json> queue_;
};
