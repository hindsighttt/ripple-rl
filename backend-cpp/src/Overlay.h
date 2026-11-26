#pragma once
// Native port of ripple/overlay.py: the ball-speed, roster and FPS
// overlays. Qt's frameless always-on-top transparent-for-input windows become
// layered (colour-keyed), click-through, topmost Win32 windows driven from a
// dedicated message-pump thread.
#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

class Overlays {
public:
    Overlays();
    ~Overlays();

    void ShowBall(bool on);
    void ShowRoster(bool on);
    void ShowFps(bool on);
    bool BallVisible() const;
    bool RosterVisible() const;
    bool FpsVisible() const;

    void SetBall(double speedUu, const std::string& unit, int decimals);
    void ClearBall();
    void SetRoster(const json& players);
    void SetRosterStyle(const std::string& style);
    void SetFpsText(std::wstring text);

private:
    struct Impl;
    Impl* p_;
};
