#pragma once
// Native port of ripple/quickchat.py: global hotkeys and key sequences
// that type configured chat lines into Rocket League via SendInput. Hotkeys are
// registered on a dedicated message-pump thread; sequences are polled on another.
#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace quickchat {

std::string KeyLabel(long long code);  // for misc.qc.labels / setKey

class Engine {
public:
    Engine();
    ~Engine();
    void Reload(const json& qc);  // qc feature state: {enabled, entries, keys}

private:
    struct Impl;
    Impl* p_;
};

}  // namespace quickchat
