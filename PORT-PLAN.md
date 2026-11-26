# Ripple RL → C++ backend port plan (`ripple`)

This repo began as a copy of the earlier Python workspace. Goal: move the **backend** from Python to
native C++ so the whole app ships as **one executable (plus bundled runtime
files)**, matching the already-native ImGui frontend, while keeping **mitmproxy
in Python** (there is no drop-in C++ equivalent) launched as a child process.

## Current architecture (what we copied)

```
┌─────────────────────────┐        TCP JSON IPC          ┌──────────────────────────┐
│  ui-cpp/  (C++ / ImGui)  │  127.0.0.1:49327, \n-lines   │  main_cpp.py  (Python)   │
│  Backend.cpp = client ───┼─────────────────────────────▶│  IpcServer + Bridge      │
└─────────────────────────┘   {type,payload,id}/reply     │  (ripple/*.py)    │
                                                           └────────────┬─────────────┘
                                                                        │ subprocess
                                                                        ▼
                                                     mitmdump.exe + proxy_addon.py
                                                     reverse:49320  regular:49321
```

- **Frontend** `ui-cpp/` — MSVC/CMake, C++20, Dear ImGui (DX11+Win32), vendored
  CupertinoUi + nlohmann/json. `src/Backend.*` is a line-delimited-JSON IPC client.
  (The old `ui/` WebView2 SPA is still here; the ImGui UI is the "new UI".)
- **Backend** `ripple/` (Python) — command bridge (`bridge.py`, ~200
  commands), UPK parse/AES patching (`upk.py`, `patches.py`, `garage_patch.py`,
  `flip_patch.py`), catalog/store/swaps, PsyNet proxy mgmt (`proxy.py`),
  overlays + hotkeys + stats (PySide6), IPC server (`ipcserver.py`).
- **Proxy** — mitmproxy is **already a subprocess** (`mitmdump.exe`), not in-process.

## Target architecture

```
┌──────────────────────────────── ripple_rl.exe ─────────────────────────────────┐
│  ImGui frontend  ──direct calls──▶  ripple_core (C++)  ──subprocess──▶ mitmdump │
│                                     Store/Proxy/Dispatcher/...        (bundled  │
│                                                                        Python)  │
└────────────────────────────────────────────────────────────────────────────────┘
                     + runtime\  (embeddable CPython + mitmproxy + proxy_addon.py)
```

One `ripple_rl.exe`, one `runtime\` folder holding the embeddable Python +
mitmproxy. mitmproxy stays a child process; everything else is native.

## Migration strategy (incremental, always runnable)

**Phase 1 — native backend as a drop-in (in progress).**
`backend-cpp/` builds `ripple_core` (static lib) + `ripple_backend.exe` that speaks
the **identical IPC contract** on `:49327`. The existing ImGui UI connects to it
unchanged. Un-ported commands return `"Not implemented yet: <type>"` exactly as
`bridge.py` did, so the UI behaves the same for them. Port commands one module at
a time, verifying each against the real UI. **No UI changes required.**

**Phase 2 — port the modules** (table below), each as a `ripple_core` component.

**Phase 3 — collapse to one exe (done).** `ripple_core` is linked straight into
`ripple_rl` (`add_subdirectory(../backend-cpp)`), and `BackendHost` runs the whole
backend on a background thread *inside* `ripple_rl.exe`; the UI's socket client
connects to it over loopback, so there is one executable and no separate backend
process. The proxy runtime is bundled under `runtime\`. See **PACKAGING.md** and
`packaging/bundle-runtime.ps1`. (A later refinement could drop the loopback
socket for a direct in-process call, but it is not needed for the single exe.)

## Recent additions (2026-11-26) + coordination

- **UI:** `App::GaragePatchPanel` (Settings → Maintenance → "Garage patch") exposes
  `garage.patch.status/apply/revert` — re-apply the garage/underglow/camber patch after a
  game-files verify. `tools\poke.py` sends any IPC command to a running backend (for the
  features whose Tools pages are still preview-only mockups).
- **A parallel session shares this repo** and built a package-patch writer + manager
  (`PackagePatches.{h,cpp}`, `package_writer.py`, `patch_recipe.py`, `package-patches.json`,
  Settings → Maintenance → "Package patches", `docs/ssp3-format.md`). It merges and builds
  cleanly with this port. Open question: whether the whole-file garage patch here should fold
  into that finer-grained package-patch manager. See **HANDOFF.md** (2026-11-26 entry) for the
  full cross-agent log.

## Module porting map (Python → C++)

| Python module | C++ component | Difficulty | Notes |
|---|---|---|---|
| `store.py` | `Store.{h,cpp}` ✅ started | Low | dirs + atomic JSON. **Done** (settings slice). |
| `ipcserver.py` | `IpcServer.{h,cpp}` ✅ | Low | winsock, \n-JSON. **Done.** |
| `proxy.py` | `ProxyManager.{h,cpp}` ✅ | Med | spawn mitmdump, CA, WebCache, reg proxy. **Done.** |
| `bridge.py` | `Dispatcher.{h,cpp}` ◐ | High | ~200 cmds; app/settings + features + catalog + swaps + home/launch done, rest TODO. |
| (AES-256-ECB) | `Aes.{h,cpp}` ✅ | Low | Windows CNG/BCrypt, replaces pycryptodome. **Done.** |
| (zlib + sha256) | `Zlib.{h,cpp}` + `Hash.{h,cpp}` ✅ | Med | stb_image zlib inflate + stored-block deflate; BCrypt SHA-256. **Done.** |
| (home/install/launch) | `Home.{h,cpp}` ✅ | Med | Steam/Epic detect, home.state, source-path settings, home.launch. **Done** (garage/flip skipped). |
| `features.py` | `Features.{h,cpp}` ✅ | Low | get/set over `data/feature_defaults.json` (generated from DEFAULTS). **Done.** |
| `catalog.py` + `data/*.json` | `Catalog.{h,cpp}` ✅ | Med | items/titles/categories + inventory filtering, rarity-sorted. **Done.** |
| `upk.py` | `Upk.{h,cpp}` ✅ | **High** | UE3 header parse + AES-256-ECB convert. **Done — byte-exact verified vs Python.** |
| `bootstrap.py` | `Bootstrap.{h,cpp}` ✅ | **High** | ProjectX parse/guard/patch/re-encrypt (the proxy bootstrap). **Done — verified semantically vs Python (both patched exports decode identically).** |
| `garage_patch.py`, `flip_patch.py` | `Garage.{h,cpp}` ✅ | High | ground-truth swap + flip-reset colour. **Done** — status parity + flip colour verified vs Python (flip uses stored-block zlib: valid, not byte-identical). |
| `swaps.py` | `Swaps.{h,cpp}` ✅ | High | item-swap apply/revert/list on top of Upk. **Done.** |
| `garage_patch.py`,`flip_patch.py` | TODO | High | hash-guarded byte-exact UPK patches. |
| `spawn.py` + `social.save_title` | `Spawn.{h,cpp}` ✅ | Med | spawn list (items/titles) + custom-title save. **Done.** |
| (presets, `bridge.py`) | `Presets.{h,cpp}` ✅ | Low | named swap sets, replayed through the swaps engine. **Done.** |
| `quickchat.py` | `Quickchat.{h,cpp}` ✅ | Med | VK hotkeys (`RegisterHotKey` on a pump thread) + key sequences + SendInput chat. **Done.** |
| `overlay.py` (PySide6) | `Overlay.{h,cpp}` ✅ | Med | native layered click-through Win32 windows (ball/roster/FPS). **Done — window creation + stats data path verified.** |
| `statsapi.py` | `Stats.{h,cpp}` ✅ | Med | reconnecting loopback TCP stream (not HTTP) + `ensure_ini`. **Done — tracker.changed verified.** |
| `workshop.py` | `Workshop.{h,cpp}` ✅ | Med | map import/apply/restore/delete. **Done.** |
| `packs.py` + zip | `Packs.{h,cpp}` + `Zip.{h,cpp}` ✅ | Med | custom packs import/list/apply/restore; zip via stb raw-DEFLATE. **Done — zip extraction byte-verified.** |
| `bootstrap.py` | folded into `ProxyManager`/`Patches` | High | ProjectX bootstrap patch. |
| `proxy_addon.py` | **stays Python** | — | runs inside mitmproxy; shipped in `runtime\`. |

Legend: ✅ done · ◐ partially done · TODO.

### What the native backend already does
`ripple_backend.exe` boots like `main_cpp.py`: resolves state dirs, starts the
**bundled Python mitmproxy** (same flags/CA/WebCache/registry handling as
`proxy.py`), serves IPC on `:49327`, and natively handles:
- app/settings: `app.state`, `app.setTheme`, `app.setLang`, `settings.get`,
  `settings.setBringToFront/setCloseToTray/setAlwaysOnTop`, `win.*` shims.
- catalog: `items.categories`, `catalog.titles`, `items.list` (category/rarity/
  query/owned filters, rarity sort, paging), `items.inventorySync`.
- features (get/set): titles, fakerank (incl. overrides/reward), ping, platform,
  qc (read), misc.match, overlay, garage, paint, match, game, cust.palette,
  cust.cameraCaps, credits, tracker (+validated `tracker.set`), sounds, rlsettings,
  fps, spoof, invite, plus `mp.state` and `stadium.get`.
- item swaps: `items.swap`, `items.paint`, `active.list/revert/revertAll/
  copyToOther` — native UPK parse + AES-256-ECB (BCrypt). The `convert` output
  is **byte-for-byte identical to upk.py**, verified on a real package across
  three target-name lengths (incl. one that changes the padded length).
- proxy bootstrap + launch: `home.state`, `home.launch`, `home.setInstall`,
  `home.setPseudo/forgetPseudo`, `settings.setSourcePath/detectSourcePath/
  openBackupFolder`. `home.launch` starts the bundled proxy, runs the native
  ProjectX bootstrap (`Bootstrap.cpp`, verified against `bootstrap.py`), applies
  the garage + flip UPK patches (`Garage.cpp`), and launches Rocket League
  (Epic URL or exe). `garage.patch.status/apply/revert` are wired too.
- stats + overlays: a reconnecting Stats TCP client (`Stats.cpp`) drives native
  layered click-through overlay windows (`Overlay.cpp`, ball speed / roster /
  FPS) and emits throttled `tracker.changed` events. `overlay.set`/`fps.set`/
  `tracker.set` re-apply overlay visibility. Verified end-to-end (fake stats
  stream → tracker.changed; overlay window created). Quickchat hotkeys (input
  injection) are not ported yet.
- workshop + custom packs: `workshop.list/import/apply/restore/delete` and
  `custom.list/import/remove/rename/apply/restore` (+ per-kind variants). A
  built-in zip extractor (`Zip.cpp`, stored + DEFLATE via stb raw-inflate) with
  zip-slip protection; apply/restore stage, back up, and hash-guard the install.
  Verified: a deflated .upk inside a zip extracted byte-for-byte; import/list/
  remove/delete round-trip over IPC.
- presets + spawn + quickchat: `presets.*` (named swap sets), `spawn.*` (the
  injected spawn list + paints/titles), `titles.save`, and `misc.qc.*` (global
  hotkeys + key sequences typed into RL via SendInput, on their own message-pump
  thread). Verified over IPC: preset CRUD, spawn add/title/paints, and quickchat
  labels/setKey/setEntries binding. The PsyNet wire formatting that reads this
  state stays in `proxy_addon.py` (Python, inside mitmproxy).

Verified end-to-end over the socket (catalog loads 9068 items, owned filter,
feature persistence, error parity). Everything else answers
`"Not implemented yet: <type>"`, exactly as `bridge.py` did.

Feature defaults live in `data/feature_defaults.json`, regenerated from
`features.DEFAULTS` by `scripts`/the Python itself — never hand-edited.

## Bundling Python + mitmproxy (for the single-exe goal)

- Ship the **python.org embeddable** distribution under `runtime\`, plus
  mitmproxy and its deps (`cryptography`/OpenSSL, brotli, h2, …) into
  `runtime\Lib\site-packages`, and `mitmdump.exe` under `runtime\Scripts\`.
  `ProxyManager::MitmdumpPath()` already looks in `<exe>\runtime\Scripts` first
  (override with `RIPPLE_RL_MITMDUMP`).
- Keep `ripple/proxy_addon.py` (and its imports) next to the app; it is
  loaded by mitmproxy via `-s`. `ProxyManager` sets `PYTHONPATH` to the app root.
- **Prefer a visible `runtime\` folder over a one-file self-extractor.** A packed
  `--onefile` exe that spawns a TLS MITM proxy and patches game files is a
  classic Windows Defender false-positive trigger; a plain folder trips AV far
  less and starts faster.

## Known risks / open questions

1. **UPK/AES item-swap core (`upk.py`)** — DONE. AES-256-ECB via Windows CNG
   (BCrypt); `upk::Convert` output verified byte-for-byte against `upk.py` on a
   real package (`ripple_upktool` CLI diff). The remaining `patches.py` (ProjectX
   proxy bootstrap) uses zlib chunk (de)compression and is still Python; port it
   the same way (Python oracle + byte diff) when collapsing the proxy bootstrap.
2. **CA install needs admin.** `ProxyManager::InstallCa()` tries non-elevated
   `certutil` then an elevated fallback (UAC once), gated by a store check so it
   won't re-prompt every launch.
3. **Overlays/hotkeys** currently lean on PySide6; native replacements are
   layered Win32 windows / ImGui + `RegisterHotKey`.
4. **Stats API** is HTTP+JSON — straightforward with WinHTTP (already linked).

## Build (native backend)

See `backend-cpp/README.md`. Short version (VS 2026 / CMake 3.30, no VS
generator for v18 — use Ninja under the dev environment):

```
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S backend-cpp -B backend-cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build backend-cpp/build
```
