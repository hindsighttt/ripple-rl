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
