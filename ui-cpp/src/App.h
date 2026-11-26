#pragma once

#include "WorkspacePages.h"

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <utility>
#include <vector>

class Backend;
class ImageCache;

class App {
public:
    App(Backend& backend, ImageCache& images);

    enum class WindowCommand { None, Minimize, ToggleMaximize, Close };
    WindowCommand Render();

    // Pushed backend events (home.changed, app.toast, tracker.changed, ...).
    void OnEvent(const std::string& type, const nlohmann::json& payload);

private:
    void Sidebar();
    void RenderContent();
    struct WorkspaceSettings;
    void WorkspaceSettingsPanel(WorkspaceSettings& settings, const char* get, const char* set);
    void HomePanel();
    void TitlesPanel();
    void SpawnPanel(bool swapMode);
    void CustomTitlesPanel();
    void LoadCustomTitles();
    void SettingsPanel();
    void PackagePatchesPanel();
    void GaragePatchPanel();
    void GaragePanel();
    void LoadGarage();
    void FlushGarage();
    void SetGarageField(int document, const char* field, nlohmann::json value);
    void LoadSettings();
    void PollHome(bool force);
    void Toast(const std::string& text, bool ok);

    void LoadTitles();
    void LoadSpawnMeta();
    void LoadSpawnItems(bool swapMode);
    void RefreshSpawnList();

    struct CatalogSelection {
        std::set<int> rows;
        int anchor = -1;
        void Click(int index, bool extend, bool toggle);
        void Clear();
    };

    Backend& backend_;
    ImageCache& images_;
    using Page = Workspace::Page;
    Page page_ = Page::Home;
    int previewTab_ = 0;
    int settingsTab_ = 0;
    std::string sidebarSearch_;
    bool resetItemBrowser_ = true;

    nlohmann::json home_;        // last home.state result
    bool haveHome_ = false;
    bool launchBusy_ = false;
    bool usernameSaving_ = false;
    int launchMode_ = 0;
    std::string usernameDraft_;
    double lastHomePoll_ = -100.0;

    std::string toast_;
    bool toastOk_ = true;
    double toastUntil_ = 0.0;

    struct TitleRow {
        std::string id;
        std::string text;
        std::string category;
        std::string searchText;
        unsigned rgb;
    };
    struct SpawnedRow {
        std::string instanceId;
        std::string label;
        std::string titleId;
    };
    struct ItemRow {
        long long itemId;
        std::string label;
        std::string category;
        std::string rarity;
        std::string key;
        std::string thumbnail;
    };

    std::vector<TitleRow> titleRows_;
    std::vector<std::string> titleCategories_;
    std::set<std::string> activeTitleIds_;
    std::vector<SpawnedRow> spawnedTitles_;
    CatalogSelection titleSelection_;
    bool titlesRequested_ = false;
    bool titlesLoaded_ = false;
    int titleTab_ = 0;
    nlohmann::json customTitles_;
    std::string customTitleText_;
    std::string customTitleEdit_;
    float customTitleColor_[3] = {1.0f, 1.0f, 1.0f};
    float customTitleGlow_[3] = {1.0f, 0.25f, 0.25f};
    bool customTitleHasGlow_ = false;
    bool customTitlesRequested_ = false;
    bool customTitlePending_ = false;
    bool hideActiveTitles_ = false;
    bool titleScrollReset_ = true;
    int titleCategory_ = 0;
    int titleActionsPending_ = 0;
    std::string titleSearch_;
    std::string titleLoadError_;

    std::vector<ItemRow> spawnItems_;
    std::vector<std::pair<std::string, std::string>> spawnCats_;  // (label, slug)
    std::vector<SpawnedRow> spawnedItems_;
    CatalogSelection itemSelection_;
    bool spawnMetaLoaded_ = false;
    bool spawnItemsDirty_ = true;
    bool spawnLoading_ = true;
    bool spawnScrollReset_ = true;
    bool showSpawnedItems_ = false;
    bool itemActionPending_ = false;
    int spawnCat_ = 0;
    int spawnPaint_ = 0;
    int spawnRarity_ = 0;
    int spawnInventory_ = 0;
    unsigned spawnQuery_ = 0;
    double spawnLoadAfter_ = 0.0;
    std::string spawnSearch_;
    std::string spawnLoadError_;
    ItemRow swapTarget_{};
    bool choosingSwapReplacement_ = false;

    struct GarageDocument {
        nlohmann::json values;
        nlohmann::json queued = nlohmann::json::object();
        std::string error;
        double saveAfter = 0;
        bool loading = false;
        bool saving = false;
    };
    enum GarageSource { Garage, Paint, GarageSourceCount };
    GarageDocument garage_[GarageSourceCount];
    int garageTab_ = 0;
    int paintTeam_ = 0;
    bool garageRequested_ = false;

    struct WorkspaceSettings {
        nlohmann::json values;
        std::string error;
        bool busy = false;
    };
    WorkspaceSettings trackerSettings_;
    nlohmann::json liveMatch_;
    WorkspaceSettings fpsSettings_;
    WorkspaceSettings rouletteSettings_;
    WorkspaceSettings packagePatches_;
    WorkspaceSettings garagePatch_;

    nlohmann::json settings_;
    bool settingsRequested_ = false;
    std::string epicPath_;
    std::string steamPath_;
};
