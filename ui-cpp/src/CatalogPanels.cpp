#include "App.h"
#include "Backend.h"
#include "ImageCache.h"
#include "MenuTheme.h"
#include "MenuWidgets.h"

#include "Cupertino.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <string_view>

using namespace Cupertino;

namespace {
    // Paint ids 0..18, matching ripple/spawn.py PAINTS.
    const char* const kPaints[] = {
        "Unpainted", "Crimson", "Lime", "Black", "Sky Blue", "Cobalt", "Burnt Sienna",
        "Forest Green", "Purple", "Pink", "Orange", "Grey", "Titanium White", "Saffron",
        "Gold", "Rose Gold", "White Gold", "Onyx", "Platinum"};
    unsigned RgbHex(std::string_view text, unsigned fallback) {
        if (text.starts_with('#'))
            text.remove_prefix(1);
        if (text.size() != 6 || text.find_first_not_of("0123456789abcdefABCDEF") != std::string_view::npos)
            return fallback;
        return std::strtoul(std::string(text).c_str(), nullptr, 16);
    }

    std::string ThumbUrl(const std::string& key) {
        std::string name = key.substr(key.rfind('.') + 1);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return "https://cdn.jsdelivr.net/gh/itemchanger/assets@main/assets/" + name + ".png";
    }

    std::string CategoryLabel(std::string category) {
        std::replace(category.begin(), category.end(), '_', ' ');
        return category;
    }

    struct Choice {
        const char* label;
        const char* key;
    };
    constexpr Choice kRarities[] = {
        {"All rarities", ""}, {"Common", "common"}, {"Uncommon", "uncommon"}, {"Rare", "rare"},
        {"Very Rare", "veryrare"}, {"Import", "import"}, {"Exotic", "exotic"},
        {"Black Market", "blackmarket"}, {"Limited", "limited"}, {"Legacy", "legacy"},
    };
    constexpr const char* kInventory[] = {"All items", "In my inventory", "Not in my inventory"};

    std::string Lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool Search(const char* id, const char* hint, std::string& query, float width = -1.0f) {
        ImGui::SetNextItemWidth(width);
        return MenuWidgets::Field(id, hint, query, true);
    }

    void Muted(std::string_view message) {
        ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted);
        ImGui::TextUnformatted(message.data(), message.data() + message.size());
        ImGui::PopStyleColor();
    }

    bool Tab(const char* label, bool active) {
        return MenuWidgets::Tab(label, active) && !active;
    }

    void BeginResults(const char* id, bool& reset) {
        if (reset)
            ImGui::SetNextWindowScroll(ImVec2(0, 0));
        reset = false;
        const float footerHeight = ImGui::GetFrameHeight() + Px(18);
        const float height = (std::max)(1.0f, ImGui::GetContentRegionAvail().y - footerHeight);
        MenuWidgets::BeginPane(id, ImVec2(0, height), ImGuiChildFlags_NavFlattened);
    }

    template <typename DrawCard>
    void DrawGrid(int count, float minimumWidth, int maximumColumns, float height, DrawCard drawCard) {
        if (count == 0)
            return;

        // Every card in a row has the same height, so clipped rows still contribute to the scrollbar.
        const float gap = Px(8);
        const float width = ImGui::GetContentRegionAvail().x;
        const int columns = std::clamp(static_cast<int>((width + gap) / (Px(minimumWidth) + gap)), 1, maximumColumns);
        const float cardWidth = (std::max)(1.0f, std::floor((width - gap * (columns - 1)) / columns));
        const ImVec2 cardSize(cardWidth, Px(height));
        const float rowStride = cardSize.y + gap;

        // Each results pane keeps its own entrance. A hidden tab stops drawing, so it
        // starts a new stagger on return without resetting during ordinary scrolling.
        auto* storage = ImGui::GetStateStorage();
        const auto lastFrameId = ImGui::GetID("##gridLastFrame");
        const auto startTimeId = ImGui::GetID("##gridStart");
        const auto firstCardId = ImGui::GetID("##gridFirstCard");
        const auto endCardId = ImGui::GetID("##gridEndCard");
        const int frame = ImGui::GetFrameCount();
        const float now = static_cast<float>(ImGui::GetTime());
        if (storage->GetInt(lastFrameId, -2) != frame - 1) {
            const int firstRow = static_cast<int>(ImGui::GetScrollY() / rowStride);
            const int rowsInView = static_cast<int>(std::ceil(ImGui::GetWindowHeight() / rowStride)) + 1;
            storage->SetFloat(startTimeId, now);
            storage->SetInt(firstCardId, firstRow * columns);
            storage->SetInt(endCardId, (firstRow + rowsInView) * columns);
        }
        storage->SetInt(lastFrameId, frame);
        const float elapsed = now - storage->GetFloat(startTimeId);
        const int firstCard = storage->GetInt(firstCardId);
        const int endCard = storage->GetInt(endCardId);
        const float styleAlpha = ImGui::GetStyle().Alpha;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        ImGuiListClipper clipper;
        clipper.Begin((count + columns - 1) / columns, rowStride);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const int index = row * columns + column;
                    if (index >= count)
                        break;
                    if (column > 0)
                        ImGui::SameLine();
                    float alpha = 1.0f;
                    if (elapsed < 0.45f && index >= firstCard && index < endCard) {
                        const float delay = ImMin(0.25f, (index - firstCard) * 0.019f);
                        const float remaining = 1.0f - ImSaturate((elapsed - delay) / 0.19f);
                        alpha = 1.0f - remaining * remaining * remaining;
                    }
                    ImGui::PushID(index);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, styleAlpha * alpha);
                    drawCard(index, cardSize);
                    ImGui::PopStyleVar();
                    ImGui::PopID();
                }
            }
        }
        ImGui::PopStyleVar();
    }

    struct Card {
        ImRect bounds;
        ImDrawList* draw;
        bool clicked;
    };

    Card BeginCard(ImVec2 size, bool selected) {
        const bool clicked = ImGui::InvisibleButton("##card", size, ImGuiButtonFlags_EnableNav);
        const ImRect bounds(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const bool focused = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        const ImU32 background = selected ? MenuTheme::Selected : focused ? MenuTheme::Hover : MenuTheme::Surface;
        draw->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(background), Px(4));
        return {bounds, draw, clicked};
    }

    void EndCard(const Card& card, bool selected) {
        const ImU32 border = ImGui::GetColorU32(selected ? MenuTheme::Accent : MenuTheme::Border);
        card.draw->AddRect(card.bounds.Min, card.bounds.Max, border, Px(4), 0, Px(selected ? 2 : 1));
    }

    void CardLine(const Card& card, float top, float height, const Font& font, Rgba color, std::string_view text) {
        const ImRect rect(card.bounds.Min + Px(ImVec2(11, top)),
                          ImVec2(card.bounds.Max.x - Px(11), card.bounds.Min.y + Px(top + height)));
        Typography::Draw(card.draw, font, rect, color, Typography::Truncate(font, text, rect.GetWidth()));
    }

    bool DrawTitleCard(std::string_view title, Rgba color, ImVec2 size, bool selected) {
        const Card card = BeginCard(size, selected);
        ImRect text = card.bounds;
        text.Expand(-Px(12));
        const Font font = Font::System(14, FontWeight::Semibold);
        const float height = Typography::Measure(font, title, text.GetWidth()).y;
        text.Min.y += (std::max)(0.0f, (text.GetHeight() - height) * 0.5f);
        card.draw->PushClipRect(text.Min, text.Max, true);
        Typography::DrawWrapped(card.draw, font, text, color, title, TextAlignment::Center);
        card.draw->PopClipRect();
        EndCard(card, selected);
        if (height > text.GetHeight())
            ImGui::SetItemTooltip("%.*s", static_cast<int>(title.size()), title.data());
        return card.clicked;
    }

    std::string SelectionLabel(const std::set<int>& rows, std::string_view single) {
        if (rows.empty())
            return "No selection";
        if (rows.size() == 1)
            return std::string(single);
        return std::to_string(rows.size()) + " selected";
    }

    void FooterText(std::string_view text, float reservedWidth, bool error = false) {
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x - reservedWidth - Px(12);
        if (width <= 0)
            return;
        const Font font = Font::System(13);
        const ImRect rect(position, position + ImVec2(width, ImGui::GetFrameHeight()));
        Typography::Draw(ImGui::GetWindowDrawList(), font, rect, Rgba::Hex(error ? 0xFF8E8E : 0xA7A3A0),
                         Typography::Truncate(font, text, width));
    }

    bool FooterButton(const char* label, bool disabled, float width = 84) {
        ImGui::BeginDisabled(disabled);
        const bool clicked = MenuWidgets::Button(label, ImVec2(Px(width), 0), true);
        ImGui::EndDisabled();
        return clicked;
    }

    float FooterStart(float width) {
        return ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - Px(width);
    }
} // namespace

void App::CatalogSelection::Click(int index, bool extend, bool toggle) {
    if (extend && anchor >= 0) {
        if (!toggle)
            rows.clear();
        for (int i = (std::min)(anchor, index); i <= (std::max)(anchor, index); ++i)
            rows.insert(i);
        return;
    }
    if (!toggle)
        rows.clear();
    if (!rows.erase(index))
        rows.insert(index);
    anchor = index;
}

void App::CatalogSelection::Clear() {
    rows.clear();
    anchor = -1;
}

void App::LoadTitles() {
    titlesRequested_ = true;
    backend_.Request("catalog.titles", {}, [this](const json& payload, const std::string& error) {
        titleLoadError_ = error;
        titlesLoaded_ = error.empty();
        if (!titlesLoaded_)
            return;
        const auto& titles = payload["titles"];
        const auto& colors = payload["categories"];
        std::set<std::string> categories;
        for (auto& item : titles.items()) {
            const auto& value = item.value();
            const auto name = value.value("Text", item.key());
            const auto category = value.value("Category", std::string());
            const auto style = colors.find(category);
            const unsigned rgb = style == colors.end() ? 0xF4F3F2
                : RgbHex(style->value("Color", std::string()), 0xF4F3F2);
            titleRows_.push_back({item.key(), name, category, Lower(name + " " + item.key()), rgb});
            if (!category.empty())
                categories.insert(category);
        }
        std::sort(titleRows_.begin(), titleRows_.end(),
                  [](const TitleRow& a, const TitleRow& b) { return a.text < b.text; });
        titleCategories_.assign(categories.begin(), categories.end());
    });
}

void App::RefreshSpawnList() {
    backend_.Request("spawn.list", {}, [this](const json& payload, const std::string& error) {
        if (!error.empty()) {
            Toast(error, false);
            return;
        }
        spawnedItems_.clear();
        spawnedTitles_.clear();
        activeTitleIds_.clear();
        if (showSpawnedItems_)
            itemSelection_.Clear();
        if (titleTab_ == 2 || hideActiveTitles_)
            titleSelection_.Clear();

        for (const auto& item : payload["items"]) {
            const auto instanceId = item.value("instanceId", std::string());
            const auto titleId = item.value("titleId", std::string());
            if (!titleId.empty()) {
                activeTitleIds_.insert(titleId);
                spawnedTitles_.push_back({instanceId, titleId, titleId});
                continue;
            }
            auto label = item.value("label", std::string("Item"));
            const auto paint = item.value("paintName", std::string());
            if (!paint.empty() && paint != "Unpainted")
                label += " (" + paint + ")";
            spawnedItems_.push_back({instanceId, std::move(label), {}});
        }
    });
}

void App::LoadSpawnMeta() {
    spawnMetaLoaded_ = true;
    backend_.Request("items.categories", {}, [this](const json& payload, const std::string& error) {
        spawnCats_ = {{"All categories", ""}};
        if (!error.empty()) {
            Toast(error, false);
            return;
        }
        for (const auto& category : payload["categories"]) {
            const auto slug = category.value("slug", std::string());
            spawnCats_.emplace_back(category.value("label", slug), slug);
        }
    });
}

void App::LoadSpawnItems(bool swapMode) {
    spawnItemsDirty_ = false;
    const unsigned query = spawnQuery_;
    // The catalog reply is filtered by the existing backend. Its full length is available to the clipped grid.
    json request = {
        {"query", spawnSearch_}, {"limit", (std::numeric_limits<int>::max)()}, {"rarity", kRarities[spawnRarity_].key},
        {"ownedOnly", spawnInventory_ == 1}, {"notOwnedOnly", spawnInventory_ == 2},
    };
    if (swapMode && choosingSwapReplacement_)
        request["category"] = swapTarget_.category;
    else if (spawnCat_ > 0)
        request["category"] = spawnCats_[spawnCat_].second;
    backend_.Request("items.list", request, [this, query](const json& payload, const std::string& error) {
        if (query != spawnQuery_)
            return;
        spawnLoading_ = false;
        spawnLoadError_ = error;
        spawnItems_.clear();
        if (!error.empty())
            return;
        const auto& items = payload["items"];
        spawnItems_.reserve(items.size());
        for (const auto& item : items) {
            const auto key = item.value("key", std::string());
            spawnItems_.push_back({
                item.value("itemId", 0LL), item.value("label", std::string()),
                item.value("category", std::string()), item.value("rarity", std::string()), key,
                key.empty() ? std::string() : ThumbUrl(key),
            });
        }
    });
}
void App::SpawnPanel(bool swapMode) {
    if (!spawnMetaLoaded_) {
        LoadSpawnMeta();
        RefreshSpawnList();
    }

    const auto reload = [this](double delay) {
        ++spawnQuery_;
        spawnItemsDirty_ = true;
        spawnLoading_ = true;
        spawnLoadAfter_ = ImGui::GetTime() + delay;
        itemSelection_.Clear();
        spawnScrollReset_ = true;
    };
    if (resetItemBrowser_) {
        resetItemBrowser_ = false;
        showSpawnedItems_ = false;
        choosingSwapReplacement_ = false;
        spawnSearch_.clear();
        spawnCat_ = 0;
        spawnRarity_ = 0;
        spawnInventory_ = swapMode ? 1 : 0;
        reload(0);
    }

    bool searchChanged = false;
    if (swapMode) {
        if (choosingSwapReplacement_) {
            if (Tab("Change owned item", false)) {
                choosingSwapReplacement_ = false;
                spawnInventory_ = 1;
                spawnSearch_.clear();
                spawnRarity_ = 0;
                reload(0);
            }
            ImGui::SameLine();
            Muted("Replacing " + swapTarget_.label);
        } else {
            Muted("Choose an owned item to replace.");
        }
        searchChanged = Search("##itemSearch", "Search items", spawnSearch_);
    } else {
        const float viewWidth = ImClamp(ImGui::GetContentRegionAvail().x * 0.32f, Px(120), Px(164));
        if (!showSpawnedItems_) {
            searchChanged = Search("##itemSearch", "Search items", spawnSearch_,
                                   ImGui::GetContentRegionAvail().x - viewWidth - Px(9));
        } else {
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - viewWidth - Px(9), ImGui::GetFrameHeight()));
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(viewWidth);
        int view = showSpawnedItems_ ? 1 : 0;
        constexpr const char* views[] = {"Item catalog", "Spawned items"};
        if (MenuWidgets::Combo("##spawnView", &view, views, IM_ARRAYSIZE(views))) {
            showSpawnedItems_ = view == 1;
            itemSelection_.Clear();
            spawnScrollReset_ = true;
        }
    }

    const int catalogView = swapMode ? static_cast<int>(choosingSwapReplacement_) : static_cast<int>(showSpawnedItems_);
    const MenuWidgets::ContentFade viewEntrance("##catalogEntrance", catalogView);
    if (!showSpawnedItems_) {
        bool changed = searchChanged;

        const float filterWidth = (std::min)(Px(174), (ImGui::GetContentRegionAvail().x - Px(18)) / 3.0f);
        ImGui::SetNextItemWidth(filterWidth);
        const char* category = spawnCats_.empty() ? "All categories" : spawnCats_[spawnCat_].first.c_str();
        if (swapMode && choosingSwapReplacement_) {
            const auto match = std::find_if(spawnCats_.begin(), spawnCats_.end(),
                [&](const auto& entry) { return entry.second == swapTarget_.category; });
            category = match != spawnCats_.end() ? match->first.c_str() : swapTarget_.category.c_str();
        }
        ImGui::BeginDisabled(swapMode && choosingSwapReplacement_);
        if (MenuWidgets::BeginCombo("##itemCategory", category)) {
            for (int i = 0; i < static_cast<int>(spawnCats_.size()); ++i) {
                if (MenuWidgets::Option(spawnCats_[i].first.c_str(), spawnCat_ == i)) {
                    changed |= spawnCat_ != i;
                    spawnCat_ = i;
                }
            }
            MenuWidgets::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(filterWidth);
        if (MenuWidgets::BeginCombo("##rarity", kRarities[spawnRarity_].label)) {
            for (int i = 0; i < IM_ARRAYSIZE(kRarities); ++i) {
                if (MenuWidgets::Option(kRarities[i].label, spawnRarity_ == i)) {
                    changed |= spawnRarity_ != i;
                    spawnRarity_ = i;
                }
            }
            MenuWidgets::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(filterWidth);
        ImGui::BeginDisabled(swapMode && !choosingSwapReplacement_);
        changed |= MenuWidgets::Combo("##inventory", &spawnInventory_, kInventory, IM_ARRAYSIZE(kInventory));
        ImGui::EndDisabled();
        if (changed)
            reload(0.16);
        if (spawnItemsDirty_ && ImGui::GetTime() >= spawnLoadAfter_)
            LoadSpawnItems(swapMode);
    }

    const int count = showSpawnedItems_ ? static_cast<int>(spawnedItems_.size())
                                        : static_cast<int>(spawnItems_.size());
    const char* section = swapMode ? (choosingSwapReplacement_ ? "Replacements" : "Owned items")
                                   : (showSpawnedItems_ ? "Spawned items" : "Item catalog");
    const bool showCount = showSpawnedItems_ || (!spawnLoading_ && spawnLoadError_.empty());
    MenuTheme::Section(section, showCount ? std::to_string(count) + (count == 1 ? " item" : " items") : std::string());
    BeginResults(showSpawnedItems_ ? "##spawned" : "##catalog", spawnScrollReset_);
    if (!showSpawnedItems_ && spawnLoading_) {
        Muted("Loading items...");
    } else if (!showSpawnedItems_ && !spawnLoadError_.empty()) {
        Muted(spawnLoadError_);
        if (MenuWidgets::Button("Retry")) {
            spawnLoading_ = true;
            LoadSpawnItems(swapMode);
        }
    } else if (count == 0) {
        Muted(showSpawnedItems_ ? "No spawned items" : "No matching items");
    } else {
        DrawGrid(count, 176, 4, showSpawnedItems_ ? 44 : 133, [&](int index, ImVec2 size) {
            const bool selected = itemSelection_.rows.contains(index);
            const Card card = BeginCard(size, selected);
            if (showSpawnedItems_) {
                const auto& label = spawnedItems_[index].label;
                CardLine(card, 0, 44, Font::System(13, FontWeight::Semibold), Rgba::Hex(0xF4F3F2), label);
                ImGui::SetItemTooltip("%s", label.c_str());
            } else {
                const ItemRow& item = spawnItems_[index];
                const ImVec2 imageBottom(card.bounds.Max.x, card.bounds.Min.y + Px(87));
                card.draw->AddRectFilled(card.bounds.Min, imageBottom, ImGui::GetColorU32(IM_COL32(0, 0, 0, 255)), Px(4),
                                         ImDrawFlags_RoundCornersTop);
                if (!item.thumbnail.empty() && ImGui::IsItemVisible()) {
                    if (const ImTextureID texture = images_.Get(item.thumbnail)) {
                        const ImVec2 origin = ImVec2(card.bounds.GetCenter().x - Px(36), card.bounds.Min.y + Px(7));
                        card.draw->AddImage(ImTextureRef(texture), origin, origin + Px(ImVec2(72, 72)),
                                            ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(IM_COL32_WHITE));
                    }
                }
                const auto category = std::find_if(spawnCats_.begin(), spawnCats_.end(),
                    [&](const auto& entry) { return entry.second == item.category; });
                const std::string& categoryLabel = category != spawnCats_.end() ? category->first : item.category;
                const auto rarity = std::find_if(std::begin(kRarities) + 1, std::end(kRarities),
                    [&](const Choice& choice) { return item.rarity == choice.key; });
                const std::string rarityLabel = rarity != std::end(kRarities) ? rarity->label : item.rarity;
                CardLine(card, 90, 21, Font::System(13, FontWeight::Semibold), Rgba::Hex(0xF4F3F2), item.label);
                CardLine(card, 110, 19, Font::System(11), Rgba::Hex(0xA7A3A0), categoryLabel + " \xC2\xB7 " + rarityLabel);
                ImGui::SetItemTooltip("%s\n%s \xC2\xB7 %s\nItem ID %lld", item.label.c_str(), categoryLabel.c_str(),
                                      rarityLabel.c_str(), item.itemId);
            }
            EndCard(card, selected);
            if (card.clicked)
                itemSelection_.Click(index, !swapMode && ImGui::GetIO().KeyShift,
                                     !swapMode && ImGui::GetIO().KeyCtrl);
        });
    }
    MenuWidgets::EndPane();

    ImGui::Separator();
    const auto& selected = itemSelection_.rows;
    std::string_view single;
    if (selected.size() == 1)
        single = showSpawnedItems_ ? spawnedItems_[*selected.begin()].label : spawnItems_[*selected.begin()].label;
    const bool hasToast = !toast_.empty() && ImGui::GetTime() < toastUntil_;
    const bool disabled = selected.empty() || itemActionPending_ || (!showSpawnedItems_ && spawnLoading_);
    if (swapMode) {
        const auto* choice = selected.empty() ? nullptr : &spawnItems_[*selected.begin()];
        const float width = choosingSwapReplacement_ ? 96 : 164;
        const bool sameItem = choosingSwapReplacement_ && choice && choice->key == swapTarget_.key;
        FooterText(hasToast ? toast_ : sameItem ? "Choose a different replacement" : SelectionLabel(selected, single),
                   Px(width), hasToast && !toastOk_);
        ImGui::SetCursorPosX(FooterStart(width));
        if (FooterButton(choosingSwapReplacement_ ? "Apply swap" : "Choose replacement",
                         disabled || sameItem || (choice && choice->key.empty()), width)) {
            if (choosingSwapReplacement_) {
                itemActionPending_ = true;
                backend_.Request("items.swap", {{"targetKey", swapTarget_.key}, {"sourceKey", choice->key}},
                                 [this](const json&, const std::string& error) {
                                     itemActionPending_ = false;
                                     Toast(error.empty() ? "Item swap applied" : error, error.empty());
                                 });
            } else {
                swapTarget_ = *choice;
                choosingSwapReplacement_ = true;
                spawnSearch_.clear();
                spawnRarity_ = 0;
                spawnInventory_ = 0;
                reload(0);
            }
        }
        return;
    }
    const float actionWidth = showSpawnedItems_ ? 84 : 229;
    FooterText(hasToast ? toast_ : SelectionLabel(selected, single), Px(actionWidth), hasToast && !toastOk_);
    ImGui::SetCursorPosX(FooterStart(actionWidth));
    if (!showSpawnedItems_) {
        ImGui::SetNextItemWidth(Px(136));
        MenuWidgets::Combo("##paint", &spawnPaint_, kPaints, IM_ARRAYSIZE(kPaints));
        ImGui::SameLine();
    }
    if (FooterButton(showSpawnedItems_ ? "Remove" : "Spawn", disabled)) {
        itemActionPending_ = true;
        const auto finish = [this](const json&, const std::string& error) {
            itemActionPending_ = false;
            Toast(error.empty() ? "Spawn list updated" : error, error.empty());
            RefreshSpawnList();
        };
        if (showSpawnedItems_) {
            std::vector<std::string> ids;
            for (const int index : selected)
                ids.push_back(spawnedItems_[index].instanceId);
            backend_.Request("spawn.removeMany", {{"instanceIds", ids}}, finish);
        } else {
            json items = json::array();
            for (const int index : selected)
                items.push_back({{"productId", spawnItems_[index].itemId}, {"paint", spawnPaint_}});
            backend_.Request("spawn.addMany", {{"items", std::move(items)}}, finish);
        }
    }
}

void App::TitlesPanel() {
    if (!titlesRequested_) {
        LoadTitles();
        RefreshSpawnList();
    }

    const auto tab = [&](const char* label, int index) {
        if (Tab(label, titleTab_ == index)) {
            titleTab_ = index;
            titleSelection_.Clear();
            titleScrollReset_ = true;
        }
    };
    tab("Titles", 0);
    ImGui::SameLine();
    tab("Custom titles", 1);
    ImGui::SameLine();
    tab("Active titles", 2);
    const MenuWidgets::ContentFade tabEntrance("##titlesEntrance", titleTab_);
    if (titleTab_ == 1) {
        MenuTheme::Section("Custom titles");
        CustomTitlesPanel();
        return;
    }

    bool changed = false;
    const bool showActive = titleTab_ == 2;
    if (showActive && !customTitlesRequested_)
        LoadCustomTitles();
    if (!showActive) {
        const float categoryWidth = (std::min)(Px(185), ImGui::GetContentRegionAvail().x * 0.44f);
        changed |= Search("##titleSearch", "Search titles", titleSearch_,
                          ImGui::GetContentRegionAvail().x - categoryWidth - Px(9));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(categoryWidth);
        const auto category = titleCategory_ == 0 ? "All categories" : CategoryLabel(titleCategories_[titleCategory_ - 1]);
        if (MenuWidgets::BeginCombo("##titleCategory", category.c_str())) {
            if (MenuWidgets::Option("All categories", titleCategory_ == 0)) {
                changed |= titleCategory_ != 0;
                titleCategory_ = 0;
            }
            for (int i = 0; i < static_cast<int>(titleCategories_.size()); ++i) {
                if (MenuWidgets::Option(CategoryLabel(titleCategories_[i]).c_str(), titleCategory_ == i + 1)) {
                    changed |= titleCategory_ != i + 1;
                    titleCategory_ = i + 1;
                }
            }
            MenuWidgets::EndCombo();
        }
        changed |= MenuWidgets::Checkbox("Hide active titles", &hideActiveTitles_);
    }
    if (changed) {
        titleSelection_.Clear();
        titleScrollReset_ = true;
    }

    // Keep catalog and active views on the same card layout, using instance IDs for removals.
    struct Entry {
        std::string_view text;
        std::string_view id;
        Rgba color;
        bool active;
    };
    std::vector<Entry> filtered;
    if (showActive) {
        filtered.reserve(spawnedTitles_.size());
        for (const auto& active : spawnedTitles_) {
            const auto saved = customTitles_.find("titles");
            if (saved != customTitles_.end() && saved->is_array()) {
                const auto custom = std::find_if(saved->begin(), saved->end(), [&](const json& title) {
                    return title.is_object() && title.value("id", std::string()) == active.titleId;
                });
                if (custom != saved->end()) {
                    const auto name = custom->find("text");
                    const std::string_view label = name != custom->end() && name->is_string()
                        ? name->get_ref<const std::string&>() : active.titleId;
                    const unsigned color = RgbHex(custom->value("color", std::string()), 0xFFFFFF);
                    filtered.push_back({label, active.instanceId, Rgba::Hex(color), false});
                    continue;
                }
            }
            const auto row = std::find_if(titleRows_.begin(), titleRows_.end(),
                                          [&](const TitleRow& title) { return active.titleId == title.id; });
            if (row != titleRows_.end())
                filtered.push_back({row->text, active.instanceId, Rgba::Hex(row->rgb), false});
            else
                filtered.push_back({active.titleId, active.instanceId, Rgba::Hex(0xF4F3F2), false});
        }
    } else {
        const auto query = Lower(titleSearch_);
        filtered.reserve(titleRows_.size());
        for (const auto& title : titleRows_) {
            if (titleCategory_ > 0 && title.category != titleCategories_[titleCategory_ - 1])
                continue;
            if (!query.empty() && title.searchText.find(query) == std::string::npos)
                continue;
            const bool active = activeTitleIds_.contains(title.id);
            if (!hideActiveTitles_ || !active)
                filtered.push_back({title.text, title.id, Rgba::Hex(title.rgb), active});
        }
    }

    MenuTheme::Section(showActive ? "Active titles" : "Title catalog",
                       (titlesLoaded_ || showActive)
                           ? std::to_string(filtered.size()) + (filtered.size() == 1 ? " title" : " titles")
                           : std::string());
    BeginResults(showActive ? "##active" : "##catalog", titleScrollReset_);
    if (!showActive && !titleLoadError_.empty()) {
        Muted(titleLoadError_);
        if (MenuWidgets::Button("Retry")) {
            titleLoadError_.clear();
            LoadTitles();
        }
    } else if (!titlesLoaded_ && !showActive) {
        Muted("Loading titles...");
    } else if (filtered.empty()) {
        Muted(showActive ? "No active titles" : "No matching titles");
    } else {
        DrawGrid(static_cast<int>(filtered.size()), 167, 3, 76, [&](int index, ImVec2 size) {
            const Entry& entry = filtered[index];
            ImGui::BeginDisabled(entry.active);
            if (DrawTitleCard(entry.text, entry.color.Opacity(entry.active ? 0.5f : 1.0f),
                              size, titleSelection_.rows.contains(index)))
                titleSelection_.Click(index, ImGui::GetIO().KeyShift, ImGui::GetIO().KeyCtrl);
            ImGui::EndDisabled();
            if (entry.active)
                ImGui::SetItemTooltip("Already active");
        });
    }
    MenuWidgets::EndPane();

    ImGui::Separator();
    const auto& selected = titleSelection_.rows;
    const auto single = selected.size() == 1 ? filtered[*selected.begin()].text : std::string_view();
    const bool hasToast = !toast_.empty() && ImGui::GetTime() < toastUntil_;
    const bool canSpawn = std::any_of(selected.begin(), selected.end(), [&](int index) {
        return !filtered[index].active;
    });
    FooterText(hasToast ? toast_ : SelectionLabel(selected, single), Px(84), hasToast && !toastOk_);
    ImGui::SetCursorPosX(FooterStart(84));
    if (FooterButton(showActive ? "Remove" : "Spawn", !canSpawn || titleActionsPending_ > 0)) {
        const auto finish = [this](const json&, const std::string& error) {
            if (!error.empty())
                Toast(error, false);
            if (--titleActionsPending_ == 0)
                RefreshSpawnList();
        };
        if (showActive) {
            std::vector<std::string> ids;
            for (const int index : selected)
                ids.emplace_back(filtered[index].id);
            titleActionsPending_ = 1;
            backend_.Request("spawn.removeMany", {{"instanceIds", ids}}, finish);
        } else {
            for (const int index : selected) {
                if (!filtered[index].active) {
                    ++titleActionsPending_;
                    backend_.Request("spawn.addTitle", {{"titleId", filtered[index].id}}, finish);
                }
            }
        }
    }
}
namespace {
    std::string TitleHex(const float color[3]) {
        const auto rgb = ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], 1));
        char hex[7];
        std::snprintf(hex, sizeof(hex), "%02X%02X%02X", (rgb >> IM_COL32_R_SHIFT) & 255,
                      (rgb >> IM_COL32_G_SHIFT) & 255, (rgb >> IM_COL32_B_SHIFT) & 255);
        return hex;
    }

    ImVec4 ReadTitleHex(const std::string& source, const ImVec4& fallback) {
        const std::string value = source.starts_with('#') ? source.substr(1) : source;
        if (value.size() != 6 || value.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            return fallback;
        const auto rgb = RgbHex(value, 0xFFFFFF);
        return ImVec4(float((rgb >> 16) & 255) / 255, float((rgb >> 8) & 255) / 255, float(rgb & 255) / 255, 1);
    }
}

void App::LoadCustomTitles() {
    customTitlesRequested_ = true;
    backend_.Request("titles.get", {}, [this](const json& result, const std::string& error) {
        if (error.empty())
            customTitles_ = result;
        else
            Toast(error, false);
    });
}

void App::CustomTitlesPanel() {
    if (!customTitlesRequested_)
        LoadCustomTitles();

    if (!customTitles_.is_object()) {
        if (!toast_.empty() && ImGui::GetTime() < toastUntil_)
            Muted(toast_);
        if (MenuWidgets::Button("Reload"))
            LoadCustomTitles();
        return;
    }
    auto onResult = [this](const json&, const std::string& error) {
        customTitlePending_ = false;
        if (error.empty())
            LoadCustomTitles();
        else
            Toast(error, false);
    };
    const auto titles = customTitles_.value("titles", json::array());
    bool enabled = customTitles_.value("enabled", true);
    ImGui::BeginDisabled(customTitlePending_);
    if (MenuWidgets::Checkbox("Enable custom titles", &enabled)) {
        customTitlePending_ = true;
        backend_.Request("titles.setEnabled", {{"value", enabled}}, onResult);
    }
    ImGui::TextWrapped("Launch without Easy Anti-Cheat to add saved titles to Rocket League's title picker.");
    ImGui::SetNextItemWidth(-1.0f);
    MenuWidgets::Field("##customTitle", "Title text", customTitleText_);
    const float colorWidth = ImMin(Px(250), ImGui::GetContentRegionAvail().x * 0.51f);
    ImGui::SetNextItemWidth(colorWidth);
    MenuWidgets::Color("Text color", customTitleColor_);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Text color");
    MenuWidgets::Checkbox("Glow", &customTitleHasGlow_);
    if (customTitleHasGlow_) {
        ImGui::SetNextItemWidth(colorWidth);
        MenuWidgets::Color("Glow color", customTitleGlow_);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Glow color");
    }

    const int limit = customTitles_.value("max", 0);
    const bool canSave = !customTitleEdit_.empty()
                         || (customTitles_.value("canAdd", true) && (limit <= 0 || titles.size() < size_t(limit)));
    ImGui::BeginDisabled(!canSave || customTitleText_.find_first_not_of(" \t\r\n") == std::string::npos);
    if (FooterButton(customTitleEdit_.empty() ? "Add title" : "Save title", false, 96)) {
        customTitlePending_ = true;
        backend_.Request("titles.save",
                         {{"titleId", customTitleEdit_}, {"text", customTitleText_},
                          {"color", TitleHex(customTitleColor_)},
                          {"glow", customTitleHasGlow_ ? TitleHex(customTitleGlow_) : std::string()}},
                         [this, onResult](const json& result, const std::string& error) {
                             if (error.empty()) {
                                 customTitleEdit_.clear();
                                 customTitleText_.clear();
                             }
                             onResult(result, error);
                         });
    }
    ImGui::EndDisabled();
    if (!customTitleEdit_.empty()) {
        ImGui::SameLine();
        if (MenuWidgets::Button("Cancel edit")) {
            customTitleEdit_.clear();
            customTitleText_.clear();
        }
    }
    if (!canSave) {
        ImGui::SameLine();
        Muted("Custom title limit reached");
    }
    ImGui::EndDisabled();

    MenuTheme::Section("Saved titles", std::to_string(titles.size()));
    MenuWidgets::BeginPane("##customTitleList", ImVec2(0, -Px(31)));
    constexpr float spawnWidth = 78, editWidth = 48, removeWidth = 68;
    const float actionWidth = Px(spawnWidth + editWidth + removeWidth) + ImGui::GetStyle().ItemSpacing.x * 3;
    const Font font = Font::System(13);
    if (titles.empty())
        Muted("No custom titles");
    for (const auto& title : titles) {
        const std::string id = title.value("id", std::string());
        const std::string label = title.value("text", id);
        const std::string glow = title.value("glow", std::string());
        const ImVec4 color = ReadTitleHex(title.value("color", std::string()), ImVec4(1, 1, 1, 1));
        ImGui::PushID(id.c_str());
        const float labelWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - actionWidth);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const ImVec2 labelSize(labelWidth, ImGui::GetFrameHeight());
        Typography::Draw(ImGui::GetWindowDrawList(), font, ImRect(position, position + labelSize),
                         Rgba(color.x, color.y, color.z),
                         Typography::Truncate(font, label, labelWidth));
        ImGui::Dummy(labelSize);
        if (Typography::Measure(font, label).x > labelWidth)
            ImGui::SetItemTooltip("%s", label.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(customTitlePending_ || id.empty());
        const bool active = activeTitleIds_.contains(id);
        if (FooterButton(active ? "Spawned" : "Spawn", active || !enabled || titleActionsPending_ > 0, spawnWidth)) {
            ++titleActionsPending_;
            backend_.Request("spawn.addTitle", {{"titleId", id}}, [this, id](const json&, const std::string& error) {
                --titleActionsPending_;
                if (error.empty())
                    activeTitleIds_.insert(id);
                Toast(error.empty() ? "Title added to spawn list" : error, error.empty());
                RefreshSpawnList();
            });
        }
        if (!enabled && !active)
            ImGui::SetItemTooltip("Enable custom titles to spawn this title.");
        ImGui::SameLine();
        if (MenuWidgets::Button("Edit", ImVec2(Px(editWidth), 0))) {
            customTitleEdit_ = id;
            customTitleText_ = label;
            customTitleColor_[0] = color.x;
            customTitleColor_[1] = color.y;
            customTitleColor_[2] = color.z;
            const ImVec4 glowColor = ReadTitleHex(glow, color);
            customTitleGlow_[0] = glowColor.x;
            customTitleGlow_[1] = glowColor.y;
            customTitleGlow_[2] = glowColor.z;
            customTitleHasGlow_ = !glow.empty();
        }
        ImGui::SameLine();
        if (MenuWidgets::Button("Remove", ImVec2(Px(removeWidth), 0))) {
            customTitlePending_ = true;
            if (customTitleEdit_ == id) {
                customTitleEdit_.clear();
                customTitleText_.clear();
            }
            backend_.Request("titles.remove", {{"id", id}}, onResult);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    MenuWidgets::EndPane();
    if (!toast_.empty() && ImGui::GetTime() < toastUntil_)
        Muted(toast_);
}
