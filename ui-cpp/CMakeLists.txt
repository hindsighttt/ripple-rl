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

# The Ripple RL frontend executable.
file(GLOB_RECURSE RIPPLE_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(ripple_rl WIN32 ${RIPPLE_SOURCES} assets/MenuAssets.rc)
target_include_directories(ripple_rl PRIVATE src vendor/json vendor/stb)
target_link_libraries(ripple_rl PRIVATE CupertinoUi d3d11 dxgi ws2_32 dwmapi winhttp ripple_core)
if (MSVC)
    target_compile_options(ripple_rl PRIVATE /utf-8 /permissive- /Zc:__cplusplus)
endif()
