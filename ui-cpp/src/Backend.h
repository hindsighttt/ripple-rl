#pragma once

// Client for the native backend's line-delimited JSON over loopback TCP.
// Requests are sent from the UI thread; replies and pushed events are delivered
// back on the UI thread during Poll(), so panel code never needs locking.

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using json = nlohmann::json;

class Backend {
public:
    // payload is the reply's result; error is non-empty if the command failed.
    using Reply = std::function<void(const json& payload, const std::string& error)>;
    using EventHandler = std::function<void(const std::string& type, const json& payload)>;

    ~Backend();

    bool Connect(const char* host, unsigned short port);
    void Disconnect();
    bool Connected() const { return connected_.load(); }

    // Fire a command. If onReply is set, it runs on the UI thread during Poll()
    // once the backend answers. Safe to call before Connect() (queued).
    void Request(const std::string& command, const json& payload = json::object(), Reply onReply = nullptr);

    // Drain completed replies and events on the UI thread; call once per frame.
    void Poll();
    void OnEvent(EventHandler handler) { eventHandler_ = std::move(handler); }

private:
    void ReaderLoop();
    void SendLine(const std::string& line);
    void Deliver(const json& msg);

    std::uintptr_t sock_ = static_cast<std::uintptr_t>(~0ull);  // SOCKET
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread reader_;

    std::mutex sendMutex_;
    std::mutex queueMutex_;
    int nextId_ = 1;
    std::map<int, Reply> pending_;                         // id -> callback (UI thread owns)
    std::vector<std::pair<Reply, json>> completed_;        // reader -> Poll()
    std::vector<json> events_;                             // reader -> Poll()
    EventHandler eventHandler_;
};
