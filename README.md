# Ripple RL

Ripple RL is a Windows desktop companion for Rocket League. It brings Garage
customization, cosmetic item swaps, and local game-file patch management into one
app. It also includes tools for overlays, quick chat, and preset management.

## Features

- **Garage customization** lets you configure team-specific underglow,
  headlights, taillights, wheel camber, and flip-reset effects. Some effects
  require the Garage patches.
- **Catalog and item swaps** let you browse cosmetic items and manage swaps
  on your local installation. The app keeps backups for supported file changes.
- **Patch maintenance** shows the status of the Garage packages and lets you
  re-apply or revert them. Separately, the package writer and recipe tools let
  developers build and validate experimental edits to local game packages.
- **Utilities** include configurable quick-chat shortcuts, overlays for FPS and
  ball speed, and saved presets.

## Build and run on Windows

To use Ripple RL without building it, download the Windows x64 ZIP from
[GitHub Releases](https://github.com/hindsighttt/ripple-rl/releases/latest).
Extract the entire ZIP and open **Launch Ripple RL.cmd**. The portable build
includes the runtime for Garage and package patches and starts with the network
proxy disabled.

Install the Visual Studio **Desktop development with C++** workload, CMake 3.25
or newer, and Ninja. Put a local copy of the Instrument Sans Medium TrueType font
at `ui-cpp/assets/InstrumentSans-Medium.ttf`, the path used by the Windows
resource file. Then run these commands from an **x64 Native Tools Command Prompt**
at the repository root.

```cmd
cmake -S ui-cpp -B ui-cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ui-cpp/build
set RIPPLE_RL_NO_PROXY=1
ui-cpp\build\bin\ripple_rl.exe
```

`RIPPLE_RL_NO_PROXY=1` disables automatic proxy startup for that development
session. The checkout contains source and recipe data. Build outputs, binary
fixtures, and the local proxy add-on stay outside Git.
See [PACKAGING.md](PACKAGING.md) for portable build instructions.

## Garage patches

Select your Rocket League installation in **Settings**. With Rocket League
closed, open **Settings → Maintenance → Garage patch**. Use **Refresh garage
status** to check the installed packages, **Re-apply garage patch** after the
launcher verifies or replaces game files, and **Revert to vanilla** to restore
clean packages when the original backups are available.

The bundled Garage recipe supports a specific Epic Games build. The installer
checks the clean game files against known hashes and generates the modified
packages locally from those files and the recipe. This workflow is self-contained
and uses the game installation as its source. If the game updates to an unknown
build, the recipe needs to be validated against that build before it can be used.
See the [Garage patch notes](docs/ssp3-format.md#restoring-the-epic-garage-patches-after-file-verification)
for validation and recovery details.

Settings, presets, and backups are stored under `%APPDATA%\RippleRL`.

## Project layout

| Path | Purpose |
| --- | --- |
| [`ui-cpp/`](ui-cpp/) | Native Win32, Direct3D 11, and Dear ImGui desktop interface. |
| [`backend-cpp/`](backend-cpp/) | C++ backend for settings, file operations, commands, and game integration. The desktop app hosts it in-process and talks to it over loopback IPC. |
| [`ripple/`](ripple/) | Offline package writer, recipe tools, and modules for the optional local proxy. |
| [`data/`](data/) | Catalogs, feature defaults, and version-specific patch recipes. |
| [`tools/`](tools/) and [`tests/`](tests/) | Build and verification utilities and Python regression tests. |
| [`docs/`](docs/) | Notes on the Garage package and save formats and feature implementation. |

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, testing, and pull
requests. Ripple RL is an independent project and is unaffiliated with Psyonix
or Epic Games.
