# Ripple RL package patches

The native rebuild generates the supported Epic Garage package set from a shipped
[edit recipe](../data/garage-recipe.json) and the baseline game files. Original
Ripple RL's SSP3 manifests supplied the edit data during recipe authoring. The
[offline writer](../ripple/package_writer.py) can also replace chosen
exports and replay supported SSP3 groups. Feature-level selection still requires
understanding which exports and references belong to the feature.

This investigation used the captured files offline. It did not modify the installed game
or launch Rocket League.

## Inputs and provenance

- [Recovered resources](recovered-native/) contains Main (`sp.bin`), Lite (`sp_lite.bin`),
  and Legacy (`sp_legacy.bin`). The [resource audit](feature-audit.md) records their
  decoded SHA-256 hashes and where they were found in the original application artifact.
- `%APPDATA%\RippleRL\backups\ground-truth\{before,after}\` contains five paired
  packages from the reference app. The adjacent `patch-manifest.json` records their exact
  hashes. Its `vanillaSha256` is the input accepted by the rebuild installer. For
  `GFX_PremiumContainerMenu_SF.upk`, that input already contains inactive copies left by
  previous edits, so it should not be treated as a fresh vendor install.
- [The SSP3 reader](../ripple/patches.py) parses manifests and decodes their
  copy/insert deltas. [The launch path](../backend-cpp/src/Home.cpp) calls
  [the Garage installer](../backend-cpp/src/Garage.cpp). The supported Epic recipe
  generates and verifies the replacement from a backed-up baseline. Legacy targets
  still use the captured-package installer when matching patch data is present.
- The [native Garage installer](../backend-cpp/src/Garage.cpp) edits a known
  material in the patched `TAGame.upk` base to change the flip-reset color. The
  [ProjectX package code](../ripple/bootstrap.py) is a separate, specialized package
  reader and writer. Main and Lite have no ProjectX records.

## SSP3 container and deltas

Integers are little-endian. The header is the four-byte magic `SSP3` followed by a
32-bit package-group count. Each group contains, in order:

1. A 16-bit UTF-8 name length followed by the package name.
2. A 32-bit replacement count. A replacement has two 24-byte descriptors, a one-byte
   mode, and a 32-bit payload length followed by the delta.
3. A 32-bit table-edit count. Each edit has a 32-bit offset, a 32-bit length, and two
   byte arrays of that length containing the expected input and replacement.
4. A 32-bit added-name count and a length-prefixed name blob.
5. A 32-bit added-import count and a length-prefixed import blob. The observed import
   records are 28 bytes each.
6. A 32-bit added-export count. Each entry has a length-prefixed header and a
   length-prefixed body. Every observed added-export header is 72 bytes.

A nonempty delta begins with a 32-bit operation count. Opcode 0 reads a 32-bit source
offset and a 32-bit length, then copies that range from the original export body.
Opcode 1 reads a 32-bit length and appends that many literal bytes. Concatenating the
results gives the new export body. The reader checks record boundaries, opcode validity,
and source ranges.

| Manifest | Group records | Distinct package names | Replacements | Empty deltas | File bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Main | 5 | 5 | 117 | 0 | 481,423 |
| Lite | 5 | 5 | 80 | 0 | 294,537 |
| Legacy | 149 | 15 | 4,980 | 4,946 | 1,019,916 |

All nonempty payloads in the three inputs parse to their declared ends. Preserve group
order and repeated package names. Indexing Legacy by package name would collapse its
149 groups to 15. Empty Legacy records remain unresolved.

## Resolving export replacements

Each descriptor splits into four fields with `<IQIQ`. Use the first field of descriptor
A minus one as the export-table index for the captured Main inputs. Its third field is
the source body size. The third field of descriptor B is the decoded output length.
These relationships hold for all 117 replacements, including three whose source body
has length zero. All 117 decoded bodies match the active output at the *same* export
index in the corresponding captured package, and each body has a unique exact match
among its active exports.

Descriptor B's first field coincides with the zero-based output index in 111 of 117
records. Six exceptions include the `GFxShell_SF.upk` CodeLibrary and five TAGame exports.
Neither descriptor's address field is a reliable write location. For example, Main
TAGame record #34 describes the source size as 788 and the output size as 1,708. Its
actual source body begins at virtual address `0x9d550e0` and the final active output
begins at `0xa7c8759`, as read from the respective export tables.

The meaning of both 64-bit fields is still unresolved as a general format contract.
Use the actual tables, source package and export hashes, and unique object paths to
validate a revision. Ordinals in this document are specific to the captured build.
Mode 2 decodes with the same copy/insert reader, but the observed Main output stores
its single mode-2 body before records #99–#106 even though it is record #107 in the
manifest. Its full meaning and the general ordering rule are unverified.

## Rebuilding an Unreal package

Keep three address spaces separate: physical file offsets, virtual package addresses in
export tables, and offsets relative to the decrypted header payload. The compression
map relates physical compressed ranges to virtual uncompressed ranges. The existing
[package reader](../ripple/bootstrap.py) demonstrates mapping a serial address
to a compressed chunk. Its ProjectX-specific assumptions need to be checked before
reusing them for another package.

The Main capture gives the following replay evidence:

| Package | Exact active replacements | Header-relative edits | Added names / imports / exports |
| --- | ---: | ---: | ---: |
| `TAGame.upk` | 108 | 46 | 83 / 2 / 211 |
| `GFxShell_SF.upk` | 1 | 0 | 0 / 0 / 0 |
| `GFX_PremiumContainerMenu_SF.upk` | 1 | 0 | 0 / 0 / 0 |
| `Engine.upk` | 6 | 0 | 0 / 0 / 0 |
| `GFX_Hud_SF.upk` | 1 | 0 | 0 / 0 / 0 |

1. Read the baseline header and its compression map. Read names and imports, then
   resolve the export object and outer names. Read the export body using its serial
   size and virtual offset at bytes 32–39 of the export record. Existing export
   entries can have a variable tail. The captured records use a 72-byte base plus
   four bytes per entry indicated by the count at byte 48.
2. Apply the table edits to the **decrypted header payload**. In the TAGame input, its
   physical start is `0xf19`. All 46 expected byte sequences matched at their SSP3
   offsets measured from that start. The edits touch two name records, six import
   records, and three export headers. Reading the same numbers as file or virtual
   addresses gives the wrong bytes.
3. Reconstruct each replacement using its original export body and append the new
   body. Update the active export's serial size and virtual offset. The old body
   can remain in the compressed data without being referenced by the export table.
   All five captures preserve the original decompressed chunk data as a prefix.
   Changes to chunk data were confined to the final mapped chunk. The other decoded
   chunks matched exactly.
4. Append new records in their declared order and assign final serial sizes and
   offsets to their bodies. The replayed TAGame name and import tables matched
   exactly. All 211 added export bodies matched exactly. With the serial size and
   address fields supplied, all 211 added export headers and every existing export
   header in all five packages matched. No extra metadata differences were needed
   to explain these headers.
5. Emit the rebuilt compression map and summary. In the captured TAGame output, the
   active import, name, and export tables are relocated to the end of the final
   decompressed chunk, in that order. The encrypted physical header still begins
   at `0xf19`. The original table area is still present, so reading the summary's
   logical name pointer as an encryption offset fails. The other four packages
   retain their table starts and change the relevant export entries in place.

The writer replayed the five Main groups against the captured before-packages.
The emitted packages reopened successfully. All 117 active replacement bodies,
211 added bodies, name and import tables, and export records matched the captured
after-packages, allowing for different serial offsets. The writer preserves the
original compressed data, appends a replacement final chunk, and points the chunk
map at it. Physical layout, compression, and whole-file hashes can differ from
the reference app's output. The replayed captures were checked offline. A separate
writer-generated Standard boost package was validated in the live Garage preview.

## Using the offline writer

Run [the CLI](../tools/package_writer.py) from this repository with Python. Give it
a scratch output path in an existing folder. Outputs must use a new filename.

```powershell
python -m tools.package_writer inspect $baseUpk --find $objectName
python -m tools.package_writer inspect $baseUpk --export $exportNumber
python -m tools.package_writer extract $baseUpk --export $exportNumber --output $originalBody
python -m tools.package_writer replace $baseUpk --base-sha256 $baseHash --edit $exportNumber $exportHash $modifiedBody --output $resultUpk
```

`inspect` prints the package hash, the physical header offset, and the selected
export's path and hash. Use those hashes from the intended input. Repeat `--edit` to
replace several existing export bodies together. The writer checks the input hashes,
rebuilds the package, and checks the reopened result before publishing it. An
authored body must use the input package's name and object indices.

```powershell
python -m tools.package_writer replay $baseUpk --base-sha256 $baseHash --manifest $ssp3 --manifest-sha256 $manifestHash --output $resultUpk
```

`replay` applies the complete group whose name matches the input filename, including
its table edits and additions. It has been verified against Main and rejects empty
deltas and ambiguous groups. For a package with relocated name, import, or export
tables, use `--header-offset` with the physical offset printed for the baseline.
For example, use `--header-offset 0xf19` to reopen the captured patched TAGame package.
The Python writer also accepts explicit table edits and added records when building
a custom plan. It operates on serialized bodies and tables. Compiling UnrealScript
or editing a Scaleform movie is a separate authoring step.

### Restoring the Epic Garage patches after file verification

Use **Settings → Maintenance → Garage patch → Re-apply** with Rocket League closed.
Starting the game from the updated Ripple RL build also applies the Garage package set.
The five-package Garage set has its own controls. The Standard boost test in
**Package patches** does not affect this recovery.

On the native path, the recipe uses `data/garage-recipe.json`. Baseline backups
and generated package cache files are under
`%APPDATA%\RippleRL\backups\garage-recipe-epic-20261126`. The first run can start
with that directory absent. It does not need the reference app, the recovered
`sp.bin`, or a captured patched UPK. Recognized older patched packages can stay
installed until a clean baseline becomes available after verification.

After verification, `TAGame.upk` and `GFX_PremiumContainerMenu_SF.upk` differed
from the captured input hashes. The Main SSP3 deltas replayed against the new
inputs with exact matches for all affected original exports and all patched and
added export bodies. The regenerated TAGame preserves the verified game's
`CarColors.BlueTeamV3` and `CarColors.OrangeTeamV3` data. The other three packages
use the matching the reference app captures.

If a future game update changes a recorded package hash, the backend reports
`unknown` and leaves that file alone. Replaying on a new baseline requires comparing
the original affected export bodies, table identities, and reconstructed outputs
against the trusted capture before registering the new hashes.

### Managing patches in Ripple RL

In the C++ app, open **Settings → Maintenance → Package patches**. Select a recipe,
then choose **Apply now** while Rocket League is closed, or launch from Ripple RL to
apply the selection. Clear the checkbox and apply the selection again to restore the
original package. Both boost recipes are off by default.

The native backend reads [the recipe manifest](../data/package-patches.json).
Each recipe identifies the exact package SHA-256, export number and object path,
source and result export hashes, and the byte splices to apply. Selected recipes are
combined from the verified original, sorted by export number. If two recipes select
the same export, saving the selection fails before any package is changed.

Native installation stages and verifies the baseline and generated variant before
replacing a file. The embedded Python writer builds the package, and the native
backend owns the final install and restore. Backups and recognized variants are
stored under `%APPDATA%\RippleRL\backups\package-patches\`, scoped to the install
and baseline hash. If a file matches neither the declared baseline nor a verified
variant, Ripple RL skips it. The Maintenance panel shows the detected package state.
This workflow currently accepts one verified baseline per package in the manifest.

Native IPC verification covered enabling, building and applying the selected
recipe, persisting it across a backend restart, applying it again without another
write, and restoring it after disabling. The manager skipped an unsupported input
and refused to edit a supported package while Rocket League was running. A run
against the Epic install produced the exact package used in the live preview below
and restored the original hash afterward.

### Standard boost smoke test

Use the Garage's **Rocket Boost → Standard** preview so a test does not require
equipping an item or entering a match. The installed baseline
`Boost_Standard_SF.upk` had SHA-256
`fb0cbc69a9f5bbd6987ca65f72b1fcbdb918d5415aeb8cda9db2b2411f33f4a9`.
Back up and hash the exact file before changing it. Launch through Epic between
variants so each preview uses a fresh game process.

The first variant changed just the `CustomColor` RGB floats at export #67 byte
`0x490`. The game reached the Garage with the variant installed, but the Standard
plume still appeared orange. That check did not establish which particle parameter
drives the visible plume. The original file was restored and matched the baseline
hash.

The second variant targets export #179, `Boost_Standard.FX.Boost_Painted_PS`. The
`Emitters` array at export-relative offset `0x48` has one object reference to
export #177. The test body replaces the eight-byte array payload with a four-byte
zero count and updates the property length. The writer reopened the result and
verified that only the selected export changed. In a fresh Epic launch, the
unpainted Standard boost preview had no smoke plume. The original package was
restored and its SHA-256 matched the baseline. That verifies a length-changing
export replacement in the live game. The rollback was verified on disk.

### Authoring a separate cosmetic recipe

The experimental **Hide Standard Blue boost plume** option was built from a clean
`Boost_Standard_Blue_SF.upk`. Inspect the target package and find the particle
system through the Blue boost actor's particle component template. It points to
export #164, `Boost_Standard.FX.Boost_PS`, while the drive component points to
export #165. In #164, the `Emitters` property at offset `0x48` references export
#162. The recipe records a splice at `0x58` that sets the array count to zero
and reduces the property payload from eight to four bytes.

To author another recipe, inspect and extract the target export, edit the copy,
and calculate the baseline package hash and the original and edited export
hashes. Record the export path and minimal byte splices in
[`data/package-patches.json`](../data/package-patches.json). Reopen the generated
package and compare the other exports before testing the checkbox on a clean
copy. Enable and disable the options independently, checking the baseline hash
after each restore.

The Standard Blue output was reopened and checked against all 364 original
exports. Only #164 changed. Native testing on clean copies applied both boost
recipes, disabled each independently, and restored both original package hashes.
All five Garage packages kept their original hashes during that test. The visible
effect still needs a Garage preview of **Standard Blue**, which uses a different
package from the unpainted Standard item.

## Old roulette

Main patch ordinals below are zero-based within each package group. Unreal export
numbers in this table are one-based.

| Package | Record or export | Verified change |
| --- | --- | --- |
| `TAGame.upk` | Main #34 / Lite #20, export 35546, `GFxData_MTXGarage_TA.HandleUnlockContainersResponse` | Active body grows from 788 to 1,708 bytes and gains `S.Drops.v1.roulette`. The Main and Lite deltas are identical. |
| `TAGame.upk` | Added export 136577, `S_State.Gear` | The Main capture adds this 21,012-byte body containing a second reference to the same key. It is a shared state export. |
| `GFX_PremiumContainerMenu_SF.upk` | Main #0 / Lite #0, export 6, `GFX_PremiumContainerMenu.PremiumContainerMenu` | Active body grows from 1,037,690 to 1,038,882 bytes and gains `S.Drops.v1.roulette`, `sRouletteState`, and `sRouletteJoin`. The active baseline body already has `bSkipRoulette`. Main and Lite carry the same delta. |

The active menu movie is a shared export. The manifest carries one delta for the
whole movie, including garage UI changes. Skipping this record would restore the old
movie as a whole, including its garage UI. The captured output contains two copies of
the same new body appended past the baseline chunk. Export 6 points to the second.
Counting strings across the entire compressed package would mix active code with
inactive copies. Likewise, Lite is unsuitable as a roulette-off comparison because
it carries both roulette-related replacement deltas. Its second added export also contains the key, although its body is 18,884 bytes compared with 21,012 bytes in Main.

The captured the reference app preferences include `drops\rouletteOn=true`. The active
`S_State.Gear` body reads a one-character field at zero-based offset 714 of the
9,867-byte game configuration. This is the same substring operation used for the
known underglow and light fields. The rebuild's C++ menu now saves `roulette.on`
through native IPC, and the existing configuration serializer writes `1` or `0` at
that offset and advances the configuration revision on a change. This keeps the shared
exports intact. A live on/off drop-opening comparison is still pending.

Underglow is another example of cross-feature overlap. In Main TAGame, #20 changes
`FXActor_Car_TA.HandleTeamChanged`. #28 changes
`GFxData_Garage_TA.SetPreviewCustomColor` and overlaps palette behavior. #77 and #81
change the player and menu tick functions and carry several garage settings. These
are export replacements, so a patch ordinal is too coarse to serve as a feature ID.

## Adding or removing a feature

- **Establish the base.** Identify the precise build by package hash. Record each
  affected package, full object path, expected export hash, and the source/output
  positions for the analysis. Use the same base for full-versus-subset comparisons.
- **Map the behavior to active exports.** Resolve the changed serial bodies and any
  referenced names, imports, and exports. For a setting, compare captures with the
  setting on and off while holding build and other settings constant. Use separate
  copies of the same initial packages for each capture so an earlier run cannot
  leave a second appended body. If a setting only changes shared state, test that
  path as well as comparing package bodies.
- **Select a patch at the right granularity.** A dedicated export can be restored from
  the baseline after its references are checked. A shared movie or script needs a
  smaller edit or a verified state change. For roulette, compare the key consumer in
  the menu movie and the two TAGame call sites before removing anything. Keep shared
  table entries stable unless a reference-aware remap is part of the build.
- **Author the changed export.** Given two known export bodies, the copy/insert
  representation can carry the difference. A literal-only delta is sufficient to
  represent a complete replacement in the existing decoder. New Unreal references
  still require consistent tables and indices. The original authoring toolchain,
  descriptor fingerprint algorithm, and native acceptance rules were not recovered.
  An independent builder can validate the input and output with its own package and
  export hashes.
- **Validate in a scratch directory.** First replay the full known capture as a
  reference. Then reopen the subset output and compare its active exports with both
  baselines. Check that the chosen exports changed, the unrelated active exports
  remain equivalent, and all referenced table entries resolve. For roulette, a
  controlled runtime check should cover drop opening and the garage tab together.

There is no need to derive new patches from prior patched files when the verified
baseline is available. Retaining a base package, a full replay fixture, and the
on/off capture for each feature makes later additions and removals easier to verify.
