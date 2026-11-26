# Garage / underglow save format (decoded 2026-11-24, live capture)

Captured by running the reference app, enabling underglow with a known fingerprint
(Team1=red, Team2=green, both on), and reading what it wrote. No RL launch.

## Two stores, same data

1. **Desktop store — `%APPDATA%\Ripple RL\preferences.ini`** (Qt `QSettings` INI).
   This is what Ripple RL's **desktop app** reads/writes. Fully decoded below.
2. **In-game store — `<RL install>\TAGame\Cache\S.save`** (keys `S.<Feature>.v1.<field>`).
   Written by the injected UnrealScript bytecode at runtime; needs RL running with
   the patched UPKs, so it was NOT produced here. Holds the same values.

## preferences.ini `[garage]` — confirmed encoding

Fingerprint set in the UI → exact lines written:

```
underglow0\on=true
underglow0\rgb=16711680      ; 0xFF0000 red  (team 0 = BLUE / Team 1)
underglow0\level=10
underglow1\on=true
underglow1\rgb=65280         ; 0x00FF00 green (team 1 = ORANGE / Team 2)
underglow1\level=10
underglow0\rainbow=false
underglow1\rainbow=false
```

Encoding rules:
- Section `[garage]`; QSettings group separator is `\`.
- Team suffix: `0` = Team 1 (Blue), `1` = Team 2 (Orange).
- `on`      → bool, literal `true` / `false`.
- `rgb`     → **decimal integer of packed 0xRRGGBB** (NOT a hex string). 16711680=FF0000, 65280=00FF00, 16777215=FFFFFF.
- `level`   → integer brightness, range 0–10 (UI slider). Lights/underglow only.
- `rainbow` → bool (Plus feature).

Sibling families, same shape:
- `headlights{0,1}\{on,rgb,level,rainbow}`, `taillights{0,1}\{on,rgb,level,rainbow}`.
- Paint — `primary|secondary|trim|wheels|boost{0,1}\{on,rgb,rainbow}` (no `level`).
- `stadium{0,1}\{on,rgb,rainbow}`.
- Camber (flat, not per-team): `camber`, `upDown`, `leftRight` (ints).
- Globals: `rainbowSpeed` (int, 20 default), `menuBackground` (-1 = none), `menuMap`.
- Same section also holds match/game/plugin/instant settings (see lines 70–134).

## Map to the in-game S.* keys (from the SSP3 bytecode)

| preferences.ini         | S.save key                   |
| ----------------------- | ---------------------------- |
| `underglow0\on`         | `S.Underglow.v1.enabled0`    |
| `underglow0\rgb`        | `S.Underglow.v1.color0`      |
| `underglow0\level`      | `S.Underglow.v1.brightness0` |
| `underglow0\rainbow`    | `S.Underglow.v1.rainbow0`    |
| (…1 variants → …1 keys) |                              |
| `rainbowSpeed`          | `S.Rainbow.v1.speed`         |

So the desktop values feed the in-game bytecode 1:1; the only open question is the
exact byte serialization of `S.save` itself (the game writes it), which needs an RL
session to observe. The value semantics (packed-int color, 0–10 level, per-team 0/1)
are confirmed.

## Rebuild implication

The rebuild keeps garage state in `feature_garage.json`. To match Ripple RL it needs to
(a) hold these fields with this encoding (packed-int rgb, level 0–10, team 0/1) and
(b) deliver them to the patched game as `S.<Feature>.v1.<field>` — not add a proxy
endpoint. No garage `/s/` endpoint exists.

## Underglow implementation (2026-11-24) — shipped

Implemented via **ground-truth capture + hash-guarded apply** rather than a from-scratch
UPK codec (RL's cooked-compression format proved too costly to reimplement reliably inline).

- Captured vanilla->patched for all 5 garage UPKs by letting the reference app patch a backed-up
  copy of the game, validated by summary counts (TAGame +83 names / +211 exports / +2 imports,
  matching the SSP3 manifest exactly). Stored in `%APPDATA%\RippleRL\backups\ground-truth\`
  (`before/`, `after/`, `patch-manifest.json`).
- `ripple/garage_patch.py`: `status` / `apply` / `revert`, keyed by each package's
  vanilla sha256. apply is byte-exact (writes Ripple RL's own output), backs up vanilla, verifies
  the patched hash, is idempotent, and **fail-safe** — a package whose live hash is unknown
  (game updated) is skipped, never corrupted (proven by sandbox test).
- Wired into `bridge.home_launch` (auto-apply before launch, best-effort) and exposed as bridge
  commands `garage.patch.status|apply|revert`. Patch persists on exit (consistent with the
  ProjectX-on-exit decision); revert is manual.

Result: with the patch applied, RL's in-game Premium Garage gains the Underglow/Lights/Camber/
Paint tabs and the in-car bytecode renders them — i.e. the whole garage-visual feature set.

### Known follow-ups
- Desktop garage panel -> game: the rebuild's `garage.set` stores config in
  `feature_garage.json`, but the in-car bytecode reads `S.save`. Driving underglow from the
  DESKTOP panel needs writing `S.save` (mapping decoded above: `underglow0\rgb` ->
  `S.Underglow.v1.color0`, etc.), which still needs the `S.save` on-disk byte format (only
  produced when the in-game menu writes it). Until then, underglow is set in the in-game menu.
- Patch data is this build's local capture (~107 MB); distribution needs shipped packages/deltas
  and re-capture per RL version (same as Ripple RL reships manifests).
- End-to-end in-RL render not yet eyeballed (applier output is byte-identical to Ripple RL's, so
  rendering is expected).

## /s/geam wire format (live-captured 2026-11-24) — the real delivery channel

Underglow/garage visuals are NOT delivered via S.save. The patched in-game bytecode
fetches config over HTTP from Ripple RL's local server (port 49320) at **`/s/geam`** and
**`/s/gean`** (identical 9867-byte payloads; `/s/gear` returns empty unless a game session
is active). The 9867-byte "opaque blob" a prior agent attributed to inventory is actually
this garage config. The rebuild's proxy serves /s/name, /s/ping, etc. but NOT /s/geam —
that missing handler is why rebuild underglow doesn't render.

Decoded from a distinctive live capture (underglow red/yellow, headlights green/cyan,
taillights blue/magenta; `geam-lights.txt` + `garage-config-sample1.ini` in backups/ground-truth):

- Payload = `<prefix>` + light records + camber + paint records + trailing values, then
  padded with `~` to exactly 9867 bytes. Data region ~265 chars.
- Prefix ~13 chars (looks like a counter/id; not yet pinned).
- **Light section order: Team-1 [underglow, headlights, taillights] then Team-2 [underglow,
  headlights, taillights].** Each light record = `on(1 digit)` + `rgb(8-digit zero-padded
  decimal)` + `suffix(3 digits)`. on and rgb are CONFIRMED (matched 6 distinct colours +
  on-flags exactly). The 3-char suffix carries level/rainbow but its exact encoding isn't
  pinned (brightness 20 showed "010", 10 showed "000").
- After the 6 light records: **camber** as a signed decimal (`-108` captured), then upDown/
  leftRight, then the paint records (primary/secondary/trim/wheels/boost, rgb + rainbow, no
  level), then trailing values incl. `1.00 1.00 1.00 1.00` (scales) and a `-065` field.

### Status / remaining to implement
Lights (underglow/headlights/taillights: order + on + rgb) are decoded. Still to pin for a
byte-exact serializer: the suffix (level/rainbow) encoding, the prefix, camber/upDown/
leftRight exact format, the full paint/wheels/boost section, and the trailing floats — then
implement a `/s/geam`+`/s/gean` handler in proxy_addon.py emitting this from
feature_garage.json. NOTE: that handler edits proxy_addon.py, which another agent's test
proxy is currently running — coordinate before editing.

## Verified Garage-tab serializer (2026-11-24)

Supersedes the earlier speculative ordering. garage_wire.py reproduces nine distinct native
9867-byte samples exactly. Fixtures are tests/fixtures/garage and contain only Garage state.
The captured blob is the template for unrelated protocol sections; it is not simply padded
with tildes after the first 265 bytes. Current scope excludes customization/game tabs.

| Offset | Garage field | Encoding |
| --- | --- | --- |
| 0 | revision | 13 decimal digits, runtime metadata |
| 13 | six lights | team0 U/H/T, team1 U/H/T; on1 + RGB8 + (level-10)3 |
| 85 | camber | degrees*10, signed zero-padded width4 |
| 89 / 92 | up/down, left/right | offset*10, signed zero-padded width3 |
| 96 / 147 | team paint | primary,secondary,trim,wheels,boost; on1+RGB8+rainbow1 |
| 146 / 197 | headlight rainbow | team0 / team1 |
| 198 | rainbow speed | width3, range1..100 |
| 236 / 237 | underglow rainbow | team0 / team1 |
| 240 / 241 | taillight rainbow | team0 / team1 |
| 9841 | config revision | 13 decimal digits, runtime metadata |

Lights clamp brightness to 1..20. All rainbow flags are gated by that control's enabled
switch. Camber UI values are floats; preferences.ini stores tenths. Color stays packed RGB
rendered as eight decimal digits. Captured revisions are explicit serializer inputs for
byte-exact replay. Their native generation formula is not established; the rebuild uses
cached monotonic revisions when Garage state changes.

Proxy aliases /s/geam, /s/gean and /s/gear now serve the serializer locally. Native /s/gear
long-poll timing is not reproduced; the rebuild returns the current cached revision.

Flip-reset colour is NOT in this payload: changing it produced no HTTP byte changes. The
native UI stores garage/plugins/flipColor and explicitly says it takes effect next start.
Its startup package change is being traced separately. Live rebuild rendering verification
is pending.

### Flip-reset startup patch implemented

flip_patch.py reproduces the complete native TAGame #123ABC output byte-exactly. It edits
eight material floats [R,B,R,G,B,R,G,B], preserving the exact game's default floats for
reset. It recompresses only the affected block with level9 and updates the encrypted
chunk-map size, retaining other compressed blocks and the SHFTRLC1 footer. Preparation
accepts the manifest-verified base or our last installed hash, saves a backup, and writes
atomically. home_launch calls it after the five garage packages.

The user has taken over all live visual testing; computer use is stopped. HTTP delivery
and launch application are verified, while visual effects await the user's report.
/s/gear now waits on an unchanged revision to prevent continuous game polling.

### Live match refresh notification

The user verified initial rendering and reported that HTTP-only changes require a scene
reload. The recovered native patch to RewardDropReceived.GetTotalXPEarned checks its
MatchGUID property for S.Gear:go and fetches /s/gean to update active cars. The addon now
sends that signed Reward/RewardResult v2 notification when Garage state changes. Tests
verify its signature and per-socket revision deduplication. User visual confirmation of
in-match changes is pending. Flip-reset colour is a startup material change as in Ripple RL.

### User verification

On 2026-11-24 the user confirmed the live-refresh fix "now works perfectly". Garage
changes apply in the active match without a lobby/scene reload. This supersedes the
pending live-verification notes above.
