#pragma once

#include "imgui.h"

#include <string>
#include <string_view>

namespace MenuWidgets {
    class ContentFade {
    public:
        explicit ContentFade(const char* id, int selection = 0);
        ~ContentFade();

        ContentFade(const ContentFade&) = delete;
        ContentFade& operator=(const ContentFade&) = delete;
    };

    float Ease(ImGuiID id, const char* channel, float target, float initial = 0.0f, float speed = 17.0f);
    ImU32 Mix(ImU32 from, ImU32 to, float amount);
    void Label(ImDrawList* draw, ImVec2 min, ImVec2 max, std::string_view text, float alignment, ImU32 color);

    bool Button(const char* label, ImVec2 size = {}, bool primary = false);
    bool Tab(const char* label, bool selected);
    bool Checkbox(const char* label, bool* value);
    bool Field(const char* id, const char* hint, std::string& value, bool search = false, ImGuiInputTextFlags flags = 0);
    bool BeginCombo(const char* id, const char* preview, int visibleRows = 5);
    void EndCombo();
    bool Option(const char* label, bool current);
    bool Combo(const char* id, int* current, const char* const* entries, int count);
    bool Slider(const char* id, int* value, int min, int max, const char* format = "%d", const char* label = nullptr);
    bool Slider(const char* id, float* value, float min, float max, const char* format = "%.1f", const char* label = nullptr);
    bool Color(const char* id, float rgb[3]);

    bool BeginPane(const char* id, ImVec2 size, ImGuiChildFlags childFlags = 0, ImGuiWindowFlags flags = 0);
    void EndPane();
}
