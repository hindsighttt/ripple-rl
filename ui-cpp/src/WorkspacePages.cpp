#include "WorkspacePages.h"

#include "Cupertino.h"
#include "MenuTheme.h"
#include "MenuWidgets.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <utility>

namespace Workspace {
    using Cupertino::Px;

    namespace {
        enum class FieldType { Text, Choice, Check, Number, Slider, Action, Visibility, BallUnit, Decimals, RosterStyle };

        struct Field {
            const char* label;
            FieldType type = FieldType::Text;
            const char* setting = nullptr;
        };
        struct Section {
            const char* title;
            std::initializer_list<Field> fields;
            std::initializer_list<const char*> columns;
            bool canvas = false;
        };
        struct View {
            const char* title;
            std::initializer_list<Section> sections;
        };
        struct Destination {
            Page page;
            const char* title;
            std::initializer_list<View> views;
        };

        using enum FieldType;
        using enum Page;

        constexpr Page general[] = {Home};
        constexpr Page items[] = {Items, Spawn, Active, Presets, DropOpening, Drops, Credits, Simulations};
        constexpr Page customization[] = {
            Garage, CustomBall, CustomDecals, BoostMeter, Sounds, Workshop, Weather, AprilFools, MapChanger,
            MapMerger, CustomCars, CarPalette, Camera, Stadium,
        };
        constexpr Page inGame[] = {FreePlay, Mutators, MatchOptions, RocketLeagueSettings};
        constexpr Page plugins[] = {InstantSuite, ResetCounter, ImpulseSetup, Tas};
        constexpr Page profile[] = {PlatformDisplay, Titles, FakeRank};
        constexpr Page online[] = {PingSpoof, Invite, Multiplayer, FakeFriends};
        constexpr Page tools[] = {Tracker, FpsCounter, QuickChat, MatchBinds, Overlay, ReplayEditor};
        constexpr Page help[] = {Features, Tutorial, Updates};
        constexpr Page application[] = {Settings};

        constexpr Group groups[] = {
            {"General", general}, {"Items", items}, {"Customization", customization},
            {"In game", inGame}, {"Plugins", plugins}, {"Profile", profile}, {"Online", online},
            {"Tools", tools}, {"Help", help}, {"Application", application},
        };

        const Destination destinations[] = {
            {Home, "Home", {}}, {Items, "Items", {}}, {Spawn, "Spawn", {}},
            {Garage, "Garage", {}}, {Titles, "Titles", {}}, {Settings, "Settings", {}},

            {Active, "Active swaps", {{nullptr, {
                {"Current swaps", {{"Install", Choice}, {"Revert selected", Action}}, {"Owned item", "Replacement", "Install"}},
            }}}},
            {Presets, "Presets", {{nullptr, {
                {"Preset library", {{"Search presets"}, {"Create preset", Action}, {"Import preset", Action}}, {"Name", "Last modified"}},
            }}}},
            {DropOpening, "Drop opening", {{nullptr, {
                {"Opening animation", {{"Old roulette animation", Check, "on"}}},
            }}}},
            {Drops, "Drops", {
                {"Spawn", {{"Drop catalog", {{"Drop type", Choice}, {"Quantity", Number}, {"Spawn drops", Action}}}}},
                {"Spawned", {{"Spawned drops", {{"Remove selected", Action}}, {"Drop", "Quantity"}}}},
                {"Drop items", {{"Drop items", {{"Search items"}, {"Rarity", Choice}}, {"Item", "Rarity"}}}},
            }},
            {Credits, "Credits", {{nullptr, {
                {"Balance", {{"Credits", Number}, {"Tournament credits", Number}}},
                {"Adjust balance", {{"Type", Choice}, {"Amount", Number}, {"Add credits", Action}}},
            }}}},
            {Simulations, "Simulations", {{nullptr, {
                {"Rocket Pass", {{"Enable", Check}, {"Tier", Number}, {"Premium", Check}}},
            }}}},

            {CustomBall, "Custom Ball", {{nullptr, {
                {"Ball packs", {{"Search packs"}, {"Import pack", Action}}},
                {"Preview", {}, {}, true},
            }}}},
            {CustomDecals, "Custom Decals", {{nullptr, {
                {"Decal packs", {{"Car", Choice}, {"Search packs"}, {"Import pack", Action}}},
                {"Preview", {}, {}, true},
            }}}},
            {BoostMeter, "Boost Meter", {{nullptr, {
                {"Boost meter packs", {{"Search packs"}, {"Import pack", Action}}},
                {"Preview", {}, {}, true},
            }}}},
            {Sounds, "Sounds", {{nullptr, {
                {"Sound options", {{"Alpha Boost sound", Check}, {"Sound cue", Choice}, {"Volume", Slider},
                                   {"Import sound", Action}}},
            }}}},
            {Workshop, "Workshop", {{nullptr, {
                {"Map library", {{"Search maps"}, {"Sort by", Choice}, {"Downloaded only", Check},
                                 {"Import map", Action}}, {"Map", "Source"}},
            }}}},
            {Weather, "Weather", {{nullptr, {
                {"Arena weather", {{"Weather preset", Choice}}},
            }}}},
            {AprilFools, "April Fools", {{nullptr, {
                {"Item scale", {{"Topper size", Slider}, {"Antenna size", Slider}, {"Reset sizes", Action}}},
            }}}},
            {MapChanger, "Map changer", {{nullptr, {
                {"Arena changes", {{"Original arena", Choice}, {"Replacement arena", Choice}, {"Add change", Action}}},
            }}}},
            {MapMerger, "Map merger", {{nullptr, {
                {"Arena combinations", {{"Base arena", Choice}, {"Decor source", Choice}, {"Add merge", Action}}},
            }}}},
            {CustomCars, "Custom Cars", {{nullptr, {
                {"Car packs", {{"Body", Choice}, {"Search packs"}, {"Import pack", Action}}},
                {"Preview", {}, {}, true},
            }}}},
            {CarPalette, "Car Palette", {{nullptr, {
                {"Car palette", {{"Enable custom palette", Check}, {"Team", Choice}, {"Primary color", Choice},
                                 {"Secondary color", Choice}}},
            }}}},
            {Camera, "Camera", {{nullptr, {
                {"Camera limits", {{"Remove camera limits", Check}}},
            }}}},
            {Stadium, "Stadium", {{nullptr, {
                {"Arena appearance", {{"Team", Choice}, {"Enable color override", Check}, {"Color", Choice},
                                      {"Rainbow", Check}, {"Rainbow speed", Slider}}},
                {"Main menu", {{"Background arena", Choice}}},
            }}}},

            {FreePlay, "Free Play", {{nullptr, {
                {"Training options", {{"Infinite jump", Check}, {"Goal message", Check}}},
            }}}},
            {Mutators, "Mutators", {
                {"Match", {{"Match rules", {{"Game mode", Choice}, {"Rumble", Choice}, {"Game speed", Choice},
                                             {"Gravity", Choice}, {"Audio", Choice}}}}},
                {"Ball", {{"Ball physics", {{"Type", Choice}, {"Max speed", Choice}, {"Number of balls", Number},
                                             {"Size", Slider}, {"Bounciness", Slider}}}}},
                {"Car", {{"Car rules", {{"Boost strength", Choice}, {"Boost pad respawn", Choice},
                                        {"Boost restriction", Choice}, {"Dodge timer", Choice},
                                        {"Input restriction", Choice}}}}},
            }},
            {MatchOptions, "Match options", {{nullptr, {
                {"Match behavior", {{"Unanonymize players", Check}, {"Auto GG", Check}, {"Alpha Boost for other players", Check},
                                    {"Show boost used", Check}, {"Auto save replay", Check}, {"Boost used reset key"}}},
            }}}},
            {RocketLeagueSettings, "RL settings", {{nullptr, {
                {"Account", {{"Platform", Choice}, {"Account", Choice}}},
                {"Saved settings", {{"Save a copy", Action}, {"Restore selected", Action}}},
            }}}},

            {InstantSuite, "Instant Suite", {{nullptr, {
                {"Queue again", {{"Enable", Check}, {"Delay", Number}, {"Wait until the replay is saved", Check}}},
                {"After the match", {{"Go to Free Play", Check}, {"Back to the main menu", Check},
                                     {"Exclude private matches and tournaments", Check}, {"Exclude Casual", Check}}},
            }}}},
            {ResetCounter, "Reset Counter", {{nullptr, {
                {"Flip reset counter", {{"Show counter", Check}, {"Reset key"}}},
            }}}},
            {ImpulseSetup, "Impulse Setup", {{nullptr, {
                {"Free Play setup", {{"Active setup", Choice}, {"Place key"}, {"Capture key"}}},
            }}}},
            {Tas, "TAS", {
                {"Files", {{"Recording library", {{"Search recordings"}, {"Import recording", Action}}, {"Recording", "Duration"}}}},
                {"Loaded", {{"Loaded recording", {}}, {"Recorded inputs and resets", {}}}},
                {"Controls", {{"Control bindings", {{"Throttle"}, {"Steer"}, {"Air roll"}, {"Powerslide"},
                                                     {"Jump"}, {"Boost"}, {"Start"}}}}},
                {"Settings", {{"Recording and playback", {{"Hitbox type", Choice}, {"Air sensitivity", Slider},
                                                           {"Steer sensitivity", Slider}, {"Playback speed", Slider},
                                                           {"Recording speed", Slider}, {"Start replay on reset", Check}}}}},
            }},

            {PlatformDisplay, "Platform Display", {{nullptr, {
                {"Scoreboard", {{"Enable platform display", Check}, {"Steam profile pictures", Check},
                                {"Other platforms' logos", Check}, {"Scoreboard key"}}},
            }}}},
            {FakeRank, "Fake Rank", {
                {"Rank", {{"Rank settings", {{"Enable", Check}, {"Enable when Ripple RL starts", Check},
                                              {"Playlist", Choice}, {"Rank", Choice}, {"Division", Choice}, {"MMR", Number}}},
                          {"Active overrides", {}}}},
                {"Season reward", {{"Reward settings", {{"Enable override", Check}, {"Reward level", Choice},
                                                         {"Wins toward next level", Number}}}}},
            }},
            {PingSpoof, "Ping Spoof", {{nullptr, {
                {"Ping display", {{"Enable override", Check}, {"Show my spoofed ping on my row", Check},
                                  {"Ping", Number}, {"Random range", Check}, {"Minimum", Number}, {"Maximum", Number}}},
            }}}},
            {Invite, "Invite", {{nullptr, {
                {"Invite settings", {{"Enable invites", Check}, {"Copy match players", Action}}},
            }}}},
            {Multiplayer, "Multiplayer", {
                {"Public rooms", {{"Room browser", {{"Search rooms"}, {"Refresh rooms", Action}}, {"Room", "Map", "Players"}}}},
                {"Host / join", {{"Ripple RL LAN", {{"Room address"}, {"Map", Choice}, {"List in public rooms", Check},
                                                {"Host room", Action}, {"Join room", Action}}}}},
                {"Players", {{"Room roster", {}, {"Player", "Team"}}}},
                {"Chat", {{"Room chat", {{"Message"}, {"Send", Action}}}}},
            }},
            {FakeFriends, "Fake Friends", {{nullptr, {
                {"Friend list", {{"Epic account ID"}, {"Add entry", Action}}},
            }}}},

            {Tracker, "Tracker", {
                {"Match", {}},
                {"Settings", {{"Rank and MMR", {{"Show MMR", Check}, {"Show rank", Check}, {"Playlist", Choice}, {"Shortcut"}}},
                              {"Session overlay", {{"Style", RosterStyle, "overlayStyle"},
                                                   {"Visibility", Visibility, "rosterEnabled"}}},
                              {"Ball speed overlay", {{"Enable", Check, "ballEnabled"}, {"Unit", BallUnit, "ballUnit"},
                                                      {"Decimals", Decimals, "ballDecimals"}}}}},
            }},
            {FpsCounter, "FPS Counter", {{nullptr, {
                {"Performance overlay", {{"Show the FPS counter", Check, "on"}}},
            }}}},
            {QuickChat, "Custom Quick Chat", {{nullptr, {
                {"Quick chat", {{"Enable custom quick chats", Check}, {"Global chat key"}, {"Team chat key"}, {"Party chat key"}}},
                {"Messages", {{"Add message", Action}}, {"Message", "Binding"}},
            }}}},
            {MatchBinds, "Match binds", {{nullptr, {
                {"Match keys", {{"Freeze"}, {"Spectate"}, {"Blue team"}, {"Orange team"}}},
            }}}},
            {Overlay, "Overlay", {
                {"Controller", {{"Controller overlay", {{"Show overlay", Check}, {"Skin", Choice}, {"Controller count", Number},
                                                         {"Show only in-game", Check}, {"Show when unfocused", Check}}}}},
                {"Keyboard / mouse", {{"Keyboard and mouse overlay", {{"Show overlay", Check}, {"Keyboard layout", Choice},
                                                                        {"Show only in-game", Check}, {"Show when unfocused", Check}}}}},
            }},
            {ReplayEditor, "Replay editor", {{nullptr, {
                {"Replay", {{"Open replay", Action}}},
                {"Preview", {}, {}, true},
                {"Timeline", {{"Playback position", Slider}}},
            }}}},

            {Features, "Features", {{nullptr, {
                {"Feature overview", {{"Search features"}}},
            }}}},
            {Tutorial, "Tutorial", {
                {"Items", {{"Item swaps", {}}}},
                {"Custom Ball", {{"Ball packs", {}}}},
                {"Custom Decals", {{"Decal packs", {}}}},
                {"Boost Meter", {{"Boost meter packs", {}}}},
                {"Workshop", {{"Workshop maps", {}}}},
                {"Tracker", {{"Match data and overlays", {}}}},
            }},
            {Updates, "Updates", {{nullptr, {
                {"Release notes", {}},
                {"Updates", {{"Check for updates", Action}}},
            }}}},
        };

        const Destination& Get(Page page) {
            const auto match = std::find_if(std::begin(destinations), std::end(destinations),
                                            [page](const Destination& entry) { return entry.page == page; });
            return match != std::end(destinations) ? *match : destinations[0];
        }

        std::string Lower(std::string_view value) {
            std::string lowered(value);
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lowered;
        }

        nlohmann::json RenderField(const Field& field, const nlohmann::json* settings) {
            nlohmann::json change;
            const auto* current = settings && field.setting && settings->contains(field.setting)
                                      ? &settings->at(field.setting) : nullptr;
            ImGui::PushID(field.label);
            ImGui::BeginDisabled(!current);
            switch (field.type) {
                case Text: {
                    std::string empty;
                    ImGui::SetNextItemWidth(-1);
                    MenuWidgets::Field("##value", "—", empty);
                    break;
                }
                case Choice: {
                    ImGui::SetNextItemWidth(-1);
                    if (MenuWidgets::BeginCombo("##value", "Select"))
                        MenuWidgets::EndCombo();
                    break;
                }
                case Check: {
                    bool value = current && *current == true;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - Px(24));
                    if (MenuWidgets::Checkbox("##enabled", &value))
                        change = value;
                    break;
                }
                case Number: {
                    std::string empty;
                    ImGui::SetNextItemWidth(-1);
                    MenuWidgets::Field("##value", "—", empty, false, ImGuiInputTextFlags_CharsDecimal);
                    break;
                }
                case Slider: {
                    int value = 0;
                    ImGui::SetNextItemWidth(-1);
                    MenuWidgets::Slider("##value", &value, 0, 100);
                    break;
                }
                case Action:
                    MenuWidgets::Button(field.label, ImVec2(-1, 0));
                    break;
                case Visibility: {
                    constexpr const char* choices[] = {"Hidden", "Visible"};
                    int value = current && *current == true ? 1 : 0;
                    ImGui::SetNextItemWidth(-1);
                    if (MenuWidgets::Combo("##value", &value, choices, IM_ARRAYSIZE(choices)))
                        change = value == 1;
                    break;
                }
                case BallUnit: {
                    constexpr const char* choices[] = {"km/h", "mph", "m/s"};
                    constexpr const char* units[] = {"kmh", "mph", "ms"};
                    int value = 0;
                    if (current && *current == "mph")
                        value = 1;
                    else if (current && (*current == "ms" || *current == "mps" || *current == "m/s"))
                        value = 2;
                    ImGui::SetNextItemWidth(-1);
                    if (MenuWidgets::Combo("##value", &value, choices, IM_ARRAYSIZE(choices)))
                        change = units[value];
                    break;
                }
                case Decimals: {
                    constexpr const char* choices[] = {"0", "1", "2", "3"};
                    int value = current && current->is_number_integer() ? ImClamp(current->get<int>(), 0, 3) : 2;
                    ImGui::SetNextItemWidth(-1);
                    if (MenuWidgets::Combo("##value", &value, choices, IM_ARRAYSIZE(choices)))
                        change = value;
                    break;
                }
                case RosterStyle: {
                    constexpr const char* choices[] = {"Compact", "Glassmorph", "Pill", "Sidebar", "Vivid"};
                    constexpr const char* keys[] = {"compact", "glassmorph", "pill", "sidebar", "vivid"};
                    int value = 0;
                    for (int i = 0; current && i < IM_ARRAYSIZE(keys); ++i) {
                        if (*current == keys[i])
                            value = i;
                    }
                    ImGui::SetNextItemWidth(-1);
                    if (MenuWidgets::Combo("##value", &value, choices, IM_ARRAYSIZE(choices)))
                        change = keys[value];
                    break;
                }
            }
            ImGui::EndDisabled();
            ImGui::PopID();
            return change;
        }

        nlohmann::json RenderSections(std::initializer_list<Section> sections, const nlohmann::json* settings = nullptr) {
            nlohmann::json changes;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(ImVec2(14, 10)));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, MenuTheme::Surface);
            constexpr auto flags = ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY;
            constexpr auto noScroll = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
            for (const auto& section : sections) {
                if (MenuWidgets::BeginPane(section.title, ImVec2(0, 0), flags, noScroll)) {
                    ImGui::PushFont(ImGui::GetIO().FontDefault, Px(16));
                    ImGui::TextUnformatted(section.title);
                    ImGui::PopFont();
                    if (section.fields.size() != 0) {
                        ImGui::Spacing();
                        const float available = ImGui::GetContentRegionAvail().x;
                        const float controlWidth = ImClamp(available * 0.39f, Px(106), Px(248));
                        for (const auto& field : section.fields) {
                            const float right = ImGui::GetCursorPosX() + available;
                            if (field.type == Action) {
                                ImGui::SetCursorPosX(right - controlWidth);
                            } else {
                                ImGui::AlignTextToFramePadding();
                                ImGui::TextUnformatted(field.label);
                                if (ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x <
                                    right - controlWidth - Px(8))
                                    ImGui::SameLine(right - controlWidth);
                                else
                                    ImGui::SetCursorPosX(right - controlWidth);
                            }
                            // Controls are aligned without a table or separator grid.
                            auto change = RenderField(field, settings);
                            if (!change.is_null())
                                changes[field.setting] = std::move(change);
                        }
                    }
                    if (section.columns.size() != 0 &&
                        ImGui::BeginTable("##previewList", static_cast<int>(section.columns.size()),
                                          ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableNextRow();
                        for (const auto* column : section.columns) {
                            ImGui::TableNextColumn();
                            ImGui::TextDisabled("%s", column);
                        }
                        ImGui::EndTable();
                        ImGui::Separator();
                        ImGui::Dummy(ImVec2(1, Px(26)));
                    } else if (section.canvas) {
                        const ImVec2 origin = ImGui::GetCursorScreenPos();
                        const ImVec2 size(ImGui::GetContentRegionAvail().x, Px(104));
                        ImGui::GetWindowDrawList()->AddRectFilled(origin, origin + size,
                                                                  ImGui::GetColorU32(MenuTheme::Background), Px(5));
                        ImGui::Dummy(size);
                    } else if (section.fields.size() == 0) {
                        ImGui::Dummy(ImVec2(1, Px(25)));
                    }
                }
                MenuWidgets::EndPane();
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            return changes;
        }
        int Stat(const nlohmann::json& row, const char* key, int fallback = 0) {
            const auto value = row.find(key);
            return value != row.end() && value->is_number_integer() ? value->get<int>() : fallback;
        }

        void RenderMatch(const nlohmann::json* match) {
            if (!match || !match->is_object() || !match->contains("players") || !match->at("players").is_array() ||
                match->at("players").empty() || !match->contains("game") || !match->at("game").is_object()) {
                ImGui::TextDisabled("Waiting for a Rocket League match…");
                return;
            }
            const auto& game = match->at("game");
            int goals[2]{};
            const auto teams = game.find("Teams");
            if (teams != game.end() && teams->is_array()) {
                for (const auto& team : *teams) {
                    const int index = Stat(team, "TeamNum", -1);
                    if (index >= 0 && index < 2)
                        goals[index] = Stat(team, "Score");
                }
            }
            const int time = ImMax(0, Stat(game, "TimeSeconds"));
            ImGui::Text("Blue %d     %s%d:%02d     %d Orange", goals[0], game.contains("bOvertime") && game.at("bOvertime") == true ? "OT " : "",
                        time / 60, time % 60, goals[1]);
            ImGui::Spacing();
            constexpr const char* columns[] = {"Player", "Team", "Score", "Goals", "Assists", "Saves", "Shots"};
            if (ImGui::BeginTable("##scoreboard", IM_ARRAYSIZE(columns),
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                for (auto* column : columns)
                    ImGui::TableSetupColumn(column);
                ImGui::TableHeadersRow();
                for (const auto& player : match->at("players")) {
                    if (!player.is_object())
                        continue;
                    const int team = Stat(player, "TeamNum", -1);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const auto name = player.find("Name");
                    ImGui::TextUnformatted(name != player.end() && name->is_string() ? name->get_ref<const std::string&>().c_str() : "—");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(team == 0 ? "Blue" : team == 1 ? "Orange" : "—");
                    for (const char* field : {"Score", "Goals", "Assists", "Saves", "Shots"}) {
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", Stat(player, field));
                    }
                }
                ImGui::EndTable();
            }
        }
    }

    std::span<const Group> Groups() {
        return groups;
    }

    const char* Title(Page page) {
        return Get(page).title;
    }

    bool IsPreview(Page page) {
        return Get(page).views.size() != 0;
    }

    bool Matches(Page page, std::string_view query) {
        const auto& spec = Get(page);
        const auto needle = Lower(query);
        if (Lower(spec.title).find(needle) != std::string::npos)
            return true;
        return std::any_of(spec.views.begin(), spec.views.end(), [&](const View& view) {
            return view.title && Lower(view.title).find(needle) != std::string::npos;
        });
    }

    nlohmann::json Render(Page page, int& tab, const nlohmann::json* settings, const nlohmann::json* match) {
        const auto& spec = Get(page);
        if (!IsPreview(page))
            return {};
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(ImGui::GetIO().FontDefault, Px(23));
        ImGui::TextUnformatted(spec.title);
        ImGui::PopFont();
        if (page != FpsCounter && page != Tracker) {
            constexpr const char* preview = "UI preview";
            ImGui::SameLine(right - ImGui::CalcTextSize(preview).x);
            ImGui::TextDisabled("%s", preview);
        }
        ImGui::Spacing();

        const int count = static_cast<int>(spec.views.size());
        tab = ImClamp(tab, 0, count - 1);
        if (count > 1) {
            for (int i = 0; i < count; ++i) {
                const auto* label = spec.views.begin()[i].title;
                if (i > 0) {
                    const auto& style = ImGui::GetStyle();
                    const float tabWidth = ImGui::CalcTextSize(label).x + 2 * style.FramePadding.x;
                    if (ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + style.ItemSpacing.x + tabWidth < right)
                        ImGui::SameLine();
                }
                if (MenuWidgets::Tab(label, tab == i))
                    tab = i;
            }
        }
        const MenuWidgets::ContentFade tabEntrance("##workspaceEntrance", tab);
        if (page == Tracker && tab == 0) {
            RenderMatch(match);
            return {};
        }
        ImGui::PushID(tab);
        auto changes = RenderSections(spec.views.begin()[tab].sections, settings);
        ImGui::PopID();
        return changes;
    }

    void RenderGeneralSettings() {
        RenderSections({
            {"Appearance", {{"Language", Choice}, {"Theme", Choice}}},
            {"Startup and window", {{"Launch platform", Choice}, {"Launch Ripple RL on Windows startup", Check},
                                    {"Show Rocket League status on Discord", Check}, {"Keep window on top", Check},
                                    {"Minimize to system tray on close", Check}, {"Bring-to-front hotkey"}}},
        });
    }

    void RenderMaintenancePreview() {
        RenderSections({{"Troubleshooting", {{"Fix launch issues", Action}, {"Refresh catalog", Action}}}});
    }

    void RenderAbout() {
        RenderSections({{"Ripple RL", {{"Community", Action}}}});
    }
}
