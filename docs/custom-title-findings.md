# Rocket League custom title findings

Reference test completed on 25 November 2026 using the reference app.

## What the reference test confirmed

- Original Ripple RL rendered a newly authored title, `RIPPLE CUSTOM TEST`, in the Rocket League main menu. The title had green text (`00FF00`) and red glow (`FF0000`).
- The title also appeared on the local player's scoreboard in a private match. Visibility to other players was untested.
- The game log recorded successful PsyNet login, an established WebSocket connection, and private-match activity. The title and both color values were still present in the local cache during the match.

## Title definition in the local cache

Rocket League's cached `PlayerTitleConfig` contained two linked records.

| Collection | Fields observed | Test values |
| --- | --- | --- |
| `Titles` | `ID`, `Text`, `Category` | A generated `CustomTitle_` ID, the configured title text, and a reference to the color category |
| `Categories` | `ID`, `Color`, `GlowColor` | The category referenced by the title, `00FF00`, and `FF0000` |

Five identical copies of the cached configuration contained both records during the match. The cache entry includes a `Signature` field. The capture establishes which title and color data Rocket League had available. How the reference app supplied that configuration and how the game accepted it remain unverified.

## What Ripple RL already supports

- The C++ UI can create, edit, enable, and remove custom titles. The backend generates a `CustomTitle_` ID and saves the text, color, and optional glow. [C++ title panel](../ui-cpp/src/CatalogPanels.cpp) · [Native save handler](../backend-cpp/src/Dispatcher.cpp)
- The game-facing `/s/title` response carries the selected title ID. The backend picks the most recently saved custom title when the feature is enabled and a custom title is available. Otherwise, it falls back to the most recently spawned catalog title. The response has no text or color fields. [Title selection and response](../ripple/social.py)
- The existing `Developer` title was previously verified in the rebuild's main menu and in-match scoreboard. All 12 title tests passed during this investigation. The automated tests cover title spawning and the title response. Rendering a newly authored title in the rebuild remains unverified. [Live test notes](../HANDOFF.md) · [Title spawning tests](../tests/test_title_spawn.py) · [Title response tests](../tests/test_title_wire.py)

## Remaining gap

The rebuild needs a supported way to register a new title ID and its color category with the Rocket League client before the native UI can resolve that ID. The project already persists the definition and sends the ID. This investigation found no supported registration path in `ripple`. The investigation itself made no changes to the title implementation or to installed game files.

## Implemented registration path (2026-11-25)

`ripple/titles_config.py` registers enabled custom titles by injecting the same records
the reference app used into `PlayerTitleConfig`: a title `{ID, Category, Text}` plus the linked
category `{ID: "ripple_custom_<ID>", Color, GlowColor}`. It patches every cached configuration
carrying that block (backing up as `.ripplebak`, preserving the Unreal envelope and property
list) and rewrites a freshly fetched config response in flight, re-signed with the response's
`PsyTime`. The proxy and the C++ launcher refresh on start and on title changes; teardown
restores the cache. Live rendering of a new custom title is the remaining check.

## 2026-11-26: CRC error root cause and the registration path

### Cache CRC (the "rejects with CRC error")

Rocket League validates every WebCache entry on import. `Launch.log` shows
`LocalCacheImport: File is Corrupt, CRC check failed: '<path>'` for the injected copy and the
client then re-exports the pristine config. The check lives in `RocketLeague.exe`:
`LocalCacheImport` (string refs at 0x140ddd5dc/651) feeds the parsed entry to a CRC helper
(0x1403a1b00) and compares the result with the **header hash at offset 4**, not the
`DataCRC` property.

- Algorithm: CRC-32 with the non-reflected table (polynomial `0x04C11DB7`), initial value
  `~0x6EEFBED4`, final complement — i.e. `rl_crc(data) = ~crc_unreal(~seed, data)`.
- Input: **every byte of the cache file after the 8-byte size/hash header** (`file[8:]`).
- Verified byte-exact on both reference copies (before 0x5c7e63d6, after 0x7525d1dc), which
  are the pre-injection and the accepted post-injection cache.
- `titles_config.header_crc()` implements it and `rebuild_cache()` now stamps it, so an
  injected cache entry passes `LocalCacheImport`. Live test: the `CRC check failed` error no
  longer appears; without the fix the game reported it every launch.

### The cache alone does not make the title appear

Every launch the client runs `PsyNetStaticData: HandleCacheExpired` and refetches the config,
then re-exports **556,104 pristine bytes** over the patched cache. The injected records survive
only if the client receives them at fetch time.

### How the reference app supplied the config (confirmed live, 2026-11-26)

Ran the reference app + RL and watched ports, hosts and the cache:

- Shift binds only `127.0.0.1:49320` (its `/s/*` server) and `49325`; it does **not** bind 443
  and does **not** touch the hosts file (it actually removed a stray
  `127.0.0.1 config.psynet.gg` entry on start).
- RL's config fetch goes **direct** to `config.psynet.gg`; in the Shift session it returned
  **304** because Shift had patched the cached body while preserving the server `ETag`, so the
  conditional GET kept the patched body in place.
- After a 304 (or a real 200) RL re-exports its cache itself with a valid header CRC; Shift's
  `WebCacheBootstrap::refreshConfigs` (`QFileSystemWatcher` + `DateGuard`, per-URL state under
  `%LOCALAPPDATA%\Shift\cache-state\*.dat`) re-injects the titles into the freshly written
  file. The cache captured mid-session had 1,572 `CustomTitle_Shift_` records and a header CRC
  that validates with the formula above — Shift computes the same CRC.
- RL clears the `ETag`/`Signature` properties when it re-exports after a 304; a subsequent
  fetch is then a plain 200 and the watcher re-injects again. Net effect: every config load
  the client performs sees the titles.

So the mechanism is **cache injection + valid header CRC + a re-injection watcher**, not a
proxy: the client is never redirected, the server is only asked conditionally, and the patched
body survives every re-export. (RL's `DevOnline` WebRequest also ignores the system proxy and
the hosts file, which rules out interception for the config fetch.)

### Ripple implementation (2026-11-26, Shift-faithful)

Connection recovery removed the experimental port-443 listener, hosts redirection and
process-memory URL rewriting from `ProxyManager`; that experiment is history. The Shift
mechanism needs none of it:

- `titles_config.rebuild_cache()` stamps the client's header CRC (`header_crc`) so
  `LocalCacheImport` accepts the entry; the property block (ETag/Signature/DataCRC) is
  preserved verbatim.
- Injection runs at proxy start, at launch (`RefreshTitlesCache` in the native `home_launch`)
  and on every title edit — the `prepareLaunch` equivalent.
- **New:** `titles_config.watch_webcache()` + `cache_seen()` watch the WebCache for client
  rewrites (mtime/size) and re-inject immediately — the `refreshConfigs` equivalent, wired
  into the proxy addon's monitor loop (`TITLE_CONFIG_CACHE_REFRESHED watch`).
- Live-verified end to end with a standard Epic launch (no EAC needed): RL imported the
  patched cache, fetched the config (200 — the cache carried an empty ETag), rewrote it, and
  the watcher re-injected within a second; the cache keeps the custom title through RL's own
  re-exports (553,306 bytes, title present, header CRC valid).
- 10 `tests/test_title_register.py` checks pass (injection, idempotence, disable, envelope
  round-trip, header CRC vs the reference cache, backup/restore, watcher re-injection after a
  client rewrite, live re-signing).
- Remaining: visual confirmation that the custom title renders in the main menu and
  private-match scoreboard with these records in place.

## 2026-11-26 evening — what was tried, what is still missing

The cache side is proven; the missing piece is getting the client to *use* the patched body
on a fresh fetch. Two routes were explored and neither is finished:

1. **Direct config serving (Shift's `ConfigProxy`).** RL's config WebRequest ignores the
   system proxy and the hosts file. It does resolve `*.localhost`, but:
   - `config.localhost` resolves to `::1` first, so the local listener must be IPv6
     (`--listen-host [::]`, dual-stack 443); binding IPv4 only fails.
   - Even with a correct SAN cert (`config.psynet.gg` + `config.localhost`) signed by the
     machine-trusted mitmproxy CA, RL rejects the handshake
     (`Client TLS handshake failed ... ZeroReturnError`). It pins the config TLS chain, which
     is why Shift serves config over **plain HTTP**.
   - Switching the live URL to `http://` needs the scheme replaced, but RL stores the host as
     its own `config.psynet.gg` buffer: a 16-byte host swap (`config.localhost`) keeps the
     path, while a 24-byte `https://config.psynet.gg` needle was not found in memory. So the
     URL stays `https://` and the pinned chain blocks local TLS.
2. **In-process URL rewrite.** Python can find and patch the host when memory is readable
   (proven earlier: `HandleGetURL URL=https://127.0.0.1?...`, and later
   `https://config.localhost/v2/...`). Under a normal Epic launch EAC denies
   `ReadProcessMemory` (`ERROR_ACCESS_DENIED`), and in the latest `-noeac` attempts the patch
   still returned False (process appears to run at elevated integrity while Ripple is not).

**Shift's no-EAC launch (found in its memory/registry strings):** it writes Epic's own
per-game extra arguments and lets Epic launch the game, so auth is still fresh and the process
has no EAC. The config file is
`%LOCALAPPDATA%\EpicGamesLauncher\Saved\Config\WindowsEditor\GameUserSettings.ini`, section
`[<epicAccountIdHex>_Settings]`, keys
`<sandbox>:<clientId>:Sugar_AdditionalCommandsEnabled=True` and
`..._AdditionalCommands=" -noeac"`. Ripple now reproduces this in `Home::Launch` (writes, opens
the launch URI, restores after 30 s). It successfully launched RL without EAC (no
`RocketLeague_EAC.exe`), but that alone did not make the memory patch work.

**Where the cache approach stands:** injection with the correct header CRC, the client-rewrite
watcher, and the `-noeac` launch guard all work and are in the build. A 200 response still
replaces the in-memory config, so a title becomes visible only when the fetch is a **304**
(cached ETag equals the server's current revision). The ETag bumps on config updates; after
the watcher re-injects the freshly written 200 body while preserving its new ETag, the *next*
refresh is a 304 and the in-memory config is taken from the patched cache. That delay is the
remaining UX gap — or is bypassed entirely if config can be served locally (route 1).

## 2026-11-26 — EAC-safe custom titles via TAGame display call sites

The cache/config routes above are a server-visibility mechanism. A simpler, fully client-local
and **EAC-on** path is to render the title by editing only `TAGame.upk` (unguarded, like the
garage patches) instead of `ProjectX.upk` (SHA-guarded, so previously usable only under
`-noeac`). RL resolves a title ID to a display struct via the import
`ProjectX.TitleConfig_X.GetTitleData` (idx `-3354`); we intercept at the TAGame callers rather
than registering a new entry in ProjectX.

### Trace (read-only, verified against clean Steam **and** Epic `TAGame.upk`)

The four edited export bodies are **byte-identical across both builds**, so one SHA-256 baseline
per export covers both; every index is still re-checked by `object_path()` at build time.

| Export | Path | SHA-256 (prefix) | Role |
| --- | --- | --- | --- |
| 38079 | `GFxData_PRI_TA.SetPlayerTitle` | `f756f33f…` | scoreboard / in-match nameplate (primary) |
| 37643 | `GFxData_PlayerTitles_TA.GetTitleData` | `f03e9a0f…` | title menu / picker list data |
| 37668 | `GFxData_PlayerTitles_TA.GetOnlinePlayerTitles` | `c5ed4be3…` | picker list (ID appears) |
| 124227 | `GFxData_PlayerTitles_TA.SetupTitleStats` | `a7837b81…` | stat-title menu (not yet edited) |
| 53001 | `OnlineGamePlayerTitles_TA.GetTitleData` | `0489a5c3…` | cached forwarder (not yet edited) |

`SetPlayerTitle` ends with three **unconditional** copies `XPTitle = Data.Text`,
`TitleColor = Data.Color`, `TitleGlowColor = Data.GlowColor` (`Data` being the resolved
`ProjectX._Types_X.PlayerTitleData`), then the `Return; Nothing; EndOfScript` epilogue. The
stat-title path (`StatTitleUtils_TA.ApplyStatValueIfStatTitle`) runs before these copies.

### RL UnrealScript bytecode notes (recovered by disassembly)

- Local/param variable = `0x2B <objref>`; instance variable = `0x01 <objref>`; struct-member
  l-value = `0x35 <member><struct> 00 01 <inner>` (reads use `00 00`); `FromHexColor` call =
  `0x1C <objref> <string> 0x16`; `==(Name,Name)` is **native `0xFE`** (`!=` is `0xFF`).
- `Jump`/`JumpIfNot` offsets are **absolute** offsets into the in-memory script, where object
  references occupy 8 bytes (4 on disk), so an injected block's VM size is its byte length plus
  four per object reference. Context access is `0x19`; a Psyonix wrapper token `0x5E` precedes
  each context.

### Implementation (`ripple/title_package.py` → `build_tagame`)

For every enabled custom title, before each function's return tail we append
`if (TitleId == <customID>) { <assign the display fields/struct> }`. Because the override runs
**last** (after the stat-title path) it is never clobbered, and because it is appended before
the return tail — exactly like the stock ProjectX `Apply` patch — **no existing jump needs
relocating** (confirmed: at the primary site no jump targets the epilogue; at the menu site the
one existing jump converges onto the inserted block, which is the desired behaviour). Sites:

- **38079 `SetPlayerTitle`** — overrides `XPTitle` / `TitleColor` / `TitleGlowColor` for the
  local player. Covers the scoreboard/leaderboard.
- **37643 `GetTitleData`** — overrides the resolved `Data` struct so the menu/picker shows the
  title.
- **37668 `GetOnlinePlayerTitles`** — the existing picker edit, so the ID lists and is
  selectable.

All three edits ship in **one** TAGame rewrite (`package_writer.build` with multiple `Edit`s and
a single shared added-name blob). Each export's SHA-256 is verified against the clean baseline
first, so a future game update fails safe. `ProjectX.upk` is never touched; the old ProjectX
registry path (`build` / `build_picker`) is retained in the module as a `-noeac` fallback but is
no longer installed. `Home::Launch` installs this patch on **both** normal and `-noeac` launches
and restores `TAGame.upk` from backup on the next launch.

### Static validation done

- All five indices resolve on both Steam and Epic; the four bodies are byte-identical across
  builds.
- The patched bodies round-trip through `package_writer.build` (bytes re-verified on reparse).
- Disassembly of the injected regions confirms each block parses as valid bytecode and every
  `JumpIfNot` target lands exactly on the next block or the return tail.
- `tests/test_package_writer.py` (incl. the byte-exact ProjectX/picker fixtures) and
  `tests/test_title_register.py` pass.

### Live verification still required (static analysis cannot prove)

1. **A — the local PRI actually holds our custom `TitleId` when `SetPlayerTitle` runs.** The
   `/s/title` plugin sets it; confirm the **scoreboard shows our text + colour with EAC ON** in
   a match. Baselines: clean `D:\SteamLibrary\…\rocketleague\TAGame\CookedPCConsole`; Epic launch
   target `D:\Epic Games Games\rocketleague\TAGame\CookedPCConsole`.
2. **B — the stat-title path does not overwrite our injected fields.** Our override is appended
   last, so it should win; confirm text/colour persist through a full match (including any
   stat-title refresh).

If A/B pass, optionally extend `build_tagame` to `SetupTitleStats` (124227) and the forwarder
(53001) using the same append-before-return pattern for the stat-title menu surface.

## 2026-11-26 — LIVE CONFIRMED: scoreboard works with EAC on

Verified in a live match (Epic, **EAC ON**) with the TAGame-only patch installed by Ripple at
launch:

- **Scoreboard / in-match leaderboard: the custom title renders** (text + colour) on the local
  player's row. This confirms **check A** (the local PRI holds our `TitleId` when
  `GFxData_PRI_TA.SetPlayerTitle` runs — set by the `/s/title` plugin) and **check B** (the
  stat-title path does not overwrite our appended override). The patched `TAGame.upk` on disk
  matched the recorded patched hash and both edited exports differed from baseline.
- **Title menu selector: the custom title is listed and rendered**, so the picker
  (`GetOnlinePlayerTitles`) and display override (`GetTitleData`) work there too.
- **Open gap — menu equip reverts to "no title":** clicking the custom title in the selector
  switches back to None immediately. This is the *equip / ownership* path, not a display call
  site, so none of the four edited exports touch it. The game validates the selected title
  against the owned/available set and, finding our ID unowned, reverts. This does **not** affect
  the scoreboard, which is driven by `/s/title` writing the PRI directly and never goes through
  the menu equip.

Next step for full menu support: trace the selector click handler (the equip path through
`GFxData_PlayerTitles_TA` / the loadout / ownership validation) and intercept the
owned/available gate for our custom IDs, the same EAC-safe TAGame-only way.

## 2026-11-26 — menu-equip revert fixed (set PlayerTitleData.Id)

Root cause of the selector reverting to "no title" on click: the menu tracks the selected
entry with `GFxData_PlayerTitles_TA.UpdateSelectedTitle`, which does
`SelectedTitle = PlayerTitles.Find('Id', EquippedTitle)`. `PlayerTitles` is populated via
`UpdatePlayerTitles → GetPlayerTitles → GetOnlinePlayerTitles` (the picker we patch), so our
entry *is* in the searched array — but it is built from `GetTitleData('<ourID>')`, and our
`GetTitleData` override set Text/Color/GlowColor only, leaving `Id` empty. `Find('Id', ourID)`
therefore returned INDEX_NONE and the selection cleared.

Fix: the menu override (`GetTitleData`, 37643) now also writes `Data.Id = <ourID>` (a Name
literal, import `ProjectX._Types_X.PlayerTitleData.Id` = -3661) before Text/Color/GlowColor, so
the picker entry carries a matching Id and `Find` locates it. The scoreboard override
(`SetPlayerTitle`, 38079) is unchanged (it writes display fields directly, not a struct).
Validated: 37643 still disassembles cleanly (all jumps land), build round-trips, install/restore
on a clean baseline adds the Id write (body 208→342) and restores exactly. Pending: live confirm
the selector now keeps the title selected.

## 2026-11-26 — titles made fully menu-driven; prefix rebranded to Ripple

Follow-up to the EAC-safe render patch: the in-game title is now chosen entirely by the game's
title menu, not forced by `/s/title`.

- `PRI_TA.SyncPlayerTitle` sets the PRI title from the equipped loadout via `SetTitle(NewTitle)`,
  which is `Title = NewTitle` with no ownership check, and our `SetPlayerTitle` patch renders any
  custom ID. Spawned titles are added to the player's products (`spawn.product_rows` ->
  GetPlayerProducts), so they are owned and equippable in the menu too.
- `social.active_title_id()` therefore no longer force-serves anything: it returns a custom only
  when explicitly pinned (`titles.pinnedId`), else `""`. The player switches among custom /
  spawned / regular / none from the game menu, keeping all customs in Ripple's inventory.
- New custom title IDs are now `CustomTitle_Ripple_<hex>` (was `CustomTitle_Shift_`); existing
  `CustomTitle_Shift_*` IDs still work (the injector's ID regex is prefix-agnostic).
- `tests/test_title_wire.py` rewritten to the new contract (nothing served unless pinned);
  `test_title_spawn.py` unchanged and still passing.

Live check pending: with `/s/title` empty, confirm an equipped custom still renders on the
scoreboard via the native loadout path. If the online-sync branch of `SyncPlayerTitle` clears it,
re-enable forcing for customs (pin) — a small Ripple toggle would set `titles.pinnedId`.
