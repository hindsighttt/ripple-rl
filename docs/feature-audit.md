# Feature audit

## Current wiring checkpoint — 2026-11-24

This checkpoint supersedes older pending-state notes below. Startup spawning/equip and
Garage live updates were confirmed in game; Garage confirmation came from the user.
User now requested an easy-wins pass plus item removal. Rich car palette is excluded.

Implemented in this pass:

- Live removal path for locally spawned products: exact instance selection, bulk/clear,
  signed client expiration updates, local cleanup acknowledgment, and mixed cleanup
  requests filtered to real IDs with a verified/recomputed signature. Offline tests pass;
  visual in-game removal still needs the user's test. Next startup excludes removed rows.
- Tracker SPA key/value settings apply immediately; speed overlay uses chosen units/precision.
- Name history stores recent names and supports deleting history without clearing the name.
- Epic/Steam path selection and switching preserve saved install paths.
- Close-to-tray, always-on-top, and bring-to-front hotkey settings are connected, including
  the SPA's encoded virtual-key/modifier payload.
- Custom pack apply/restore aliases and payloads, nested ZIP imports, active state, rename,
  remove, thumbnails and ball completion events are connected. UPKs are backed up; restore
  checks installed hashes. Generic UPK packs are supported, while specialized car-body
  options remain incomplete. Historical packs lack the new active-pack restore ledger.

Validation: 34 tests pass, including actual bridge-dispatch payloads, temporary pack
apply/restore and signed expiration cleanup; compileall passes. No computer use, app/game
restart or game-package mutation was performed for this pass. Restart the rebuild GUI once
to load bridge/window changes. User performs game testing. Existing SPA remains unchanged.

Deeper remaining work includes actual game FPS (current count reflects Stats packets),
loadout CRUD/delay/hold, join-team/spectate RPC, rank wire parity, drops/credits/Rocket Pass,
workshop/TAS, startup registration, Discord RPC and specialized custom-car controls.
Registered stub commands are not completed features. Read older findings below as dated
evidence rather than the current completion state.

Reviewed 2026-11-24 against `ui/app.js`, the Python bridge, and original saved data. Registered
commands establish dispatch coverage. Game behavior requires runtime verification. The 1.8.4 target differs from the
2.6.2 Lite build described in `%APPDATA%/RippleRL/research/reference-feature-notes.md`.

## Completed saved-data corrections

- Custom titles persist creation, edits, and deletion.
- Theme updates accept the SPA's `dark` boolean and existing `theme` or `value` payloads.
- Quick-chat hotkey state initializes before its first registration attempt.
- Quick-chat keyboard labels and target keys persist through the recovered SPA commands.
  Enabled-state changes reload bindings. Unicode input, foreground-game checks, keyboard
  sequences and channel selection passed mocked-input checks. Controller input remains pending.
- Stats API reconnects with bounded backoff and closes its socket/thread on application exit.
- New swap presets capture the current active swap rows. Rename and duplicate use the SPA's
  `name` and `newName` payloads. Renaming the selected preset updates `currentPreset`, while
  duplication retains all stored fields and keeps the selection. Old preset rows remain intact.
- `catalog.titles` returns recovered `data/titles.json` and `data/categories.json` using the
  SPA's expected `titles` and `categories` properties.
- `spawn.paintable` uses the exact recovered set of 2,465 product IDs. `spawn.addList` accepts
  the SPA's `productIds` and `paints`, stores compatible paint combinations, adds incompatible
  products once unpainted, and returns `count`. This completes saved spawn-list behavior only.

The original `Ripple RL/presets.json` stores full active rows in each preset's `swaps` array. The
SPA issues `presets.rename` and `presets.duplicate` with `name` and `newName`. These provide
the saved-data contract used above. Temporary-state bridge-dispatch checks cover the additions.

## Priority game behavior still pending

- Persistent PsyNet connection and inventory/spawn synthesis. Spawn storage and the ID-list
  endpoint exist. Gear and WebSocket payload semantics remain unresolved.
- Verify fake-rank wire format and season rewards. `/s/sk` JSON remains inferred.
- Verify name and ping behavior across a clean restart and supported game contexts.
- `items.paint` only records metadata. Cross-install copying returns zero. Preset loading
  suppresses individual swap errors and can report success after incomplete application.
- Custom pack metadata, car options, ball-image conversion, thumbnails, downloads, and restore
  commands remain incomplete. Workshop activation, play, downloads, and rooms are stubs.
- Garage lighting/camber, paint/rainbows, match behavior, mutators, stadium customization,
  palette/camera caps, friends, credits, and platform display mostly persist UI state only.
- Quick-chat controller sequences and live keyboard/channel behavior need verification. FPS
  counts Stats API packets and roster MMR remains zero.

## Missing recovered SPA commands

- `spawn.addTitle`.
- `custom.ball.restore`, `custom.car.restore`, `custom.decal.restore`, `custom.hud.restore`.
- `credits.add`, `credits.clear`, `sim.rp.get`, `sim.rp.set`.
- `invite.send`, `invite.joinRequest`.
- `loadouts.rename`, `loadouts.duplicate`, `loadouts.delete`, `loadouts.deleteMany`,
  `loadouts.import`, `loadouts.importMany`, `loadouts.move`, `loadouts.copyTeam`,
  `loadouts.setColors`, `loadouts.setSlot`.
- `loadouts.config.create`, `loadouts.config.delete`, `loadouts.config.import`,
  `loadouts.config.reapply`, `loadouts.config.rename`, `loadouts.config.switch`.
- `plugins.set`, `plugins.values`, `plugins.clearSlot`.
- `rlsettings.copy`, `rlsettings.load`, `rlsettings.remove`, `rlsettings.rename`, `rlsettings.undo`.
- `tas.state`, `tas.new`, `tas.load`, `tas.refresh`, `tas.rename`, `tas.save`, `tas.delete`,
  `tas.enable`, `tas.settings`, `tas.cmd`.

This list includes commands routed through SPA wrapper functions. Additional dynamically
constructed commands and runtime behavior still require checking.

## Spawn-list contract and recovered resources

The SPA supplies `spawn.addList` with `{productIds, paints}` and expects `count`. Its paint
picker explicitly promises that incompatible items spawn once, unpainted. This now uses the
original `paintable.json`, recovered from the supplied `%APPDATA%/RippleRL/research/reference-runtime.bin` without launching
or modifying any process. Temporary-state `Bridge.dispatch` checks covered compatible and
incompatible products, existing rows and unknown fields, generated IDs, empty input, duplicate
selections, and invalid-paint rollback.

The compiled Qt resource names begin at dump offset `0x4607e6c0`, the 22-byte resource tree
entries at `0x4607f610`, and resource data at `0x45da2600`. Every data entry has a big-endian
32-bit stored length. Compressed entries have a second big-endian decoded length followed by
zlib data. Decoded lengths were verified before writing these artifacts.

| Artifact | Dump data-entry offset | Stored bytes | Decoded bytes | SHA256 of decoded file |
| --- | --- | --- | --- | --- |
| `docs/recovered-native/sp.bin` | `0x45f0ad13` | 61316 | 481423 | `5abfaedb882762150f92f195125aea01d2365b7bb2a45aa148ffca89b077755b` |
| `docs/recovered-native/sp_lite.bin` | `0x45ec8007` | 40658 | 294537 | `39ee2ebb7918d2a006a50223151a4746c6924d5c508306fdc953d57951a33cea` |
| `docs/recovered-native/sp_legacy.bin` | `0x45f58e81` | 180458 | 1019916 | `428b8cd240c3db94c36faa44e291a517115bc6993c8a075dd12b0af65e32a90a` |
| `data/paintable.json` | `0x45ed1edd` | 12691 | 12691 | `c7dae0e99b1cf73961b9ab76b65235678c0220b2e5b2f4a072fc8d05ed04d5e3` |

All three `sp` resources start with `SSP3`. They are package patch manifests containing
UnrealScript bytecode and package names. Main and Lite contain five package groups for
`TAGame.upk`, `GFxShell_SF.upk`, `GFX_PremiumContainerMenu_SF.upk`, `Engine.upk`, and
`GFX_Hud_SF.upk`. Neither contains `ProjectX.upk`, `ws://`, or named PsyNet functions. The
native code separately references `PsyNetConnection_X.GetPsyNetURL`,
`PsyNetMessengerWebSocket_X.Init`, and `PsyNetMessengerWebSocket_X.Init.InURL` near the
loopback HTTP/WS prefixes and SHA256 guards. The complete patch record layout and that
separate native patch routine still require decoding.

The prior 1.8.4 report mentions `S.SpawnList.v1`, `S.SpawnFor.v1`, `S.LoadoutDigits`, `SPacket1`,
and `ShopPurchase`/`SpawnedDropOpen`. Its later sections speculate about digit encoding and
inventory synthesis. Those claims do not constitute a verified encoder or decoder.

## Runtime bootstrap and inventory status (2026-11-24)

ProjectX bootstrap guards and chunk mappings are decoded in bootstrap.py, with a complete
hash-addressed package backup. The corrected bootstrap has authenticated and connected
the live PsyNet socket while the reference app is absent. Name spoof is visibly displayed.
An earlier incorrect assignment patch crashed and was restored before correction.

Inventory wire format is captured from Products/GetPlayerProducts v2. ProductData synthesis
and response HMAC pass local replay checks, but live responses were rejected with
Missing or wrong signature. The garage shows only base Cristiano. Header delimiter
preservation is now fixed and awaiting a fresh live response. Do not mark spawning complete.
The /s/gear request still returns upstream 301; no verified custom gear encoder exists.

### Signature investigation

Static inspection of the current RocketLeague.exe (SHA256
4b8eeef3f8026d9e9cb929c6bc019304960f86a03349e5ff74b2a432354fb1bb) locates
the signature-failure caller at VA 0x140dfcd80 and verifier at 0x140e1e750. The verifier
reads PsySig/PsyTime, decodes a 32-byte key from VA 0x141de9c10 by XOR with the four bytes
following it, and calls 0x140dd75c0. The decoded key exactly equals RESPONSE_KEY. That
helper concatenates the time bytes, a dash, and the body array before HMAC/base64. This
was read-only inspection; no executable patch was made.

Header spacing preservation did not resolve the live failure; server already uses one
space. Installed mitmproxy Fragmentizer instead uses 4,000-byte continuation chunks for
length-changing edits. The addon now raises that threshold to 16 MiB so the inventory
edit is one complete frame. This transport hypothesis awaits live confirmation.
Four inventory-wire tests cover signatures, owned-row preservation, duplicate prevention,
invalid-signature passthrough, and large edited-message framing.

### Live framing result: confirmed

With the 16 MiB fragmentation threshold, the next real startup had no inventory signature
failure. The client showed NEW ITEM / CRISTIANO / TITANIUM WHITE. Equip Now visibly changed
the menu car to the white Cristiano wheels. This verifies startup inventory injection and
menu equip with the reference app absent. Live state updates, removal/restart, and match
replication still need verification. Header-spacing theory was ruled out.
