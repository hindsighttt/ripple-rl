cmake_minimum_required(VERSION 3.25)
project(RippleRL LANGUAGES CXX RC)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# The native backend, linked straight in so ripple_rl.exe hosts it in-process
# (BackendHost) — a single executable, no separate backend process.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../backend-cpp ${CMAKE_BINARY_DIR}/backend-cpp)

# Dear ImGui (features/shadows branch) vendored under vendor/imgui.
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32 IMGUI_DEFINE_MATH_OPERATORS)
target_compile_features(imgui PUBLIC cxx_std_20)
if (MSVC)
    target_compile_options(imgui PUBLIC /utf-8 /permissive- /Zc:__cplusplus)
endif()

# The CupertinoUi widget library (copied from the user's imgui-apple source).
file(GLOB_RECURSE CUPERTINO_SOURCES CONFIGURE_DEPENDS vendor/CupertinoUi/*.cpp vendor/CupertinoUi/*.h)
add_library(CupertinoUi STATIC ${CUPERTINO_SOURCES})
target_include_directories(CupertinoUi PUBLIC vendor/CupertinoUi PRIVATE vendor/stb)
target_link_libraries(CupertinoUi PUBLIC imgui)

option(RIPPLE_RL_USE_LOCAL_BAKED_FONTS "Use the local CupertinoUi font bundle when present" ON)
set(LOCAL_BAKED_FONTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/CupertinoUi/core/generated/BakedFonts.inc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LOCAL_BAKED_FONTS}")
if(NOT RIPPLE_RL_USE_LOCAL_BAKED_FONTS OR NOT EXISTS "${LOCAL_BAKED_FONTS}")
    set(INSTRUMENT_FONT "${CMAKE_CURRENT_SOURCE_DIR}/assets/InstrumentSans-Medium.ttf")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${INSTRUMENT_FONT}")
    file(SIZE "${INSTRUMENT_FONT}" FONT_SIZE)
    file(READ "${INSTRUMENT_FONT}" FONT_BYTES HEX)
    math(EXPR FONT_PADDING "(4 - ${FONT_SIZE} % 4) % 4")
    string(REPEAT "00" ${FONT_PADDING} FONT_PADDING_BYTES)
    string(REGEX REPLACE "(..)(..)(..)(..)" "    0x\\4\\3\\2\\1,\n"
           FONT_WORDS "${FONT_BYTES}${FONT_PADDING_BYTES}")
    set(FONT_TABLE "inline constexpr unsigned int InstrumentSans[] = {\n${FONT_WORDS}};\n\n")
    string(APPEND FONT_TABLE "inline constexpr Cupertino::BakedFile BakedFontFiles[] = {\n")
    # These names are CupertinoUi's lookup keys. Every slot uses Instrument Sans.
    foreach(FAMILY IN ITEMS Display Text)
        foreach(WEIGHT IN ITEMS Bold Heavy Medium Regular Semibold)
            string(APPEND FONT_TABLE "    {\"SF-Pro-${FAMILY}-${WEIGHT}.otf\", InstrumentSans, ${FONT_SIZE}},\n")
        endforeach()
    endforeach()
    string(APPEND FONT_TABLE "};\n")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/RippleFonts.inc" CONTENT "${FONT_TABLE}")
    target_include_directories(CupertinoUi PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(CupertinoUi PRIVATE RIPPLE_RL_USE_INSTRUMENT_SANS)
endif()

# The Ripple RL frontend executable.
file(GLOB_RECURSE RIPPLE_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(ripple_rl WIN32 ${RIPPLE_SOURCES} assets/MenuAssets.rc)
target_include_directories(ripple_rl PRIVATE src vendor/json vendor/stb)
target_link_libraries(ripple_rl PRIVATE CupertinoUi d3d11 dxgi ws2_32 dwmapi winhttp ripple_core)
if (MSVC)
    target_compile_options(ripple_rl PRIVATE /utf-8 /permissive- /Zc:__cplusplus)
endif()
