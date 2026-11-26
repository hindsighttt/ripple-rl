#pragma once

#include <nlohmann/json.hpp>

#include <span>
#include <string_view>

namespace Workspace {
    enum class Page {
        Home, Items, Spawn, Garage, Titles, Settings,
        Active, Presets, DropOpening, Drops, Credits, Simulations,
        CustomBall, CustomDecals, BoostMeter, Sounds, Workshop, Weather, AprilFools, MapChanger,
        MapMerger, CustomCars, CarPalette, Camera, Stadium,
        FreePlay, Mutators, MatchOptions, RocketLeagueSettings,
        InstantSuite, ResetCounter, ImpulseSetup, Tas,
        PlatformDisplay, FakeRank, PingSpoof, Invite, Multiplayer, FakeFriends,
        Tracker, FpsCounter, QuickChat, MatchBinds, Overlay, ReplayEditor,
    };

    struct Group {
        const char* label;
        std::span<const Page> pages;
    };

    std::span<const Group> Groups();
    const char* Title(Page page);
    bool IsPreview(Page page);
    bool Matches(Page page, std::string_view query);
    nlohmann::json Render(Page page, int& tab, const nlohmann::json* settings = nullptr,
                          const nlohmann::json* match = nullptr);
    void RenderGeneralSettings();
    void RenderMaintenancePreview();
    void RenderAbout();
}
