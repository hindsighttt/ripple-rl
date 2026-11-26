# Contributing to Ripple RL

## Issues and changes

For a bug report, include the Ripple RL commit or build, your Windows version,
your Rocket League platform and build, and the steps to reproduce the issue.
For a patch issue, include the affected package name and the status shown under
**Settings → Maintenance → Garage patch**. Remove account information and local
paths from any logs or screenshots you post.

Branch from `dev` when working on preview pages. `main` is the release branch
and shows pages with working controls. For a pull request, describe the change
and how you tested it, and mention which parts of the app it affects. For a
desktop feature, check the path from the menu through the command dispatcher to
the backend. For a saved setting, exercise both a fresh configuration and an
existing one.

## Development setup

Follow the [Windows build instructions](README.md#build-and-run-on-windows) for
the desktop app. To build only the backend and its verification tools, run the
following from the same x64 developer prompt.

```cmd
cmake -S backend-cpp -B backend-cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build backend-cpp/build
```

The standalone backend is `backend-cpp\build\bin\ripple_backend.exe`. Set
`RIPPLE_RL_NO_PROXY=1` before running it for a local IPC smoke test.

## Tests

For the Python tests, use Python 3.12 with the dependencies from
[`requirements.txt`](requirements.txt). From the repository root, run the
synthetic package-writer tests with this command.

```powershell
python -B -m unittest discover -s tests -p test_package_writer.py
```

Run the full suite with `python -B -m unittest discover -s tests` when you have
the local binary fixtures. If a change affects game-file patching, also verify
the generated package can be reopened, unrelated exports are preserved, and a
source hash mismatch is rejected. For an installer change, exercise apply and
revert on copies of a supported clean installation before any in-game check.
Record the game build and the checks you ran in the pull request.

## Repository contents

Keep build outputs, game packages, generated binary fixtures, raw captures,
inventory dumps, keys, and machine-specific data out of commits. Use small,
synthetic fixtures for new tests where possible. Keep existing third-party
license notices with their code. Ripple RL currently has no project-wide
license file.
