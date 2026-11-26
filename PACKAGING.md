# Ripple RL — single-executable packaging

The goal of the C++ port: ship Ripple RL as **one executable plus a `runtime\`
folder**, with no separate backend process and no Python install required on the
user's machine (the proxy is the only part that stays Python).

## Architecture

```
┌────────────────────────── ripple_rl.exe ──────────────────────────┐
│  ImGui/DX11 UI (main thread)                                      │
│        │  socket client → 127.0.0.1:49327 (loopback, same process)│
│  BackendHost (background thread): store, ProxyManager, Overlays,  │
│        Stats, IpcServer, Dispatcher  ── all of ripple_core ──      │
└───────────────────────────────┬───────────────────────────────────┘
                                 │ subprocess
                                 ▼
                 runtime\python.exe  runtime\mitmdump-launch.py
                 (bundled Python + mitmproxy; loads ripple\proxy_addon.py)
```

- `ripple_rl.exe` links `ripple_core` (`ui-cpp/CMakeLists.txt` does
  `add_subdirectory(../backend-cpp)`), and `main.cpp` starts the backend in
  process via `BackendHost` before the UI connects:
  ```cpp
  BackendHost backendHost;
  backendHost.Start();      // store + proxy + overlays + stats + IPC + dispatch
  ...                        // UI connects to 127.0.0.1:49327 as before
  backendHost.Stop();
  ```
  The UI's socket client is unchanged — the "socket" is now a loopback inside the
  same process, so there is **one executable and no external backend**.
- `ripple_backend.exe` still builds (when `backend-cpp` is the top-level CMake
  project) for standalone debugging; it uses the same `BackendHost`.

## Build

```powershell
# One exe (UI + backend). CMake pulls in backend-cpp automatically.
cmd /c '"<vcvars64.bat>" && cmake -S ui-cpp -B ui-cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build ui-cpp/build'
# -> ui-cpp/build/bin/ripple_rl.exe
```

## Bundle a distributable

```powershell
packaging\bundle-runtime.ps1            # uses C:\Python312 by default
```

It builds `ripple_rl.exe`, lays out `dist\`, and assembles the proxy runtime
from an existing Python 3.12 that has mitmproxy + pycryptodome installed. It
  copies the interpreter + standard library (not that install's site-packages) and
then copies exactly mitmproxy + pycryptodome and their dependency closure —
computed from installed package metadata by `packaging\copy_deps.py` (44 packages
incl. cryptography/OpenSSL, `mitmproxy_rs`, brotli, h2, aioquic, zstandard).
The bundle fetches a pinned PresentMon console release, checks its SHA-256,
and includes its license beside the binary in `dist\runtime`.

Produces `dist\` (~143 MB total, verified):

```
dist\ripple_rl.exe        the app, 9.5 MB   (UI + backend hosted in-process)
dist\data\               5.4 MB            (catalog + feature-default data)
dist\ripple\      0.2 MB            (the mitmproxy addon + its imports)
dist\runtime\            128 MB            (Python 3.12 + mitmproxy + launcher)
```

Verified end-to-end: `dist\runtime\python.exe dist\runtime\mitmdump-launch.py
--version` prints `Mitmproxy 11.0.2 / Python 3.12.7 / OpenSSL 3.4.1`, and
`dist\ripple_rl.exe` launches as one process (UI window + in-process backend;
catalog/features/swaps all load from `dist\data`).

## How the proxy runtime is located

`ProxyManager::MitmdumpCommand()` resolves, in order:
1. `RIPPLE_RL_MITMDUMP` env var (explicit override),
2. **`<exe>\runtime\python.exe` + `<exe>\runtime\mitmdump-launch.py`** (the
   bundled, portable layout — avoids a pip-generated `mitmdump.exe`, which embeds
   an absolute path to its creating Python and is not relocatable),
3. `<exe>\runtime\Scripts\mitmdump.exe`, then common dev installs,
4. bare `mitmdump.exe` via `PATH`.

## Notes

- **Prefer the folder layout over a packed one-file exe.** A `--onefile`-style
  self-extractor that spins up a TLS MITM proxy and patches game files is a
  classic Windows Defender false-positive trigger; a plain `ripple_rl.exe` + a
  visible `runtime\` folder trips AV far less and starts faster.
- **What stays Python:** only the proxy — mitmproxy plus `proxy_addon.py` and the
  `ripple` modules it imports. Everything else (UI + all of `ripple_core`)
  is native. mitmproxy has no drop-in C++ equivalent, which is why the hybrid was
  the plan from the start.
