# Rebuild status

## Verified working

- UI: the recovered WebView2 SPA runs unmodified inside QtWebEngine with a `window.chrome.webview`
  shim over a `ripple://` scheme handler. All original bridge commands are dispatched in Python.
- Item swap: UPK header parse → AES-256-ECB decrypt → append target name entry → repoint export
  ObjectName → fix offsets/chunk table → re-encrypt. Output is byte-for-byte identical to the
  original tool for the tested pair (`Antenna_8Ball` ← `Antenna_DollarSign`).
- Backups (`%APPDATA%\RippleRL\backups\<source>\<file>`) and revert.
- Catalog / item filtering (`items.list`, categories, owned).
- Proxy (`mitmproxy`, `127.0.0.1:49321`) overrides:
  - `/s/name` → `<ms><pseudo>`
  - `/s/ping` → fixed / random / real timestamp
  - `/s/sp?` → `,<instanceId>,...`
  - `/s/title`, `/s/sk`, `/s/pfp`
  - everything else tunnels to real PsyNet
- Custom title registration: enabled custom titles are written into the cached
  `PlayerTitleConfig` (title + linked color category) before launch and into a freshly
  fetched config response, re-signed like inventory. `/s/title` then resolves the generated ID.
- Machine-root CA install for Schannel (RL ignores CurrentUser roots).
- Stats API client + roster / ball speed / FPS overlay windows.
- Quick chat hotkeys.

## Open

- Verify in-game name / spawn list with RL using port 49321.
- Paint struct patching (currently only recorded).
- Fake rank wire format (inferred JSON; needs an online-match capture).
- `/s/lo` loadout wire capture (not requested by free play).
- LAN, replay editor, TAS are UI-state only.

## Sample payloads (observed)

`/s/sp?` → `,7ad4...,d38b...,`
`/s/name` (spoof) → `1795468375248TestName`
`/s/ping` (off) → epoch ms; (on) → configured number
`/s/gear`/`/s/gean` → opaque 9,867-byte config blob (passthrough)
