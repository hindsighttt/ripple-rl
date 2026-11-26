#include "MenuWidgets.h"
#include "MenuTheme.h"

#include "Cupertino.h"
#include "misc/cpp/imgui_stdlib.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace MenuWidgets {
    using Cupertino::Px;

    void Label(ImDrawList* draw, ImVec2 min, ImVec2 max, std::string_view text, float alignment, ImU32 color) {
        if (text.empty() || max.x <= min.x)
            return;
        draw->PushClipRect(min, max, true);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::RenderTextClippedEx(draw, min, max, text.data(), ImGui::FindRenderedTextEnd(text.data(), text.data() + text.size()),
                                   nullptr, ImVec2(alignment, 0.5f));
        ImGui::PopStyleColor();
        draw->PopClipRect();
    }

    namespace {
        ImRect ItemBounds() {
            return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }

        bool CanDrawItem() {
            return !ImGui::GetCurrentWindow()->SkipItems && ImGui::IsItemVisible();
        }

        float Hover(ImGuiID id) {
            return Ease(id, "hover", ImGui::IsItemHovered() || ImGui::IsItemFocused() ? 1.0f : 0.0f);
        }

        bool DrawButton(const char* label, ImVec2 size, bool primary, bool tab, bool selected) {
            const float labelWidth = ImGui::CalcTextSize(label, nullptr, true).x;
            const ImVec2 padding = ImGui::GetStyle().FramePadding;
            if (size.x == 0)
                size.x = labelWidth + 2 * padding.x;
            if (size.y == 0)
                size.y = ImGui::GetFrameHeight();
            const auto id = ImGui::GetID(label);
            const bool pressed = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
            if (!CanDrawItem())
                return pressed;

            const ImRect bounds = ItemBounds();
            const float hover = Hover(id);
            const float active = Ease(id, "active", selected ? 1.0f : 0.0f, selected ? 1.0f : 0.0f);
            auto* draw = ImGui::GetWindowDrawList();
            auto* storage = ImGui::GetStateStorage();
            const auto pressedAt = ImHashStr("buttonPress", 0, id);
            if (pressed)
                storage->SetFloat(pressedAt, static_cast<float>(ImGui::GetTime()));
            const float time = static_cast<float>(ImGui::GetTime()) - storage->GetFloat(pressedAt, -10.0f);
            const float flash = ImSaturate(1.0f - time * 4.5f);
            const ImU32 normal = primary ? MenuTheme::Selected : Mix(MenuTheme::Surface, MenuTheme::Control, 0.27f);
            ImU32 shade = Mix(normal, MenuTheme::Hover, hover * 0.68f);
            shade = Mix(shade, MenuTheme::Selected, active * 0.55f);
            const float press = ImMax(flash * flash, ImGui::IsItemActive() ? 0.58f : 0.0f);
            shade = Mix(shade, MenuTheme::Accent, press * 0.72f);
            draw->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(shade), Px(3));
            if (tab && active > 0.01f) {
                const float center = bounds.GetCenter().x;
                const float half = (bounds.GetWidth() * 0.5f - Px(7)) * active;
                draw->AddLine(ImVec2(center - half, bounds.Max.y - Px(2)),
                              ImVec2(center + half, bounds.Max.y - Px(2)), ImGui::GetColorU32(MenuTheme::Accent), Px(1.25f));
            }
            const ImU32 ink = primary ? Mix(MenuTheme::Text, MenuTheme::Accent, 0.42f) : MenuTheme::Text;
            Label(draw, bounds.Min + ImVec2(Px(4), 0), bounds.Max - ImVec2(Px(4), 0), label, 0.5f, ink);
            if (ImGui::IsItemFocused() && GImGui->NavCursorVisible)
                draw->AddRect(bounds.Min, bounds.Max, ImGui::GetColorU32(MenuTheme::Accent), Px(3));
            return pressed;
        }

        // Use ImGui's slider interaction and numeric entry. Only its stock drawing is hidden.
        bool TrackInput(const char* id, ImGuiDataType type, void* value, const void* min, const void* max) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, 0);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, 0);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, 0);
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, Px(12));
            const bool changed = ImGui::SliderScalar(id, type, value, min, max, "",
                                                     ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(5);
            return changed;
        }

        ImRect TrackBounds() {
            const ImRect item = ItemBounds();
            const float center = item.GetCenter().y;
            const float margin = ImMin(Px(8), item.GetWidth() * 0.25f);
            return ImRect(item.Min.x + margin, center - Px(2.5f), item.Max.x - margin, center + Px(2.5f));
        }

        void Knob(const ImRect& track, ImGuiID id, float fraction) {
            auto* draw = ImGui::GetWindowDrawList();
            const float position = ImLerp(track.Min.x, track.Max.x, ImSaturate(fraction));
            const float hover = Hover(id);
            const ImVec2 center(position, track.GetCenter().y);
            draw->AddCircleFilled(center, Px(5.4f + hover * 0.6f), ImGui::GetColorU32(MenuTheme::Text));
        }

        template <typename T>
        bool NumericSlider(const char* id, ImGuiDataType type, T* value, T min, T max, const char* format, const char* label) {
            const float width = ImGui::CalcItemWidth();
            const auto key = ImGui::GetID(id);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float header = ImGui::GetTextLineHeight() + Px(4);
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(width, header));
            ImGui::SetNextItemWidth(width);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Px(ImVec2(4, 2)));
            const bool changed = TrackInput(id, type, value, &min, &max);
            ImGui::PopStyleVar();
            if (CanDrawItem()) {
                const float fraction = ImSaturate(float(*value - min) / float(max - min));
                const float offset = Ease(key, "sliderOffset", fraction, fraction, 20);
                const float valueDisplay = Ease(key, "sliderValue", float(*value), float(*value), 15);
                auto* draw = ImGui::GetWindowDrawList();
                char text[32];
                if constexpr (std::is_integral_v<T>)
                    std::snprintf(text, sizeof(text), format, static_cast<T>(std::lround(valueDisplay)));
                else
                    std::snprintf(text, sizeof(text), format, valueDisplay);
                const ImVec2 right = start + ImVec2(width, header);
                if (label)
                    Label(draw, start, right - ImVec2(Px(56), 0), label, 0, MenuTheme::Muted);
                Label(draw, start, right - ImVec2(Px(2), 0), text, 1, MenuTheme::Text);
                if (!ImGui::TempInputIsActive(key)) {
                    const ImRect track = TrackBounds();
                    const ImVec2 top(track.Min.x, track.GetCenter().y - Px(3.5f));
                    const ImVec2 bottom(track.Max.x, track.GetCenter().y + Px(3.5f));
                    draw->AddRectFilled(top, bottom, ImGui::GetColorU32(MenuTheme::Control), Px(3));
                    if (offset > 0.004f) {
                        const ImVec2 fillEnd(ImLerp(top.x, bottom.x, offset), bottom.y);
                        const auto a = ImGui::GetColorU32(MenuTheme::Accent);
                        const auto b = ImGui::GetColorU32(Mix(MenuTheme::Accent, MenuTheme::Action, 0.74f));
                        draw->AddRectFilledMultiColor(top, fillEnd, a, b, b, a);
                    }
                    Knob(track, key, offset);
                }
            }
            ImGui::EndGroup();
            return changed;
        }

        ImU32 Hsv(float h, float s, float v) {
            ImVec4 color(0, 0, 0, 1);
            ImGui::ColorConvertHSVtoRGB(h, s, v, color.x, color.y, color.z);
            return ImGui::GetColorU32(color);
        }

        bool Gradient(const char* id, float* value, const ImU32* stops, int count) {
            constexpr float min = 0, max = 1;
            const auto key = ImGui::GetID(id);
            const bool changed = TrackInput(id, ImGuiDataType_Float, value, &min, &max);
            if (CanDrawItem() && !ImGui::TempInputIsActive(key)) {
                ImRect track = TrackBounds();
                track.Min.y -= Px(1);
                track.Max.y += Px(1);
                auto* draw = ImGui::GetWindowDrawList();
                for (int i = 0; i < count - 1; ++i) {
                    const ImVec2 left(ImLerp(track.Min.x, track.Max.x, float(i) / (count - 1)), track.Min.y);
                    const ImVec2 right(ImLerp(track.Min.x, track.Max.x, float(i + 1) / (count - 1)), track.Max.y);
                    draw->AddRectFilledMultiColor(left, right, stops[i], stops[i + 1], stops[i + 1], stops[i]);
                }
                Knob(track, key, *value);
            }
            return changed;
        }

        void ColorText(char (&text)[8], const float rgb[3]) {
            const auto byte = [](float value) { return static_cast<unsigned>(std::lround(ImSaturate(value) * 255)); };
            std::snprintf(text, sizeof(text), "#%02X%02X%02X", byte(rgb[0]), byte(rgb[1]), byte(rgb[2]));
        }

        bool PickColor(float& h, float& s, float& v) {
            bool changed = false;
            ImGui::TextDisabled("Saturation");
            const ImU32 saturation[] = {Hsv(h, 0, v), Hsv(h, 1, v)};
            ImGui::SetNextItemWidth(-1);
            changed |= Gradient("##saturation", &s, saturation, 2);
            ImGui::TextDisabled("Brightness");
            const ImU32 brightness[] = {Hsv(h, s, 0), Hsv(h, s, 1)};
            ImGui::SetNextItemWidth(-1);
            changed |= Gradient("##brightness", &v, brightness, 2);

            float rgb[3] {};
            ImGui::ColorConvertHSVtoRGB(h, s, v, rgb[0], rgb[1], rgb[2]);
            char hex[8];
            ColorText(hex, rgb);
            ImGui::TextDisabled("Hex");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##hex", hex, sizeof(hex), ImGuiInputTextFlags_CharsHexadecimal |
                                                          ImGuiInputTextFlags_EnterReturnsTrue)) {
                const char* digits = hex[0] == '#' ? hex + 1 : hex;
                if (std::string_view(digits).size() == 6 && std::string_view(digits).find_first_not_of("0123456789ABCDEFabcdef") == std::string_view::npos) {
                    const unsigned color = std::strtoul(digits, nullptr, 16);
                    ImGui::ColorConvertRGBtoHSV(float((color >> 16) & 255) / 255,
                                               float((color >> 8) & 255) / 255, float(color & 255) / 255, h, s, v);
                    changed = true;
                }
            }
            return changed;
        }

        void PaintScrollbar() {
            auto* window = ImGui::GetCurrentWindow();
            if (window->SkipItems)
                return;
            const auto id = ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y);
            const bool scrolling = window->ScrollbarY && window->ScrollMax.y > 0;
            const float alpha = Ease(id, "scrollFade", scrolling ? 1.0f : 0.0f, 0.0f, 11);
            if (!scrolling || alpha <= 0.01f)
                return;
            ImRect area = ImGui::GetWindowScrollbarRect(window, ImGuiAxis_Y);
            const auto& style = ImGui::GetStyle();
            const float inset = ImTrunc(ImMin(style.ScrollbarPadding, area.GetWidth() * 0.5f));
            area.Expand(-inset);
            if (area.GetHeight() <= 1)
                return;

            const float visible = window->InnerRect.GetHeight();
            const float thumb = ImTrunc(ImClamp(area.GetHeight() * visible / (visible + window->ScrollMax.y),
                                                ImMin(area.GetHeight(), style.GrabMinSize), area.GetHeight()));
            const float y = ImSaturate(window->Scroll.y / window->ScrollMax.y);
            const bool held = ImGui::GetActiveID() == id;
            const float shade = Ease(id, "scrollHeld", held ? 1.0f : 0.0f, 0.0f, 10);
            const float top = area.Min.y + (area.GetHeight() - thumb) * y;
            // Use the native scrollbar's scroll ratio and hit area for the custom track.
            const float right = area.Max.x;
            const float left = right - ImMin(area.GetWidth(), Px(6) * Ease(id, "scrollWidth", 1, 0, 20));
            auto* draw = window->DrawList;
            draw->PushClipRect(window->OuterRectClipped.Min, window->OuterRectClipped.Max, false);
            draw->AddRectFilled(ImVec2(left, area.Min.y), ImVec2(right, area.Max.y),
                                ImGui::GetColorU32(MenuTheme::Control, alpha), Px(2));
            draw->AddRectFilled(ImVec2(left, top), ImVec2(right, top + thumb),
                                ImGui::GetColorU32(Mix(MenuTheme::Accent, MenuTheme::Action, 0.30f * shade), alpha),
                                Px(2));
            draw->PopClipRect();
        }

    }

    ContentFade::ContentFade(const char* name, int selection) {
        const auto id = ImGui::GetID(name);
        const auto frameKey = ImHashStr("frame", 0, id);
        const auto selectionKey = ImHashStr("selection", 0, id);
        const auto elapsedKey = ImHashStr("elapsed", 0, id);
        auto* storage = ImGui::GetStateStorage();
        const int frame = ImGui::GetFrameCount();
        constexpr float duration = 0.26f;
        const bool changed = storage->GetInt(selectionKey, -1) != selection;
        const bool returning = storage->GetInt(frameKey, -2) != frame - 1;
        const float inheritedAlpha = ImGui::GetStyle().Alpha;

        // The outer page already fades on entry. Nested tab sections only need
        // their own entrance when the user changes tabs on a visible page.
        float elapsed;
        if (inheritedAlpha < 1.0f && (changed || returning))
            elapsed = duration;
        else if (changed || returning)
            elapsed = 0.0f;
        else
            elapsed = ImMin(duration, storage->GetFloat(elapsedKey) + ImGui::GetIO().DeltaTime);

        storage->SetInt(frameKey, frame);
        storage->SetInt(selectionKey, selection);
        storage->SetFloat(elapsedKey, elapsed);
        const float progress = elapsed / duration;
        const float smooth = progress * progress * (3.0f - 2.0f * progress);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, inheritedAlpha * ImLerp(0.12f, 1.0f, smooth));
    }

    ContentFade::~ContentFade() {
        ImGui::PopStyleVar();
    }

    float Ease(ImGuiID id, const char* channel, float target, float initial, float speed) {
        const auto key = ImHashStr(channel, 0, id);
        auto* storage = ImGui::GetStateStorage();
        const float old = storage->GetFloat(key, initial);
        const float blend = 1.0f - std::exp(-speed * ImGui::GetIO().DeltaTime);
        float value = ImLerp(old, target, blend);
        if (std::abs(value - target) < 0.002f)
            value = target;
        storage->SetFloat(key, value);
        return value;
    }

    ImU32 Mix(ImU32 from, ImU32 to, float amount) {
        return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(from),
                                                      ImGui::ColorConvertU32ToFloat4(to), amount));
    }

    bool Button(const char* label, ImVec2 size, bool primary) {
        return DrawButton(label, size, primary, false, false);
    }

    bool Tab(const char* label, bool selected) {
        return DrawButton(label, {}, false, true, selected);
    }

    bool Checkbox(const char* label, bool* value) {
        const auto id = ImGui::GetID(label);
        const float height = ImGui::GetFrameHeight();
        const ImVec2 size(ImGui::CalcTextSize(label, nullptr, true).x + Px(24), height);
        const bool clicked = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
        if (clicked)
            *value = !*value;
        if (!CanDrawItem())
            return clicked;

        const ImRect item = ItemBounds();
        const ImVec2 center(item.Max.x - Px(7), item.GetCenter().y);
        auto* draw = ImGui::GetWindowDrawList();
        const float amount = Ease(id, "checked", *value ? 1.0f : 0.0f, *value ? 1.0f : 0.0f, 14);
        const float hover = Hover(id);
        const auto bg = ImGui::GetColorU32(Mix(MenuTheme::Control, MenuTheme::Hover, hover));
        draw->AddRectFilled(center - Px(ImVec2(7, 7)), center + Px(ImVec2(7, 7)), bg, Px(2));
        const ImVec2 radius = Px(ImVec2(7, 7)) * amount;
        if (amount > 0.01f) {
            draw->AddRectFilled(center - radius, center + radius, ImGui::GetColorU32(MenuTheme::Accent, amount), Px(2));
            ImGui::RenderCheckMark(draw, center - Px(ImVec2(4, 4)), ImGui::GetColorU32(MenuTheme::Background, amount), Px(8));
        }
        Label(draw, item.Min, item.Max - ImVec2(Px(22), 0), label, 0,
                Mix(MenuTheme::Muted, MenuTheme::Text, ImMax(hover, amount)));
        return clicked;
    }

    bool Field(const char* id, const char* hint, std::string& value, bool search, ImGuiInputTextFlags flags) {
        if (!search)
            return ImGui::InputTextWithHint(id, hint, &value, flags);
        ImGui::PushID(id);
        const auto focus = ImGui::GetID("##focus");
        auto* storage = ImGui::GetStateStorage();
        const float width = ImGui::CalcItemWidth();
        const float iconWidth = ImMin(Px(32), width * 0.34f);
        const auto position = ImGui::GetCursorScreenPos();
        const ImVec2 size(width, ImGui::GetFrameHeight());
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(position, position + size, ImGui::GetColorU32(MenuTheme::Surface), Px(3));

        ImGui::BeginGroup();
        if (storage->GetBool(focus)) {
            ImGui::SetKeyboardFocusHere();
            storage->SetBool(focus, false);
        }
        ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, 0);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, 0);
        ImGui::SetNextItemWidth(ImMax(Px(5), width - iconWidth));
        bool changed = ImGui::InputTextWithHint("##text", hint, &value, flags);
        ImGui::PopStyleColor(3);
        const bool active = ImGui::IsItemActive();
        ImGui::SameLine(0, 0);
        if (ImGui::InvisibleButton("##search", ImVec2(iconWidth, size.y), ImGuiButtonFlags_EnableNav)) {
            storage->SetBool(focus, true);
            if (!value.empty()) {
                value.clear();
                changed = true;
            }
        }
        if (!value.empty())
            ImGui::SetItemTooltip("Clear search");
        const float amount = Ease(focus, "searchIcon", active || ImGui::IsItemHovered() ? 1.0f : 0.0f);
        const ImVec2 center = ItemBounds().GetCenter() - Px(ImVec2(1, 1));
        const auto color = ImGui::GetColorU32(Mix(MenuTheme::Muted, MenuTheme::Text, amount));
        if (value.empty()) {
            draw->AddCircle(center, Px(3.4f), color, 0, Px(1.15f));
            draw->AddLine(center + Px(ImVec2(2.5f, 2.5f)), center + Px(ImVec2(5.2f, 5.2f)), color, Px(1.15f));
        } else {
            draw->AddLine(center - Px(ImVec2(3, 3)), center + Px(ImVec2(3, 3)), color, Px(1.15f));
            draw->AddLine(center + Px(ImVec2(-3, 3)), center + Px(ImVec2(3, -3)), color, Px(1.15f));
        }
        ImGui::EndGroup();
        ImGui::PopID();
        return changed;
    }

    namespace {
        void HideNativeScrollbar() {
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, 0);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, 0);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, 0);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, 0);
        }
    }

    bool BeginCombo(const char* id, const char* preview, int visibleRows) {
        auto* parent = ImGui::GetCurrentWindow();
        const auto origin = ImGui::GetCursorScreenPos();
        const ImRect bounds(origin, origin + ImVec2(ImGui::CalcItemWidth(), ImGui::GetFrameHeight()));
        const auto key = ImGui::GetID(id);
        HideNativeScrollbar();
        ImGui::PushStyleColor(ImGuiCol_PopupBg, MenuTheme::Surface);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, MenuTheme::Surface);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, MenuTheme::Control);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(ImVec2(4, 2)));
        const auto animation = ImHashStr("combo", 0, key);
        const float previous = parent->StateStorage.GetFloat(animation, 0);
        const float height = ImLerp(1.8f, static_cast<float>(visibleRows), previous) * Px(32) + Px(4);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, height));
        const bool opened = ImGui::BeginCombo(id, nullptr, ImGuiComboFlags_NoArrowButton);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(7);
        if (!parent->SkipItems && parent->ClipRect.Overlaps(bounds)) {
            auto* draw = parent->DrawList;
            const float next = ImLerp(previous, opened ? 1.0f : 0.0f,
                                      1.0f - std::exp(-19 * ImGui::GetIO().DeltaTime));
            parent->StateStorage.SetFloat(animation, next);
            Label(draw, bounds.Min + ImVec2(Px(10), 0), bounds.Max - ImVec2(Px(26), 0), preview, 0, MenuTheme::Text);
            const auto color = ImGui::GetColorU32(Mix(MenuTheme::Muted, MenuTheme::Text, next));
            const ImVec2 center(bounds.Max.x - Px(13), bounds.GetCenter().y);
            const float tilt = ImLerp(Px(2.2f), -Px(2.2f), next);
            draw->AddTriangleFilled(center + ImVec2(-Px(3.3f), -tilt * 0.7f),
                                    center + ImVec2(Px(3.3f), -tilt * 0.7f), center + ImVec2(0, tilt), color);
        }
        if (opened) {
            const float fade = parent->StateStorage.GetFloat(animation, 1);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Px(ImVec2(4, 2)));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.45f + fade * 0.55f));
        }
        return opened;
    }

    void EndCombo() {
        PaintScrollbar();
        ImGui::PopStyleVar(2);
        ImGui::EndCombo();
    }

    bool Option(const char* label, bool current) {
        const auto id = ImGui::GetID(label);
        ImGui::PushStyleColor(ImGuiCol_Text, 0);
        ImGui::PushStyleColor(ImGuiCol_Header, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
        const bool selected = ImGui::Selectable(label, current, 0, ImVec2(0, Px(30)));
        ImGui::PopStyleColor(4);
        if (current)
            ImGui::SetItemDefaultFocus();
        if (CanDrawItem()) {
            ImRect item = ItemBounds();
            item.Min.x += Px(2);
            item.Max.x -= Px(2);
            auto* draw = ImGui::GetWindowDrawList();
            const float hover = Hover(id);
            if (current || hover > 0.01f)
                draw->AddRectFilled(item.Min, item.Max, ImGui::GetColorU32(
                    Mix(MenuTheme::Surface, MenuTheme::Hover, current ? 0.5f : hover)), Px(3));
            if (current)
                ImGui::RenderCheckMark(draw, ImVec2(item.Min.x + Px(4), item.GetCenter().y - Px(3.8f)),
                                       ImGui::GetColorU32(MenuTheme::Accent), Px(8));
            Label(draw, item.Min + ImVec2(Px(23), 0), item.Max - ImVec2(Px(3), 0), label, 0,
                    Mix(MenuTheme::Muted, MenuTheme::Text, current ? 1.0f : hover));
        }
        return selected;
    }

    bool Combo(const char* id, int* current, const char* const* entries, int count) {
        bool changed = false;
        if (BeginCombo(id, *current >= 0 && *current < count ? entries[*current] : "", ImClamp(count, 2, 5))) {
            for (int i = 0; i < count; ++i) {
                if (Option(entries[i], i == *current) && i != *current) {
                    *current = i;
                    changed = true;
                }
            }
            EndCombo();
        }
        return changed;
    }

    bool Slider(const char* id, int* value, int min, int max, const char* format, const char* label) {
        return NumericSlider(id, ImGuiDataType_S32, value, min, max, format, label);
    }

    bool Slider(const char* id, float* value, float min, float max, const char* format, const char* label) {
        return NumericSlider(id, ImGuiDataType_Float, value, min, max, format, label);
    }

    bool Color(const char* id, float rgb[3]) {
        ImGui::PushID(id);
        auto* storage = ImGui::GetStateStorage();
        const auto hueKey = ImGui::GetID("##hueMemory");
        const auto saturationKey = ImGui::GetID("##saturationMemory");
        float h, s, v;
        ImGui::ColorConvertRGBtoHSV(rgb[0], rgb[1], rgb[2], h, s, v);
        if (s <= 0.0001f || h <= 0.0001f) {
            const float previous = storage->GetFloat(hueKey, h);
            if (s <= 0.0001f || previous >= 0.999f)
                h = previous;
        }
        if (v <= 0.0001f)
            s = storage->GetFloat(saturationKey, s);

        const float width = ImGui::CalcItemWidth();
        const bool showHex = width >= Px(142);
        const float chip = showHex ? Px(94) : ImGui::GetFrameHeight();
        ImU32 hue[7];
        for (int i = 0; i < IM_ARRAYSIZE(hue); ++i)
            hue[i] = Hsv(float(i) / 6, 0.88f, 1);
        ImGui::SetNextItemWidth(ImMax(Px(16), width - chip - Px(8)));
        const bool hueChanged = Gradient("##hue", &h, hue, IM_ARRAYSIZE(hue));
        // Changing hue on white or black should produce a visible color on the first drag.
        if (hueChanged && s <= 0.0001f)
            s = 1.0f;
        if (hueChanged && v <= 0.0001f)
            v = 1.0f;
        float preview[3] {};
        ImGui::ColorConvertHSVtoRGB(h, s, v, preview[0], preview[1], preview[2]);
        char hex[8];
        ColorText(hex, preview);

        ImGui::SameLine(0, Px(8));
        if (Button("##swatch", ImVec2(chip, 0)))
            ImGui::OpenPopup("##picker");
        // Override the button's centered label to leave room for the swatch.
        if (CanDrawItem()) {
            const ImRect item = ItemBounds();
            auto* draw = ImGui::GetWindowDrawList();
            if (showHex) {
                draw->AddRectFilled(item.Min + Px(ImVec2(2, 2)), item.Max - Px(ImVec2(2, 2)),
                                    ImGui::GetColorU32(MenuTheme::Control), Px(4));
                Label(draw, item.Min + Px(ImVec2(25, 0)), item.Max - Px(ImVec2(4, 0)), hex, 0, MenuTheme::Text);
            }
            draw->AddRectFilled(item.Min + Px(ImVec2(5, 5)),
                                ImVec2(item.Min.x + Px(20), item.Max.y - Px(5)), Hsv(h, s, v), Px(3));
        }
        ImGui::SetItemTooltip("Edit color %s", hex);

        bool changed = hueChanged;
        ImGui::SetNextWindowSizeConstraints(Px(ImVec2(238, 0)), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(ImVec2(12, 11)));
        if (ImGui::BeginPopup("##picker")) {
            const float elapsed = Ease(ImGui::GetID("##pickerFade"), "alpha", 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.45f + 0.55f * elapsed));
            MenuTheme::Section("Color settings", hex);
            changed |= PickColor(h, s, v);
            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        storage->SetFloat(hueKey, h);
        storage->SetFloat(saturationKey, s);
        if (changed)
            ImGui::ColorConvertHSVtoRGB(h, s, v, rgb[0], rgb[1], rgb[2]);
        ImGui::PopID();
        return changed;
    }

    bool BeginPane(const char* id, ImVec2 size, ImGuiChildFlags childFlags, ImGuiWindowFlags flags) {
        HideNativeScrollbar();
        const bool visible = ImGui::BeginChild(id, size, childFlags, flags);
        ImGui::PopStyleColor(4);
        return visible;
    }

    void EndPane() {
        PaintScrollbar();
        ImGui::EndChild();
    }
}
