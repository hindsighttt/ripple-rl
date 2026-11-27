cmake_minimum_required(VERSION 3.25)
project(RippleBackend LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ripple_core: the native backend, buildable as a static library so it links both
# into the standalone ripple_backend.exe and straight into ripple_rl.exe (the
# single-exe build, via BackendHost).
add_library(ripple_core STATIC
    src/Store.cpp
    src/WinUtil.cpp
    src/ProxyManager.cpp
    src/IpcServer.cpp
    src/Features.cpp
    src/Catalog.cpp
    src/Aes.cpp
    src/Hash.cpp
    src/Zlib.cpp
    src/Upk.cpp
    src/Bootstrap.cpp
    src/Swaps.cpp
    src/Garage.cpp
    src/PackagePatches.cpp
    src/Home.cpp
    src/Stats.cpp
    src/Fps.cpp
    src/Overlay.cpp
    src/Zip.cpp
    src/Workshop.cpp
    src/Packs.cpp
    src/Spawn.cpp
    src/Presets.cpp
    src/Quickchat.cpp
    src/BackendHost.cpp
    src/Dispatcher.cpp)

# Include dirs are PUBLIC (ripple_core's own CLI tools include its JSON-using
# headers; the UI shares these same vendored dirs so there is no conflict). The
# Windows-only defines/options stay PRIVATE so they don't leak into the UI.
target_include_directories(ripple_core PUBLIC
    src
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui-cpp/vendor/json
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui-cpp/vendor/stb)

target_compile_features(ripple_core PUBLIC cxx_std_20)
target_compile_definitions(ripple_core PRIVATE
    _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
if (MSVC)
    target_compile_options(ripple_core PRIVATE /utf-8 /permissive- /Zc:__cplusplus)
endif()
target_link_libraries(ripple_core PUBLIC ws2_32 advapi32 bcrypt shell32 user32 gdi32 wininet crypt32)

# CLI/standalone targets are only built when this is the top-level project, so
# add_subdirectory() from ui-cpp builds just the ripple_core library.
if (PROJECT_IS_TOP_LEVEL)
    # Standalone backend process for development and verification.
    add_executable(ripple_backend src/main.cpp)
    target_link_libraries(ripple_backend PRIVATE ripple_core)

    # Verification CLI: proves upk::Convert == upk.py convert() byte-for-byte.
    add_executable(ripple_upktool src/upk_tool.cpp)
    target_link_libraries(ripple_upktool PRIVATE ripple_core)

    # Verification CLI: proves the ProjectX bootstrap matches bootstrap.py.
    add_executable(ripple_bootstraptool src/bootstrap_tool.cpp)
    target_link_libraries(ripple_bootstraptool PRIVATE ripple_core)
endif()
