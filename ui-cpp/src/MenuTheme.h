#pragma once

#include "imgui.h"

#include <string_view>

namespace MenuTheme {
    inline constexpr ImU32 Background = IM_COL32(17, 17, 19, 255);
    inline constexpr ImU32 Rail = IM_COL32(20, 20, 22, 255);
    inline constexpr ImU32 Surface = IM_COL32(24, 24, 27, 255);
    inline constexpr ImU32 Control = IM_COL32(29, 29, 33, 255);
    inline constexpr ImU32 Hover = IM_COL32(39, 39, 44, 255);
    inline constexpr ImU32 Selected = IM_COL32(31, 49, 65, 255);
    inline constexpr ImU32 Border = IM_COL32(40, 40, 44, 255);
    inline constexpr ImU32 Text = IM_COL32(237, 233, 237, 255);
    inline constexpr ImU32 Muted = IM_COL32(158, 151, 158, 255);
    inline constexpr ImU32 Accent = IM_COL32(135, 205, 255, 255);
    inline constexpr ImU32 Action = IM_COL32(55, 113, 166, 255);
    inline constexpr ImU32 ActionHover = IM_COL32(86, 157, 211, 255);
    inline constexpr float HeaderHeight = 43;

    void Apply(float scale);
    void Section(std::string_view title, std::string_view detail = {});
}
