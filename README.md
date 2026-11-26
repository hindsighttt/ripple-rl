# Ripple RL

Ripple RL is a Rocket League desktop toolkit for car customization, local patch
management, overlays, and quick chat.

## Run

Open `dist/ripple_rl.exe`, or run `run-ui.bat`. The desktop executable hosts the
native backend in the same process. The distribution includes its Python runtime.

For a development build, run `tools/build_ui.bat` from the repository.
`tools/build_backend.bat` builds the standalone backend and verification tools.
See [PACKAGING.md](PACKAGING.md) for distribution instructions.

The repository contains source files. The desktop build requires a local copy of
`ui-cpp/assets/InstrumentSans-Medium.ttf`. The legacy web UI uses local HTML and
CSS with embedded artwork. Tests that read binary fixtures require local copies
of those fixtures.

## Garage patches and local data

Configure the Rocket League install in Settings. The native installer builds the
Garage patches for the supported Epic revision from clean game files and the
shipped recipe. See the [Garage recovery notes](docs/ssp3-format.md#restoring-the-epic-garage-patches-after-file-verification)
for re-applying them after file verification.

Settings, profiles, and backups live under `%APPDATA%\RippleRL`. Its data directory
is separate from the Ripple osu! project.

Run the test suite with `python -B -m unittest discover -s tests`.
