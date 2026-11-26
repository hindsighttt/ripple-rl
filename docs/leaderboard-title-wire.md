# Leaderboard title wire — Ripple RL reference capture (2026-11-24)

Captured from ORIGINAL Ripple RL (RL connected, in a match, title "Psyonix Hackathon Winner"
spawned+equipped), curling Ripple RL's local server on 127.0.0.1:49320.

## /s/title real format

    PsyonixHackathonWinner|B1:ARUBAAAA1MoBAAAAAAAAAAAAAAAAAAAAAAA=

Format is **`<TitleID>|B1:<base64 blob>`** — NOT the `SPacket1:{"Title","Leader","Loadout"}`
JSON the rebuild currently serves (that string is only a static template in the binary).
- Prefix: the plaintext TitleID (matches data/titles.json key).
- `B1:` + base64 of a 26-byte blob: `01 15 01 00 00 00 d4 ca 01 00 00*16`
  - byte0 `01` = version
  - byte1 `0x15` = 21 = Player Title slot index (product 3036 is Slot Index 21)
  - `01 00 00 00` = count u32 = 1
  - `d4 ca 01 00` = u32 LE **117460** = a per-title numeric id (NOT in our data; not a
    crc32/adler/md5/fnv of the title string — checked)
  - trailing 16 bytes zero = likely a 16-byte InstanceID (zeros here)

Other endpoints while in-match: /s/sp, /s/gear, /s/lo, /s/pk all EMPTY; /s/name works.
So /s/title alone carries the leaderboard title; the injected plugin polls it in-match.
Ripple RL serves it only once RL is actually connected (empty "|" when disconnected).

## Open question (needs a 2nd capture)
Whether the 117460 u32 is per-title (needs a titleId->id map, source unknown; maybe
reference-runtime.bin or RL's title table) or effectively constant (then reuse the blob verbatim).

## RESOLVED (2026-11-24): blob is CONSTANT
Captured a 2nd title ("S23_2v2_TopRank_100", different color) — the 26-byte blob is
byte-identical to title #1 (`011501000000d4ca01000000...`). So 117460 is a fixed value,
not per-title; only the plaintext TitleID prefix changes. Title color is rendered by RL
from the title itself, not carried on the wire.

### Fix implemented
`ripple/social.py` `title_response()` now returns `"<TitleID>|B1:<const blob>"`
(constant `TITLE_BLOB = "B1:ARUBAAAA1MoBAAAAAAAAAAAAAAAAAAAAAAA="`). `active_title_id()`
picks the title to display: an explicit custom-title selection if set, else the most-recently
spawned title (spawn list). Tests in `tests/test_title_wire.py` (6, all pass). Still to do:
live-verify that the rebuilt game's plugin shows it on the leaderboard with the corrected wire.
