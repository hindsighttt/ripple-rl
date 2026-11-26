#include "App.h"
#include "Backend.h"
#include "MenuTheme.h"
#include "MenuWidgets.h"

#include "Cupertino.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>

using namespace Cupertino;

namespace {
    constexpr const char* kGet[] = {"garage.get", "paint.get"};
    constexpr const char* kSet[] = {"garage.set", "paint.set"};
    constexpr unsigned kStockFlipColor = 0x9297FE;

    struct Effect {
        const char* tab;
        const char* key;
        const char* rainbowKey;
    };

    constexpr Effect kEffects[] = {
        {"Underglow", "underglow", "underglowRainbow"},
        {"Headlights", "headlights", "headlightsRainbow"},
        {"Taillights", "taillights", "taillightsRainbow"},
        {"Wheel camber", "camber", nullptr},
        {"Flip reset", "flipColor", nullptr},
        {"Paint", nullptr, nullptr},
        {"Wheels", "wheels", nullptr},
        {"Boost", "boost", nullptr},
    };
    enum Tab {
        Underglow, Headlights, Taillights, Camber, FlipReset, BodyPaint, Wheels, Boost,
    };

    const json& Field(const json& object, const char* name) {
        static const json missing;
        const auto it = object.find(name);
        return it == object.end() ? missing : *it;
    }

    template <typename T>
    T Read(const json& object, const char* name, T fallback) {
        const auto& value = Field(object, name);
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            if (value.is_number())
                return value.get<T>();
        } else if constexpr (std::is_same_v<T, bool>) {
            if (value.is_boolean())
                return value.get<bool>();
        } else if (value.is_string()) {
            return value.get<T>();
        }
        return fallback;
    }

    json TeamArray(const json& values, const char* name, json defaultEntry) {
        const json& stored = Field(values, name);
        json rows = stored.is_array() ? stored : json::array();
        while (rows.size() < 2)
            rows.push_back(defaultEntry);
        for (size_t team = 0; team < 2; ++team) {
            if (rows[team].type() != defaultEntry.type())
                rows[team] = defaultEntry;
        }
        return rows;
    }

    unsigned Rgb(std::string_view hex) {
        if (hex.starts_with('#'))
            hex.remove_prefix(1);
        if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string_view::npos)
            return 0xFFFFFF;
        return std::strtoul(std::string(hex).c_str(), nullptr, 16);
    }

    std::string Hex(unsigned value) {
        char text[8];
        std::snprintf(text, sizeof(text), "#%06x", value & 0xFFFFFF);
        return text;
    }

    bool ColorControl(const char* label, unsigned& color) {
        float channels[] = {float((color >> 16) & 255) / 255, float((color >> 8) & 255) / 255,
                            float(color & 255) / 255};
        ImGui::PushID(label ? label : "compactColor");
        if (label)
            ImGui::TextDisabled("%s", label);
        ImGui::SetNextItemWidth(-1);
        const bool changed = MenuWidgets::Color("##rgb", channels);
        ImGui::PopID();
        if (changed) {
            color = 0;
            for (const float channel : channels)
                color = (color << 8) | static_cast<unsigned>(std::lround(ImClamp(channel, 0.0f, 1.0f) * 255));
        }
        return changed;
    }

    void LightPreview(unsigned color, bool enabled, bool rainbow, int level) {
        const ImVec2 size(ImGui::GetContentRegionAvail().x, Px(17));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, origin + size, ImGui::GetColorU32(MenuTheme::Background), Px(3));
        if (enabled) {
            const ImVec2 min = origin + Px(ImVec2(3, 3));
            const ImVec2 max = origin + size - Px(ImVec2(3, 3));
            if (rainbow) {
                constexpr int stops = 7;
                const float width = (max.x - min.x) / stops;
                for (int i = 0; i != stops; ++i) {
                    const auto hue = [level](int stop) {
                        ImVec4 rgba(0, 0, 0, 0.42f + level * 0.025f);
                        ImGui::ColorConvertHSVtoRGB(float(stop) / stops, 0.65f, 1.0f, rgba.x, rgba.y, rgba.z);
                        return ImGui::GetColorU32(rgba);
                    };
                    draw->AddRectFilledMultiColor(ImVec2(min.x + width * i, min.y), ImVec2(min.x + width * (i + 1), max.y),
                                                  hue(i), hue(i + 1), hue(i + 1), hue(i));
                }
            } else {
                const unsigned alpha = 55 + 9 * level;
                const ImU32 tint = IM_COL32((color >> 16) & 255, (color >> 8) & 255, color & 255, alpha);
                draw->AddRectFilled(min, max, ImGui::GetColorU32(tint), Px(2));
            }
        }
        ImGui::Dummy(size);
    }

    bool TeamControls(json& team, bool* rainbow, bool brightness, const char* channel = nullptr) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Px(ImVec2(8, 4)));
        if (channel) {
            const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            const float widths = ImGui::CalcTextSize("Enabled").x + ImGui::CalcTextSize("Rainbow").x
                               + Px(48) + ImGui::GetStyle().ItemSpacing.x;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(channel);
            ImGui::SameLine(right - widths);
        }
        bool enabled = Read(team, "on", false);
        bool changed = MenuWidgets::Checkbox("Enabled", &enabled);
        if (changed)
            team["on"] = enabled;
        if (rainbow) {
            ImGui::SameLine();
            MenuWidgets::Checkbox("Rainbow", rainbow);
        }

        unsigned color = Rgb(Read(team, "color", std::string("#ffffff")));
        if (ColorControl(channel ? nullptr : "Color", color)) {
            team["color"] = Hex(color);
            changed = true;
        }

        if (brightness) {
            int level = ImClamp(Read(team, "level", 10), 1, 20);
            ImGui::SetNextItemWidth(-1);
            if (MenuWidgets::Slider("##brightness", &level, 1, 20, "%d", "Brightness")) {
                team["level"] = level;
                changed = true;
            }
            LightPreview(color, enabled, rainbow && *rainbow, level);
        }
        ImGui::PopStyleVar();
        return changed;
    }

    template <typename RenderTeam>
    void TeamColumns(const char* id, RenderTeam render) {
        if (ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableNextRow();
            for (int team = 0; team != 2; ++team) {
                ImGui::TableNextColumn();
                ImGui::PushID(team);
                MenuTheme::Section(team == 0 ? "Blue team" : "Orange team");
                render(team);
                ImGui::Spacing();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    const char* Description(int tab) {
        switch (tab) {
            case Underglow: return "Set the light beneath your car for each team.";
            case Headlights: return "Set front light color and brightness for each team.";
            case Taillights: return "Set rear light color and brightness for each team.";
            case Camber: return "Adjust wheel tilt and position for both teams.";
            case FlipReset: return "Changes take effect the next time Rocket League launches through Ripple RL.";
            case Wheels: return "Color overrides apply to paintable wheels.";
            case Boost: return "Color overrides apply to Alpha Boost and other color-adjustable boosts.";
            default: return "";
        }
    }


}

void App::LoadGarage() {
    garageRequested_ = true;
    for (int source = 0; source < GarageSourceCount; ++source) {
        auto& document = garage_[source];
        if (document.loading || document.saving || !document.queued.empty())
            continue;
        document.loading = true;
        document.error.clear();
        backend_.Request(kGet[source], {}, [this, source](const json& payload, const std::string& error) {
            auto& document = garage_[source];
            document.loading = false;
            document.error = error;
            if (error.empty()) {
                if (payload.is_object())
                    document.values = payload;
                else
                    document.error = "The backend returned an invalid Garage response.";
            }
        });
    }
}

void App::SetGarageField(int source, const char* name, json value) {
    auto& document = garage_[source];
    document.values[name] = value;
    document.queued[name] = std::move(value);
    document.saveAfter = ImGui::GetTime() + 0.18;
}

void App::FlushGarage() {
    for (int source = 0; source < GarageSourceCount; ++source) {
        auto& document = garage_[source];
        if (document.loading || document.saving || document.queued.empty() || !document.error.empty() ||
            ImGui::GetTime() < document.saveAfter)
            continue;

        // Only send changed top-level fields. A newer local edit always wins over a failed request.
        json patch = std::move(document.queued);
        document.queued = json::object();
        document.saving = true;
        backend_.Request(kSet[source], patch, [this, source, sent = patch](const json& result, const std::string& error) {
            auto& document = garage_[source];
            document.saving = false;
            if (error.empty() && result.is_object() && result.value("ok", false))
                return;

            document.error = error.empty() ? "The backend could not save these changes." : error;
            for (const auto& [key, value] : sent.items()) {
                if (!document.queued.contains(key))
                    document.queued[key] = value;
            }
        });
    }
}

void App::GaragePanel() {
    if (!garageRequested_)
        LoadGarage();

    constexpr int tabCount = IM_ARRAYSIZE(kEffects);
    const float selectorWidth = ImMin(Px(176), ImGui::GetContentRegionAvail().x * 0.47f);
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Garage");
    ImGui::SameLine(right - selectorWidth);
    ImGui::SetNextItemWidth(selectorWidth);
    if (MenuWidgets::BeginCombo("##garageSection", kEffects[garageTab_].tab, tabCount)) {
        for (int tab = 0; tab < tabCount; ++tab) {
            if (MenuWidgets::Option(kEffects[tab].tab, garageTab_ == tab))
                garageTab_ = tab;
        }
        MenuWidgets::EndCombo();
    }

    const MenuWidgets::ContentFade tabEntrance("##garageEntrance", garageTab_);
    const auto& effect = kEffects[garageTab_];
    const bool lighting = garageTab_ <= Taillights;
    const bool paintTab = garageTab_ >= BodyPaint;
    const bool garageReady = garage_[Garage].values.is_object() && !garage_[Garage].loading;
    const bool paintReady = garage_[Paint].values.is_object() && !garage_[Paint].loading;
    const bool hasPaint = paintReady && Read(garage_[Paint].values, "plus", false);

    const float footer = ImGui::GetFrameHeight() + Px(17);
    ImGui::PushID(garageTab_);
    if (MenuWidgets::BeginPane("##garageControls", ImVec2(0, -footer))) {
        MenuTheme::Section(effect.tab);
        if (garageTab_ != BodyPaint)
            ImGui::TextDisabled("%s", Description(garageTab_));
        if (paintTab && paintReady && !hasPaint)
            ImGui::TextDisabled("This tab requires Plus.");

        if ((paintTab && !paintReady) || (!paintTab && !garageReady)) {
            const auto& state = garage_[paintTab ? Paint : Garage];
            ImGui::TextDisabled("%s", state.loading ? "Loading Garage settings..." : "Garage settings could not be loaded.");
        } else if (lighting) {
            auto teams = TeamArray(garage_[Garage].values, effect.key, json::object());
            auto flags = TeamArray(garage_[Paint].values, effect.rainbowKey, false);
            bool lightChanged = false;
            bool rainbowChanged = false;
            TeamColumns("##lighting", [&](int team) {
                const bool previous = flags[team].get<bool>();
                bool rainbow = previous;
                lightChanged |= TeamControls(teams[team], hasPaint ? &rainbow : nullptr, true);
                if (rainbow != previous) {
                    flags[team] = rainbow;
                    rainbowChanged = true;
                }
            });
            if (lightChanged)
                SetGarageField(Garage, effect.key, std::move(teams));
            if (rainbowChanged)
                SetGarageField(Paint, effect.rainbowKey, std::move(flags));
        } else if (garageTab_ == Camber) {
            auto adjustments = Field(garage_[Garage].values, "camber");
            if (!adjustments.is_object())
                adjustments = json::object();
            bool changed = false;
            const auto adjust = [&](const char* label, const char* key, float limit, const char* unit) {
                ImGui::PushID(key);
                ImGui::SetNextItemWidth(-1);
                float value = ImClamp(Read(adjustments, key, 0.0f), -limit, limit);
                if (MenuWidgets::Slider("##value", &value, -limit, limit, unit, label)) {
                    adjustments[key] = std::round(value * 10.0f) / 10.0f;
                    changed = true;
                }
                ImGui::PopID();
            };
            adjust("Wheel tilt", "deg", 22.5f, "%.1f°");
            adjust("Vertical offset", "ud", 5, "%.1f");
            adjust("Horizontal offset", "lr", 5, "%.1f");
            if (MenuWidgets::Button("Reset camber")) {
                adjustments["deg"] = 0;
                adjustments["ud"] = 0;
                adjustments["lr"] = 0;
                changed = true;
            }
            if (changed)
                SetGarageField(Garage, "camber", std::move(adjustments));
        } else if (garageTab_ == FlipReset) {
            const int saved = Read(garage_[Garage].values, "flipColor", -1);
            const bool custom = saved >= 0 && saved <= 0xFFFFFF;
            unsigned color = custom ? static_cast<unsigned>(saved) : kStockFlipColor;
            MenuTheme::Section("Effect color", custom ? "Custom" : "Game default");
            if (ColorControl("Color", color))
                SetGarageField(Garage, "flipColor", color);
            ImGui::BeginDisabled(!custom);
            if (MenuWidgets::Button("Restore game color"))
                SetGarageField(Garage, "flipColor", -1);
            ImGui::EndDisabled();
        } else if (paintTab) {
            ImGui::BeginDisabled(!hasPaint);
            const auto teamControls = [&](json& row, const char* channel = nullptr) {
                const bool previous = Read(row, "rainbow", false);
                bool rainbow = previous;
                bool changed = TeamControls(row, &rainbow, false, channel);
                if (rainbow != previous) {
                    row["rainbow"] = rainbow;
                    changed = true;
                }
                return changed;
            };
            if (garageTab_ == BodyPaint) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Team");
                ImGui::SameLine();
                if (MenuWidgets::Tab("Blue team", paintTeam_ == 0))
                    paintTeam_ = 0;
                ImGui::SameLine();
                if (MenuWidgets::Tab("Orange team", paintTeam_ == 1))
                    paintTeam_ = 1;

                struct PaintChannel {
                    const char* key;
                    const char* label;
                };
                constexpr PaintChannel channels[] = {
                    {"primary", "Primary color"}, {"secondary", "Secondary color"}, {"trim", "Trim color"},
                };
                const MenuWidgets::ContentFade teamEntrance("##paintTeamEntrance", paintTeam_);
                ImGui::PushID(paintTeam_);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, MenuTheme::Surface);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(ImVec2(12, 10)));
                constexpr auto childFlags = ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY;
                constexpr auto windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
                for (const auto& channel : channels) {
                    auto teams = TeamArray(garage_[Paint].values, channel.key, json::object());
                    bool changed = false;
                    if (MenuWidgets::BeginPane(channel.key, ImVec2(0, 0), childFlags, windowFlags))
                        changed = teamControls(teams[paintTeam_], channel.label);
                    MenuWidgets::EndPane();
                    if (changed)
                        SetGarageField(Paint, channel.key, std::move(teams));
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                ImGui::PopID();
                ImGui::Spacing();
            } else {
                auto teams = TeamArray(garage_[Paint].values, effect.key, json::object());
                bool changed = false;
                TeamColumns("##paint", [&](int team) { changed |= teamControls(teams[team]); });
                if (changed)
                    SetGarageField(Paint, effect.key, std::move(teams));
            }
            ImGui::SetNextItemWidth(-1);
            int speed = ImClamp(Read(garage_[Paint].values, "rainbowSpeed", 20), 1, 100);
            if (MenuWidgets::Slider("##rainbowSpeed", &speed, 1, 100, "%d", "Rainbow speed"))
                SetGarageField(Paint, "rainbowSpeed", speed);
            ImGui::EndDisabled();
        }
    }
    MenuWidgets::EndPane();
    ImGui::PopID();

    bool busy = false;
    std::string error;
    for (const auto& document : garage_) {
        busy |= document.loading || document.saving || (!document.queued.empty() && document.error.empty());
        if (error.empty() && !document.error.empty())
            error = document.error;
    }

    ImGui::Separator();
    ImGui::BeginDisabled(busy);
    if (MenuWidgets::Button(error.empty() ? "Reload" : "Retry")) {
        for (auto& document : garage_)
            document.error.clear();
        LoadGarage();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (error.empty()) {
        ImGui::TextDisabled("%s", busy ? "Syncing with the rebuild..." : "Changes save automatically");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 150, 177, 255));
        ImGui::TextWrapped("%s", error.c_str());
        ImGui::PopStyleColor();
    }
}
