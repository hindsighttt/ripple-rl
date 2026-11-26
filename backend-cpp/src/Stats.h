#pragma once
// Native port of ripple/statsapi.py: a reconnecting loopback TCP client
// that reads the game's Stats API stream (line-delimited {Event, Data} JSON on
// 127.0.0.1:49123) and invokes a callback per event.
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace stats {

class Client {
public:
    using Handler = std::function<void(const std::string& event, const json& data)>;
    explicit Client(Handler handler);
    ~Client();

    void Start();
    void Shutdown();

private:
    void Run();
    Handler handler_;
    std::atomic<bool> stop_{false};
    std::atomic<std::uintptr_t> sock_{~(std::uintptr_t)0};
    std::thread thread_;
};

// Write DefaultStatsAPI.ini into the install's Config dir to enable the stream.
void EnsureIni(const std::filesystem::path& cookedDir, int port = 49123);

}  // namespace stats
