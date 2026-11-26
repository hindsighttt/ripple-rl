#include "App.h"
#include "Backend.h"
#include "MenuTheme.h"
#include "MenuWidgets.h"

#include "Cupertino.h"

#include <algorithm>

using namespace Cupertino;

namespace {
    std::string Str(const json& value, const char* key, const char* fallback = "") {
        if (value.is_object() && value.contains(key) && value[key].is_string())
            return value[key].get<std::string>();
        return fallback;
    }

    void HomeTitle(const char* text, float size) {
        ImGui::PushFont(ImGui::GetIO().FontDefault, Px(size));
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
    }

    void GameStatus(const char* text, bool running) {
        const auto origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::CalcTextSize(text).x + Px(28), ImGui::GetFrameHeight());
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, origin + size, ImGui::GetColorU32(MenuTheme::Control), Px(4));
        draw->AddCircleFilled(origin + ImVec2(Px(10), size.y * 0.5f), Px(2.5f),
                              ImGui::GetColorU32(running ? IM_COL32(116, 213, 166, 255) : MenuTheme::Muted));
        MenuWidgets::Label(draw, origin + ImVec2(Px(17), 0), origin + size, text, 0, MenuTheme::Text);
        ImGui::Dummy(size);
    }

    bool CaptionButton(const char* label, int symbol) {
        const bool pressed = ImGui::InvisibleButton(label, Px(ImVec2(33, MenuTheme::HeaderHeight - 10)),
                                                     ImGuiButtonFlags_EnableNav);
        const ImRect bounds(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        auto* draw = ImGui::GetWindowDrawList();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride))
            draw->AddRectFilled(bounds.Min, bounds.Max, symbol == 2 ? MenuTheme::Action : MenuTheme::Hover, Px(4));
        const ImVec2 center = bounds.GetCenter();
        const float span = Px(4);
        if (symbol == 0)
            draw->AddLine(center - ImVec2(span, 0), center + ImVec2(span, 0), MenuTheme::Text, Px(1.25f));
        else if (symbol == 1)
            draw->AddRect(center - ImVec2(span, span), center + ImVec2(span, span), MenuTheme::Text);
        else {
            draw->AddLine(center - ImVec2(span, span), center + ImVec2(span, span), MenuTheme::Text, Px(1.25f));
            draw->AddLine(center + ImVec2(-span, span), center + ImVec2(span, -span), MenuTheme::Text, Px(1.25f));
        }
        ImGui::SetItemTooltip("%s", label);
        return pressed;
    }
}

void MenuTheme::Apply(float scale) {
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(10, 7) * scale;
    style.ItemSpacing = ImVec2(9, 7) * scale;
    style.FrameRounding = 3 * scale;
    style.GrabRounding = 4 * scale;
    style.WindowRounding = 6 * scale;
    style.ChildRounding = 5 * scale;
    style.ChildBorderSize = scale;
    style.CellPadding = ImVec2(10, 5) * scale;
    style.FrameBorderSize = 0;
    style.PopupRounding = 3 * scale;
    style.ScrollbarRounding = 4 * scale;
    style.ScrollbarSize = 11 * scale;
    style.ScrollbarPadding = 2 * scale;
    style.GrabMinSize = 15 * scale;

    auto& colors = style.Colors;
    const auto set = [&](ImGuiCol slot, ImU32 color) { colors[slot] = ImGui::ColorConvertU32ToFloat4(color); };
    set(ImGuiCol_WindowBg, Background);
    set(ImGuiCol_ChildBg, Background);
    set(ImGuiCol_PopupBg, Surface);
    set(ImGuiCol_Text, Text);
    set(ImGuiCol_TextDisabled, Muted);
    set(ImGuiCol_Border, Border);
    set(ImGuiCol_Separator, Border);
    set(ImGuiCol_FrameBg, Control);
    set(ImGuiCol_FrameBgHovered, Hover);
    set(ImGuiCol_FrameBgActive, Hover);
    set(ImGuiCol_Button, Control);
    set(ImGuiCol_ButtonHovered, Hover);
    set(ImGuiCol_ButtonActive, Selected);
    set(ImGuiCol_Header, Selected);
    set(ImGuiCol_HeaderHovered, Hover);
    set(ImGuiCol_HeaderActive, Selected);
    set(ImGuiCol_CheckMark, Accent);
    set(ImGuiCol_CheckboxSelectedBg, Selected);
    set(ImGuiCol_TableBorderStrong, Border);
    set(ImGuiCol_TableBorderLight, Border);
    set(ImGuiCol_SliderGrab, Accent);
    set(ImGuiCol_SliderGrabActive, ActionHover);
    set(ImGuiCol_TextSelectedBg, Action);
    set(ImGuiCol_ScrollbarBg, Rail);
    set(ImGuiCol_ScrollbarGrab, Action);
    set(ImGuiCol_ScrollbarGrabHovered, Accent);
    set(ImGuiCol_TableRowBgAlt, Surface);
    set(ImGuiCol_TableHeaderBg, Surface);
    set(ImGuiCol_ScrollbarGrabActive, Accent);
    set(ImGuiCol_NavCursor, Accent);
}

void MenuTheme::Section(std::string_view title, std::string_view detail) {
    const ImVec2 size(ImGui::GetContentRegionAvail().x, Px(32));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, origin + size, ImGui::GetColorU32(Surface), Px(4));
    const ImRect text(origin + Px(ImVec2(11, 0)), origin + size - Px(ImVec2(11, 0)));
    MenuWidgets::Label(draw, text.Min, text.Max, title, 0, Text);
    if (!detail.empty())
        MenuWidgets::Label(draw, text.Min, text.Max, detail, 1, Muted);
    ImGui::Dummy(size);
}

App::App(Backend& backend, ImageCache& images) : backend_(backend), images_(images) {}

void App::Toast(const std::string& text, bool ok) {
    toast_ = text;
    toastOk_ = ok;
    toastUntil_ = ImGui::GetTime() + 3.0;
}

void App::OnEvent(const std::string& type, const json& payload) {
    if (type == "home.changed") {
        PollHome(true);
    } else if (type == "home.launchResult") {
        launchBusy_ = false;
        const auto error = Str(payload, "error");
        Toast(error.empty() ? "Launch started" : error, error.empty());
        PollHome(true);
    } else if (type == "app.toast") {
        Toast(Str(payload, "text"), payload.value("ok", true));
    } else if (type == "tracker.changed") {
        liveMatch_ = payload;
    }
}

void App::PollHome(bool force) {
    const double now = ImGui::GetTime();
    if (!force && now - lastHomePoll_ < 3.0)
        return;
    lastHomePoll_ = now;
    backend_.Request("home.state", {}, [this](const json& payload, const std::string& error) {
        if (error.empty() && payload.is_object()) {
            // Refreshes can pick up a saved username without clobbering edits in progress.
            if (!haveHome_ || (!usernameSaving_ && usernameDraft_ == Str(home_, "pseudo")))
                usernameDraft_ = Str(payload, "pseudo");
            home_ = payload;
            haveHome_ = true;
        }
    });
}

App::WindowCommand App::Render() {
    if (backend_.Connected())
        FlushGarage();
    MenuTheme::Apply(Environment().Scale());
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("Ripple RL##workspace", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(ImGui::GetIO().FontDefault, Px(14));

    const float headerHeight = Px(MenuTheme::HeaderHeight);
    const ImVec2 top = viewport->Pos;
    const ImVec2 bottom = top + viewport->Size;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(top + Px(ImVec2(1, 1)), ImVec2(bottom.x - Px(1), top.y + headerHeight),
                        MenuTheme::Rail, Px(6), ImDrawFlags_RoundCornersTop);
    draw->AddRect(top + Px(ImVec2(1, 1)), bottom - Px(ImVec2(1, 1)), MenuTheme::Border, Px(6));
    draw->AddLine(top + ImVec2(Px(12), headerHeight), ImVec2(bottom.x - Px(12), top.y + headerHeight),
                  MenuTheme::Border);
    Typography::Draw(draw, Font::System(15, FontWeight::Semibold),
                     ImRect(top + Px(ImVec2(16, 1)), top + Px(ImVec2(100, MenuTheme::HeaderHeight))),
                     Rgba::Hex(0xEDE9ED), "Ripple RL");

    WindowCommand command = WindowCommand::None;
    ImGui::SetCursorPos(ImVec2(viewport->Size.x - Px(108), Px(5)));
    if (CaptionButton("Minimize", 0))
        command = WindowCommand::Minimize;
    ImGui::SameLine(0, 0);
    if (CaptionButton("Maximize or restore", 1))
        command = WindowCommand::ToggleMaximize;
    ImGui::SameLine(0, 0);
    if (CaptionButton("Close", 2))
        command = WindowCommand::Close;

    const float railWidth = ImClamp(viewport->Size.x * 0.23f, Px(150), Px(204));
    const float bodyY = headerHeight + Px(10);
    const float bodyHeight = viewport->Size.y - bodyY - Px(11);
    ImGui::SetCursorPos(ImVec2(Px(11), bodyY));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, MenuTheme::Rail);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(ImVec2(7, 7)));
    if (MenuWidgets::BeginPane("##sectionRail", ImVec2(railWidth, bodyHeight),
                              ImGuiChildFlags_AlwaysUseWindowPadding))
        Sidebar();
    MenuWidgets::EndPane();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine(0, Px(10));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, MenuTheme::Rail);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(ImVec2(10, 10)));
    const bool catalog = page_ == Page::Items || page_ == Page::Spawn || page_ == Page::Titles || page_ == Page::Garage;
    ImGui::PushID(static_cast<int>(page_));
    if (MenuWidgets::BeginPane("##page", ImVec2(-Px(11), bodyHeight),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          catalog ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0)) {
        RenderContent();
    }
    MenuWidgets::EndPane();
    ImGui::PopID();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::PopFont();
    ImGui::End();
    return command;
}

void App::Sidebar() {
    const auto link = [&](const char* label, Page page) {
        const auto id = ImGui::GetID(label);
        if (ImGui::InvisibleButton(label, ImVec2(ImGui::GetContentRegionAvail().x, Px(29)),
                                   ImGuiButtonFlags_EnableNav) && page_ != page) {
            if (page == Page::Items || page == Page::Spawn)
                resetItemBrowser_ = true;
            if (page == Page::Tracker && !trackerSettings_.busy)
                trackerSettings_ = {};
            if (page == Page::FpsCounter && !fpsSettings_.busy)
                fpsSettings_ = {};
            page_ = page;
            previewTab_ = 0;
        }
        if (!ImGui::IsItemVisible())
            return;
        const bool selected = page_ == page;
        const float active = MenuWidgets::Ease(id, "navActive", selected ? 1.0f : 0.0f, selected ? 1.0f : 0.0f);
        const float hover = MenuWidgets::Ease(id, "navHover", ImGui::IsItemHovered() ? 1.0f : 0.0f);
        const ImRect bounds(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(
            MenuWidgets::Mix(MenuWidgets::Mix(MenuTheme::Rail, MenuTheme::Hover, hover), MenuTheme::Selected, active)), Px(4));
        const float marker = Px(7) * active;
        if (marker > 0.25f) {
            const ImVec2 center(bounds.Min.x + Px(7), bounds.GetCenter().y);
            draw->AddRectFilled(center - ImVec2(Px(1.35f), marker), center + ImVec2(Px(1.35f), marker),
                                ImGui::GetColorU32(MenuTheme::Accent, active), Px(1.35f));
        }
        MenuWidgets::Label(draw, bounds.Min + Px(ImVec2(15 + 8 * active, 0)), bounds.Max, label, 0,
                           MenuWidgets::Mix(MenuTheme::Muted, MenuTheme::Text, ImMax(active, hover)));
    };

    const auto group = [&](const Workspace::Group& group) {
        const auto* label = group.label;
        const bool active = std::find(group.pages.begin(), group.pages.end(), page_) != group.pages.end();
        ImGui::PushID(label);
        const auto id = ImGui::GetID("##group");
        auto* state = ImGui::GetStateStorage();
        bool open = state->GetBool(id, active);
        if (ImGui::InvisibleButton("##group", ImVec2(ImGui::GetContentRegionAvail().x, Px(31)),
                                   ImGuiButtonFlags_EnableNav)) {
            open = !open;
            state->SetBool(id, open);
        }
        const float reveal = MenuWidgets::Ease(id, "navOpen", open ? 1.0f : 0.0f, open ? 1.0f : 0.0f, 19.0f);
        const ImRect bounds(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        auto* draw = ImGui::GetWindowDrawList();
        const float hover = MenuWidgets::Ease(id, "groupHover", ImGui::IsItemHovered() ? 1.0f : 0.0f);
        draw->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(
            MenuWidgets::Mix(MenuTheme::Surface, MenuTheme::Hover, hover)), Px(4));
        MenuWidgets::Label(draw, bounds.Min + ImVec2(Px(10), 0), bounds.Max, label, 0, MenuTheme::Text);
        const ImVec2 center(bounds.Max.x - Px(15), bounds.GetCenter().y);
        const float tilt = ImLerp(-Px(2.5f), Px(2.5f), reveal);
        const auto accent = ImGui::GetColorU32(MenuWidgets::Mix(MenuTheme::Muted, MenuTheme::Text, hover));
        draw->AddLine(center + ImVec2(-Px(3.5f), -tilt * 0.65f), center + ImVec2(0, tilt), accent, Px(1.15f));
        draw->AddLine(center + ImVec2(0, tilt), center + ImVec2(Px(3.5f), -tilt * 0.65f), accent, Px(1.15f));

        if (reveal > 0.01f) {
            const float count = static_cast<float>(group.pages.size());
            const float height = Px(count * 29 + (count - 1) * 5) * reveal;
            if (MenuWidgets::BeginPane("##pages", ImVec2(0, height), ImGuiChildFlags_NavFlattened,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_NoBackground)) {
                ImGui::BeginDisabled(!open);
                for (const Page page : group.pages)
                    link(Workspace::Title(page), page);
                ImGui::EndDisabled();
            }
            MenuWidgets::EndPane();
        }
        ImGui::PopID();
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Px(ImVec2(5, 5)));
    ImGui::SetNextItemWidth(-1);
    MenuWidgets::Field("##pageSearch", "Find a page", sidebarSearch_, true);
    ImGui::Spacing();
    if (sidebarSearch_.empty()) {
        for (const auto& section : Workspace::Groups()) {
            if (section.pages.size() == 1 && std::string_view(section.label) == Workspace::Title(section.pages.front()))
                link(section.label, section.pages.front());
            else
                group(section);
        }
    } else {
        int matches = 0;
        for (const auto& section : Workspace::Groups()) {
            for (const Page page : section.pages) {
                if (Workspace::Matches(page, sidebarSearch_)) {
                    ++matches;
                    link(Workspace::Title(page), page);
                }
            }
        }
        if (matches == 0)
            ImGui::TextDisabled("No matching pages");
    }
    ImGui::PopStyleVar();
}

void App::RenderContent() {
    const MenuWidgets::ContentFade entrance("##pageEntrance");
    if (page_ == Page::Tracker) {
        WorkspaceSettingsPanel(trackerSettings_, "tracker.settings", "tracker.set");
        return;
    }
    if (page_ == Page::FpsCounter) {
        WorkspaceSettingsPanel(fpsSettings_, "fps.get", "fps.set");
        return;
    }
    if (page_ == Page::DropOpening) {
        WorkspaceSettingsPanel(rouletteSettings_, "roulette.get", "roulette.set");
        return;
    }
    if (Workspace::IsPreview(page_)) {
        Workspace::Render(page_, previewTab_);
        return;
    }
    if (!backend_.Connected()) {
        ImGui::TextDisabled("Connecting to the Ripple RL backend...");
        return;
    }
    switch (page_) {
        case Page::Home: HomePanel(); break;
        case Page::Items: SpawnPanel(true); break;
        case Page::Spawn: SpawnPanel(false); break;
        case Page::Garage: GaragePanel(); break;
        case Page::Titles: TitlesPanel(); break;
        case Page::Settings: SettingsPanel(); break;
        default: break;
    }
}

void App::WorkspaceSettingsPanel(WorkspaceSettings& settings, const char* get, const char* set) {
    const bool connected = backend_.Connected();
    const auto reply = [this, &settings](const json& payload, const std::string& error) {
        settings.busy = false;
        if (!error.empty() || !payload.is_object() || !payload.value("ok", true)) {
            settings.error = error.empty() ? "Could not update settings." : error;
            Toast(settings.error, false);
            return;
        }
        settings.error.clear();
        settings.values = payload;
    };
    if (connected && settings.values.is_null() && !settings.busy && settings.error.empty()) {
        settings.busy = true;
        backend_.Request(get, {}, reply);
    }

    ImGui::BeginDisabled(!connected || settings.busy || settings.values.is_null());
    auto changes = Workspace::Render(page_, previewTab_, &settings.values, connected ? &liveMatch_ : nullptr);
    ImGui::EndDisabled();
    if (!changes.is_null()) {
        settings.error.clear();
        settings.busy = true;
        backend_.Request(set, changes, reply);
    }

    if (!connected) {
        ImGui::TextDisabled("Connecting to the Ripple RL backend...");
    } else if (!settings.error.empty() && settings.values.is_null()) {
        ImGui::TextUnformatted(settings.error.c_str());
        if (MenuWidgets::Button("Retry"))
            settings.error.clear();
    }
}

void App::HomePanel() {
    PollHome(false);
    ImGui::Indent(Px(6));
    const float contentRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    HomeTitle("Home", 23);
    const auto version = Str(home_, "version");
    const auto versionLabel = version.empty() ? std::string() : "Ripple RL " + version;
    if (!versionLabel.empty()) {
        ImGui::SameLine(contentRight - ImGui::CalcTextSize(versionLabel.c_str()).x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", versionLabel.c_str());
    }
    ImGui::Spacing();

    const bool running = haveHome_ && home_.value("rlRunning", false);
    const auto install = Str(home_, "active");
    const auto account = Str(home_, "accountName");
    constexpr auto panelFlags = ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY;
    constexpr auto panelWindowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(ImVec2(17, 15)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, MenuTheme::Surface);
    if (MenuWidgets::BeginPane("##game", ImVec2(0, 0), panelFlags, panelWindowFlags)) {
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        const char* status = !haveHome_ ? "Checking" : running ? "Running" : "Closed";
        const float statusWidth = ImGui::CalcTextSize(status).x + Px(28);
        HomeTitle("Rocket League", 25);
        ImGui::SameLine(right - statusWidth);
        GameStatus(status, running);
        if (!install.empty()) {
            ImGui::TextDisabled("%s installation", install.c_str());
        } else if (haveHome_) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("No installation selected");
            ImGui::SameLine();
            if (MenuWidgets::Button("Open Settings"))
                page_ = Page::Settings;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Launch mode");
        constexpr const char* modes[] = {"With Easy Anti-Cheat", "Without Easy Anti-Cheat"};
        const float buttonWidth = Px(151);
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginDisabled(launchBusy_ || running || !haveHome_ || install.empty());
        ImGui::SetNextItemWidth(ImMax(Px(100), ImGui::GetContentRegionAvail().x - buttonWidth - gap));
        MenuWidgets::Combo("##launchMode", &launchMode_, modes, IM_ARRAYSIZE(modes));
        ImGui::SameLine();
        const bool launch = MenuWidgets::Button(launchBusy_ ? "Preparing..." : "Launch Rocket League",
                                                ImVec2(buttonWidth, 0), true);
        ImGui::EndDisabled();
        if (launch) {
            launchBusy_ = true;
            backend_.Request("home.launch", {{"noEac", launchMode_ == 1}},
                             [this](const json&, const std::string& error) {
                                 if (!error.empty()) {
                                     launchBusy_ = false;
                                     Toast(error, false);
                                 }
                             });
        }
    }
    MenuWidgets::EndPane();

    if (MenuWidgets::BeginPane("##profile", ImVec2(0, 0), panelFlags, panelWindowFlags)) {
        HomeTitle("Profile", 19);
        if (!account.empty())
            ImGui::TextDisabled("Rocket League account · %s", account.c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted("Ripple RL username");
        constexpr float saveWidth = 76;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginDisabled(!haveHome_ || usernameSaving_);
        ImGui::SetNextItemWidth(-Px(saveWidth) - gap);
        const bool enter = MenuWidgets::Field("##username", "Enter a username", usernameDraft_, false,
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool changed = usernameDraft_ != Str(home_, "pseudo");
        ImGui::BeginDisabled(!changed);
        const bool save = MenuWidgets::Button(usernameSaving_ ? "Saving..." : "Save", ImVec2(Px(saveWidth), 0), true);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (haveHome_ && !usernameSaving_ && changed && (enter || save)) {
            usernameSaving_ = true;
            backend_.Request("home.setPseudo", {{"value", usernameDraft_}},
                             [this](const json& payload, const std::string& error) {
                                 usernameSaving_ = false;
                                 if (!error.empty()) {
                                     Toast(error, false);
                                     return;
                                 }
                                 usernameDraft_ = Str(payload, "value");
                                 home_["pseudo"] = usernameDraft_;
                                 Toast("Username saved", true);
                                 PollHome(true);
                             });
        }
    }
    MenuWidgets::EndPane();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (MenuWidgets::Button("Refresh status"))
        PollHome(true);
    if (!toast_.empty() && ImGui::GetTime() < toastUntil_) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, toastOk_ ? MenuTheme::Text : IM_COL32(255, 150, 177, 255));
        ImGui::TextWrapped("%s", toast_.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Unindent(Px(6));
}

void App::LoadSettings() {
    settingsRequested_ = true;
    backend_.Request("settings.get", {}, [this](const json& payload, const std::string& error) {
        if (!error.empty()) {
            Toast(error, false);
            return;
        }
        settings_ = payload;
        epicPath_ = Str(payload, "epicPath");
        steamPath_ = Str(payload, "steamPath");
    });
}

void App::PackagePatchesPanel() {
    MenuTheme::Section("Package patches");
    auto& state = packagePatches_;
    const auto reply = [this](const json& result, const std::string& error) {
        auto& panel = packagePatches_;
        panel.busy = false;
        if (!error.empty() || !result.is_object()) {
            panel.error = error.empty() ? "Could not read package patch state." : error;
            return;
        }
        panel.error.clear();
        panel.values = result;
    };
    const auto send = [&](const char* command, const json& payload = json::object()) {
        state.busy = true;
        state.error.clear();
        backend_.Request(command, payload, reply);
    };
    if (state.values.is_null() && state.error.empty() && !state.busy)
        send("packagePatches.get");

    if (state.values.is_object()) {
        ImGui::TextDisabled("Applies when launching from Ripple RL, or use Apply now with Rocket League closed.");
        ImGui::BeginDisabled(state.busy);
        const auto& patches = state.values.at("patches");
        for (const auto& patch : patches) {
            const auto id = Str(patch, "id");
            const auto label = Str(patch, "label");
            const auto status = Str(patch, "state");
            bool enabled = patch.value("enabled", false);
            ImGui::PushID(id.c_str());
            if (MenuWidgets::Checkbox(label.c_str(), &enabled) && !state.busy)
                send("packagePatches.set", json{{"id", id}, {"enabled", enabled}});
            ImGui::SameLine();
            const char* description = status == "patched" ? "Managed package installed"
                                      : status == "original" ? "Original package installed"
                                      : status == "no-install" ? "Select a Rocket League install"
                                      : status == "missing" ? "Package missing" : "Unsupported package version";
            ImGui::TextDisabled("%s", description);
            ImGui::PopID();
        }
        if (!patches.empty() && MenuWidgets::Button("Apply now") && !state.busy)
            send("packagePatches.apply");
        if (!patches.empty()) ImGui::SameLine();
        if (MenuWidgets::Button("Refresh package status") && !state.busy)
            send("packagePatches.get");
        ImGui::EndDisabled();
    }
    if (state.busy) ImGui::TextDisabled("Checking package patches...");
    if (!state.error.empty()) {
        ImGui::TextWrapped("%s", state.error.c_str());
        if (state.values.is_null() && MenuWidgets::Button("Retry package patches"))
            send("packagePatches.get");
    }
}

void App::GaragePatchPanel() {
    MenuTheme::Section("Garage patch");
    auto& state = garagePatch_;
    const auto store = [this](const json& result, const std::string& error) {
        auto& panel = garagePatch_;
        panel.busy = false;
        if (!error.empty()) {
            panel.error = error;
            return;
        }
        panel.error.clear();
        panel.values = result;
    };
    const auto refresh = [this, store]() {
        garagePatch_.busy = true;
        garagePatch_.error.clear();
        backend_.Request("garage.patch.status", {}, store);
    };
    // apply/revert, then refresh the status so the panel reflects the result
    const auto action = [this, store](const char* command) {
        garagePatch_.busy = true;
        garagePatch_.error.clear();
        backend_.Request(command, {}, [this, store](const json&, const std::string& error) {
            if (!error.empty()) {
                garagePatch_.busy = false;
                garagePatch_.error = error;
                return;
            }
            backend_.Request("garage.patch.status", {}, store);
        });
    };
    if (state.values.is_null() && state.error.empty() && !state.busy)
        refresh();

    if (state.values.is_object()) {
        ImGui::TextDisabled("Camber, underglow and flip-reset. Re-apply after verifying game files; close Rocket League first.");
        const bool ok = state.values.value("ok", false);
        if (!ok) {
            ImGui::TextDisabled("%s", state.values.value("error", std::string("no patch data for this build")).c_str());
        } else if (state.values.contains("packages")) {
            for (const auto& item : state.values.at("packages").items()) {
                const std::string st = item.value().is_string() ? item.value().get<std::string>() : "?";
                const char* desc = st == "patched"   ? "patched"
                                   : st == "vanilla" ? "vanilla - needs re-apply"
                                   : st == "unknown" ? "unknown build - re-capture needed"
                                   : st == "missing" ? "missing"
                                                     : st.c_str();
                ImGui::TextDisabled("%s  -  %s", item.key().c_str(), desc);
            }
        }
        ImGui::BeginDisabled(state.busy);
        if (MenuWidgets::Button("Re-apply garage patch")) action("garage.patch.apply");
        ImGui::SameLine();
        if (MenuWidgets::Button("Revert to vanilla")) action("garage.patch.revert");
        ImGui::SameLine();
        if (MenuWidgets::Button("Refresh garage status")) refresh();
        ImGui::EndDisabled();
    }
    if (state.busy) ImGui::TextDisabled("Working...");
    if (!state.error.empty()) ImGui::TextWrapped("%s", state.error.c_str());
}

void App::SettingsPanel() {
    HomeTitle("Settings", 23);
    constexpr const char* tabs[] = {"General", "Rocket League", "Maintenance", "About"};
    for (int i = 0; i < IM_ARRAYSIZE(tabs); ++i) {
        if (i > 0)
            ImGui::SameLine();
        if (MenuWidgets::Tab(tabs[i], settingsTab_ == i)) {
            if (i == 2 && settingsTab_ != 2) {
                if (!packagePatches_.busy) packagePatches_.values = nullptr;
                if (!garagePatch_.busy) garagePatch_.values = nullptr;
            }
            settingsTab_ = i;
        }
    }
    ImGui::Spacing();
    const MenuWidgets::ContentFade tabEntrance("##settingsEntrance", settingsTab_);
    if (settingsTab_ == 0) {
        ImGui::TextDisabled("UI preview");
        Workspace::RenderGeneralSettings();
        return;
    }
    if (settingsTab_ == 3) {
        ImGui::TextDisabled("UI preview");
        Workspace::RenderAbout();
        return;
    }
    if (settingsTab_ == 2) {
        MenuTheme::Section("Files");
        if (MenuWidgets::Button("Open backup folder"))
            backend_.Request("settings.openBackupFolder", {});
        ImGui::Spacing();
        PackagePatchesPanel();
        ImGui::Spacing();
        GaragePatchPanel();
        ImGui::Spacing();
        ImGui::TextDisabled("UI preview");
        Workspace::RenderMaintenancePreview();
        return;
    }

    if (!settingsRequested_)
        LoadSettings();
    MenuTheme::Section("Rocket League installs");
    ImGui::Indent(Px(10));

    if (settings_.is_object()) {
        const auto active = Str(settings_, "activeSource");
        ImGui::TextDisabled("Active install   %s", active.empty() ? "None selected" : active.c_str());
        const auto install = [this](const char* source, std::string& path) {
            ImGui::PushID(source);
            ImGui::TextUnformatted(source);
            ImGui::SetNextItemWidth(-Px(116));
            MenuWidgets::Field("##path", "Rocket League install path", path);
            ImGui::SameLine();
            ImGui::BeginDisabled(path.empty());
            if (MenuWidgets::Button("Use install", ImVec2(Px(106), 0))) {
                backend_.Request("settings.setSourcePath", {{"source", source}, {"path", path}},
                                 [this](const json&, const std::string& error) {
                                     Toast(error.empty() ? "Install updated" : error, error.empty());
                                     if (error.empty())
                                         LoadSettings();
                                 });
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        };
        install("Epic", epicPath_);
        install("Steam", steamPath_);
    }
    if (MenuWidgets::Button("Refresh installs"))
        LoadSettings();
    ImGui::Unindent(Px(10));
    if (!toast_.empty() && ImGui::GetTime() < toastUntil_)
        ImGui::TextWrapped("%s", toast_.c_str());
}
