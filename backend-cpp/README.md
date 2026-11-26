# backend-cpp — native (C++) Ripple RL backend

Native port of the Python backend (`main_cpp.py` + `ripple/`). Phase 1:
a drop-in IPC server that the existing ImGui UI (`ui-cpp/`) connects to
unchanged. See `../PORT-PLAN.md` for the full strategy and module map.

## Layout

| File | Role | Ports from |
|---|---|---|
| `src/Store.*` | app/data/state dirs + atomic JSON settings | `store.py` |
| `src/ProxyManager.*` | launches bundled mitmproxy, CA, WebCache, system proxy | `proxy.py` |
| `src/IpcServer.*` | loopback TCP, line-delimited JSON | `ipcserver.py` (server) |
| `src/Dispatcher.*` | command table (`app.*`/`settings.*` done, rest TODO) | `bridge.py` |
| `src/WinUtil.*` | UTF-8↔UTF-16, env, child processes | — |
| `src/main.cpp` | `ripple_backend.exe` entry | `main_cpp.py` |

Builds `ripple_core` (static lib, re-linkable into `ripple_rl` later) and
`ripple_backend.exe`.

## Build

This machine has **Visual Studio Community 2026 (v18)** and **CMake 3.30**.
CMake 3.30 has no VS generator for v18, so build with **Ninja** from the VS
developer environment:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake -S "C:\coding projects\rocket league\ripple\backend-cpp" -B "C:\coding projects\rocket league\ripple\backend-cpp\build" -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build "C:\coding projects\rocket league\ripple\backend-cpp\build"'
```

Output: `backend-cpp/build/bin/ripple_backend.exe`.

(When a CMake with a v18 generator is available you can instead use
`-G "Visual Studio 18 2026" -A x64`.)

## Run

```powershell
# Inert smoke test — serves IPC only, does NOT start the proxy, so it leaves the
# system proxy / WebCache / cert store untouched:
$env:RIPPLE_RL_NO_PROXY = "1"; .\build\bin\ripple_backend.exe
```

Without `RIPPLE_RL_NO_PROXY`, and when `settings.proxyEnabled` is true, it starts the
bundled mitmproxy exactly as `proxy.py` did (installs the mitmproxy root CA —
prompts UAC once —, retargets RL's WebCache, and turns the Windows proxy on;
Ctrl+C reverts all of it). Then launch the UI (`ui-cpp/build/bin/ripple_rl.exe`);
it connects to `127.0.0.1:49327`.

### Locating mitmproxy
`ProxyManager` resolves `mitmdump.exe` in this order:
1. `RIPPLE_RL_MITMDUMP` env var (explicit path),
2. `<exe>\runtime\Scripts\mitmdump.exe` (bundled, production layout),
3. `C:\Python312\Scripts\mitmdump.exe`, then `%LOCALAPPDATA%\Programs\Python\Python312\Scripts\`,
4. bare `mitmdump.exe` via `PATH`.

For dev against the existing Python install, (3)/(4) work as-is. For the shipped
single-exe build, drop an embeddable Python + mitmproxy under `runtime\` (1/2).

## Status

Ported & building: Store (settings), IpcServer, ProxyManager, Dispatcher
(`app.state`, `app.setTheme`, `app.setLang`, `settings.get/setBringToFront/
setCloseToTray/setAlwaysOnTop`, `win.*`). Everything else returns
`"Not implemented yet: <type>"` — see `../PORT-PLAN.md` for the port order.
