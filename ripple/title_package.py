"""Build an experimental local title-table patch from Ripple's saved definitions."""

import argparse
import json
import os
import re
import struct
import tempfile
import zlib
from pathlib import Path

from . import package_writer as writer, upk


APPLY_EXPORT = 17596
APPLY_SHA256 = "28b652cecba5e2aa669fffc310a29e91a21a5569de0015d56ba7a149bbab7129"
PICKER_EXPORT = 37668
PICKER_SHA256 = "c5ed4be3645359d81aaf6b18195fdaf2f8ee294309400abfa1fb08f8432685eb"
PICKER_RETURN = bytes.fromhex("04 2b 1d 93 00 00 04 3a 23 93 00 00 4c")
SCRIPT_HEADER = struct.Struct("<II")
SCRIPT_HEADER_OFFSET = 40
SCRIPT_OFFSET = SCRIPT_HEADER_OFFSET + SCRIPT_HEADER.size
SCRIPT_END = b"\x04\x0b\x4c"

TITLES = 17586
TITLE_DATA = 162
TITLE_INITIALIZER = 18784
TITLE_ID, TITLE_TEXT, TITLE_COLOR, TITLE_GLOW = 161, 160, 157, 156
FROM_HEX_COLOR = -507
CUSTOM_ID = re.compile(r"CustomTitle_[A-Za-z0-9_]{1,72}", re.ASCII | re.IGNORECASE)
HEX_COLOR = re.compile(r"[0-9a-fA-F]{6}")

# --- EAC-safe TAGame display injection -------------------------------------
# Instead of registering a title in ProjectX.upk (SHA-guarded, so only usable
# under -noeac), we intercept at the TAGame.upk display call sites. TAGame is
# unguarded, so these edits render a custom title with EAC on, client-local.
#
# Every index below was resolved and verified read-only against clean Steam
# and Epic TAGame.upk; the four edited exports are byte-identical across both
# builds (so one SHA baseline per export covers both). Indices are still
# re-verified by object_path() at build time before anything is emitted.
#
# Bytecode shapes were recovered by disassembling these functions:
#   - 0xFE is the native ==(Name, Name) operator (0xFF is !=).
#   - A local/param variable reference is 0x2B <objref>; an instance variable
#     is 0x01 <objref>; a struct-member l-value is 0x35 <member><struct>00 01
#     <inner>; FromHexColor is a final call 0x1C <objref> <string> 0x16.
#   - Jump/JumpIfNot offsets are ABSOLUTE offsets into the in-memory script,
#     where object references occupy 8 bytes (they are 4 bytes on disk), so an
#     injected block's VM size is its byte length plus four per object ref.
# We only ever append before a function's return tail, exactly like the stock
# ProjectX Apply patch above, so no existing jump needs relocating.
SET_PLAYER_TITLE_EXPORT = 38079   # GFxData_PRI_TA.SetPlayerTitle (scoreboard/nameplate)
SET_PLAYER_TITLE_SHA256 = "f756f33fe1f72140b0f6b6cba6c3a886e08d67eed6c1173212d4f966569770de"
# Tail: Return; Nothing; EndOfScript. The function's last acts are three
# unconditional copies Data.Color/GlowColor/Text -> the display fields, so an
# append here runs last and is not clobbered by the stat-title path.
SET_PLAYER_TITLE_TAIL = bytes.fromhex("04 0b 4c".replace(" ", ""))
SET_PLAYER_TITLE_TAIL_VM = 3

GET_TITLE_DATA_EXPORT = 37643     # GFxData_PlayerTitles_TA.GetTitleData (title menu/picker)
GET_TITLE_DATA_SHA256 = "f03e9a0f32949796a2a35e74055b793aa46585d08d77a671a7893aa9da459b70"
# Tail: Return Data; Return ReturnValue; EndOfScript. Both branches of the
# stat-title "if" converge on this Return Data, so appending our override here
# (and leaving that jump pointing at it) runs the override on every path.
GET_TITLE_DATA_TAIL = bytes.fromhex("04 2b 37e20100 04 3a 09930000 4c".replace(" ", ""))
GET_TITLE_DATA_TAIL_VM = 21       # 13 bytes + 4 * 2 object references
GET_TITLE_DATA_LOCAL = 123447     # GFxData_PlayerTitles_TA.GetTitleData.Data (the result struct)
GET_TITLE_ID_PARAM = 37642        # GFxData_PlayerTitles_TA.GetTitleData.TitleId

# GFxData_PRI_TA display fields and the equipped-title param.
XP_TITLE, TITLE_COLOR_FIELD, TITLE_GLOW_FIELD = 37937, 37936, 37935
SET_TITLE_ID_PARAM = 38078        # GFxData_PRI_TA.SetPlayerTitle.TitleId

# ProjectX._Types_X.PlayerTitleData struct and its members. Id must be set so
# the title menu's "selected" tracking (GFxData_PlayerTitles_TA.UpdateSelectedTitle
# does PlayerTitles.Find('Id', EquippedTitle)) can locate our entry — otherwise
# clicking the title in the selector reverts to "no title".
PLAYER_TITLE_DATA = -4308
PTD_ID, PTD_TEXT, PTD_COLOR, PTD_GLOW = -3661, -4464, -4662, -4663

NAME_EQUALS = 0xFE                 # native ==(Name, Name)

_TAGAME_PATHS = {
    SET_PLAYER_TITLE_EXPORT: "GFxData_PRI_TA.SetPlayerTitle",
    GET_TITLE_DATA_EXPORT: "GFxData_PlayerTitles_TA.GetTitleData",
    PICKER_EXPORT: "GFxData_PlayerTitles_TA.GetOnlinePlayerTitles",
    XP_TITLE: "GFxData_PRI_TA.XPTitle",
    TITLE_COLOR_FIELD: "GFxData_PRI_TA.TitleColor",
    TITLE_GLOW_FIELD: "GFxData_PRI_TA.TitleGlowColor",
    SET_TITLE_ID_PARAM: "GFxData_PRI_TA.SetPlayerTitle.TitleId",
    GET_TITLE_ID_PARAM: "GFxData_PlayerTitles_TA.GetTitleData.TitleId",
    GET_TITLE_DATA_LOCAL: "GFxData_PlayerTitles_TA.GetTitleData.Data",
    PLAYER_TITLE_DATA: "ProjectX._Types_X.PlayerTitleData",
    PTD_ID: "ProjectX._Types_X.PlayerTitleData.Id",
    PTD_TEXT: "ProjectX._Types_X.PlayerTitleData.Text",
    PTD_COLOR: "ProjectX._Types_X.PlayerTitleData.Color",
    PTD_GLOW: "ProjectX._Types_X.PlayerTitleData.GlowColor",
}


def _ref(number):
    return struct.pack("<i", number)


def _fname(index, number=0):
    return b"\x21" + struct.pack("<II", index, number)


def _literal(value):
    if value.isascii():
        return b"\x1f" + value.encode("ascii") + b"\x00"
    return b"\x34" + value.encode("utf-16le") + b"\x00\x00"


def _assign_member(member, value, object_refs=0):
    # Same struct assignment as TitleConfig_X.Apply's stock initializers.
    token = b"\x0f\x35" + _ref(member) + _ref(TITLE_DATA) + b"\x00\x01"
    token += b"\x2b" + _ref(TITLE_INITIALIZER) + value
    return token, len(token) + 4 * (3 + object_refs)


def _insert_title(index_token, members):
    # Clone the stock Titles.InsertItem expression. The serialized object refs
    # expand from four to eight bytes in the VM, including for the skip length.
    builder = b"\x54" + b"".join(token for token, _ in members) + b"\x16"
    builder += b"\x2b" + _ref(TITLE_INITIALIZER) + b"\x16"
    argument = index_token + builder
    argument_size = len(argument) + 4 + sum(size - len(token) for token, size in members)
    return b"\x57\x09\x01" + _ref(TITLES) + struct.pack("<H", argument_size) + argument, 13 + argument_size


def _color(value, fallback):
    value = value.strip().lstrip("#")
    if value and not HEX_COLOR.fullmatch(value):
        raise ValueError(f"Expected a six-digit RGB value, got {value!r}")
    return b"\x1c" + _ref(FROM_HEX_COLOR) + _literal(value or fallback) + b"\x16"


def _package_edit(package, number, expected, tail, instructions, extra_names):
    stock = package.export_body(number)
    writer.require(writer.sha256(stock) == expected, "Unsupported title function version")
    vm_size, disk_size = SCRIPT_HEADER.unpack_from(stock, SCRIPT_HEADER_OFFSET)
    end = SCRIPT_OFFSET + disk_size
    insert_at = end - len(tail)
    writer.require(stock[insert_at:end] == tail, "Unexpected title function ending")
    code = b"".join(token for token, _ in instructions)
    sizes = SCRIPT_HEADER.pack(vm_size + sum(size for _, size in instructions), disk_size + len(code))
    body = stock[:SCRIPT_HEADER_OFFSET] + sizes + stock[SCRIPT_OFFSET:insert_at] + code + stock[insert_at:]
    return writer.build(package, [writer.Edit(number, expected, body)],
                        added_names=b"".join(extra_names), name_count=len(extra_names))


def _title_names(package, definitions, extra_names):
    indices = {name.casefold(): index for index, name in enumerate(package.names)}
    seen = set()
    for row in reversed(list(definitions)):
        title_id = row["id"]
        key = title_id.casefold()
        if not CUSTOM_ID.fullmatch(title_id) or key in seen:
            raise ValueError(f"Invalid or duplicate custom title ID {title_id!r}")
        seen.add(key)
        # Unreal folds a canonical numeric suffix into the FName instance number.
        base, _, suffix = title_id.rpartition("_")
        number = 0
        if suffix.isdecimal() and (len(suffix) == 1 or not suffix.startswith("0")) and len(suffix) <= 9:
            title_id, number = base, int(suffix) + 1
        key = title_id.casefold()
        if key not in indices:
            indices[key] = package.name_count + len(extra_names)
            name = title_id.encode("ascii") + b"\x00"
            extra_names.append(struct.pack("<i", len(name)) + name + bytes(8))
        yield row, _fname(indices[key], number)


def build(baseline, definitions, keys):
    """Add title definitions to ProjectX's local title table."""
    package = writer.Package(baseline, keys)
    writer.require(package.object_path(APPLY_EXPORT) == "TitleConfig_X.Apply" and
                   package.object_path(FROM_HEX_COLOR) == "Core.Object.FromHexColor",
                   "Unsupported ProjectX title references")
    instructions = []
    extra_names = []
    for row, name in _title_names(package, definitions, extra_names):
        title_text = row["text"].strip()
        if not title_text or "\x00" in title_text or len(title_text.encode("utf-16le")) > 256:
            raise ValueError(f"Invalid text for {row['id']}")
        members = [
            _assign_member(TITLE_ID, name),
            _assign_member(TITLE_TEXT, _literal(title_text)),
            _assign_member(TITLE_COLOR, _color(row.get("color", ""), "FFFFFF"), object_refs=1),
            _assign_member(TITLE_GLOW, _color(row.get("glow", ""), ""), object_refs=1),
        ]
        instructions.append(_insert_title(b"\x25", members))

    if not instructions:
        raise ValueError("No enabled custom titles to install")
    return _package_edit(package, APPLY_EXPORT, APPLY_SHA256, SCRIPT_END, instructions, extra_names)


def _picker_item(name, index=b"\x26"):
    # Match the picker’s built-in InsertItem(0, GetTitleData('None')).
    # At index one, the stock clear-title entry stays first.
    return b"\x57\x09\x2b" + _ref(37661) + b"\x15\x00" + index + b"\x1c" + _ref(37643) + name + b"\x16\x16", 34


def build_picker(baseline, definitions, keys):
    """Include custom IDs in TAGame's local player title picker."""
    # The Garage writer can relocate the table. This physical header offset is
    # shared by the supported Epic and Steam TAGame builds.
    package = writer.Package(baseline, keys, physical_names=3865)
    writer.require(package.object_path(PICKER_EXPORT) == "GFxData_PlayerTitles_TA.GetOnlinePlayerTitles" and
                   package.object_path(37643) == "GFxData_PlayerTitles_TA.GetTitleData",
                   "Unsupported TAGame title picker references")
    extra_names = []
    items = [_picker_item(name) for _, name in _title_names(package, definitions, extra_names)]
    return _package_edit(package, PICKER_EXPORT, PICKER_SHA256, PICKER_RETURN, items, extra_names)


def _fromhex(from_hex, value, fallback):
    """FromHexColor("RRGGBB") call, matching the stock ProjectX colour builder."""
    value = value.strip().lstrip("#")
    if value and not HEX_COLOR.fullmatch(value):
        raise ValueError(f"Expected a six-digit RGB value, got {value!r}")
    return b"\x1c" + _ref(from_hex) + _literal(value or fallback) + b"\x16"


def _title_text(row):
    text = row["text"].strip()
    if not text or "\x00" in text or len(text.encode("utf-16le")) > 256:
        raise ValueError(f"Invalid text for {row['id']}")
    return _literal(text)


def _struct_dest(member):
    # l-value Data.<member> of the function's local PlayerTitleData struct.
    return (b"\x35" + _ref(member) + _ref(PLAYER_TITLE_DATA) + b"\x00\x01"
            + b"\x2b" + _ref(GET_TITLE_DATA_LOCAL))


def _display_inner(row, name, from_hex, site):
    """Body of one "if (TitleId == name) { <assign display> }" block, without
    its leading JumpIfNot. Returns (bytes, object_reference_count)."""
    text, colour, glow = _title_text(row), row.get("color", ""), row.get("glow", "")
    if site == "pri":
        param = SET_TITLE_ID_PARAM
        lets = [
            (b"\x0f" + b"\x01" + _ref(XP_TITLE) + text, 1),
            (b"\x0f" + b"\x01" + _ref(TITLE_COLOR_FIELD) + _fromhex(from_hex, colour, "FFFFFF"), 2),
            (b"\x0f" + b"\x01" + _ref(TITLE_GLOW_FIELD) + _fromhex(from_hex, glow, ""), 2),
        ]
    else:  # "menu": write the members of the function's local result struct.
        param = GET_TITLE_ID_PARAM
        lets = [
            (b"\x0f" + _struct_dest(PTD_ID) + name, 3),  # Data.Id = <ourID> so Find('Id', ...) matches
            (b"\x0f" + _struct_dest(PTD_TEXT) + text, 3),
            (b"\x0f" + _struct_dest(PTD_COLOR) + _fromhex(from_hex, colour, "FFFFFF"), 4),
            (b"\x0f" + _struct_dest(PTD_GLOW) + _fromhex(from_hex, glow, ""), 4),
        ]
    condition = bytes([NAME_EQUALS]) + b"\x2b" + _ref(param) + name + b"\x16"
    inner = condition + b"".join(token for token, _ in lets)
    refs = 1 + sum(count for _, count in lets)  # the compared param + each assign
    return inner, refs


def _display_instruction(named_rows, insert_vm, from_hex, site):
    """All title override blocks, with absolute JumpIfNot targets filled in.

    Each block is ``07 <abs-target> <condition> <assignments>``. A false
    condition jumps past the block to the next one (or to the return tail for
    the last), so independent custom IDs never interfere."""
    blocks = []
    for row, name in named_rows:
        inner, refs = _display_inner(row, name, from_hex, site)
        blocks.append((inner, 3 + len(inner) + 4 * refs))  # + 07 + WORD target
    code = b""
    position = insert_vm
    total_vm = 0
    for inner, block_vm in blocks:
        position += block_vm
        if position > 0xFFFF:
            raise ValueError("Custom titles exceed the title function jump range")
        code += b"\x07" + struct.pack("<H", position) + inner
        total_vm += block_vm
    return code, total_vm


def _display_edit(package, number, expected, tail, tail_vm, named_rows, from_hex, site):
    stock = package.export_body(number)
    writer.require(writer.sha256(stock) == expected, "Unsupported title function version")
    vm_size, disk_size = SCRIPT_HEADER.unpack_from(stock, SCRIPT_HEADER_OFFSET)
    end = SCRIPT_OFFSET + disk_size
    insert_at = end - len(tail)
    writer.require(stock[insert_at:end] == tail, "Unexpected title function ending")
    code, total_vm = _display_instruction(named_rows, vm_size - tail_vm, from_hex, site)
    sizes = SCRIPT_HEADER.pack(vm_size + total_vm, disk_size + len(code))
    body = stock[:SCRIPT_HEADER_OFFSET] + sizes + stock[SCRIPT_OFFSET:insert_at] + code + stock[insert_at:]
    return writer.Edit(number, expected, body)


def _picker_edit(package, named_rows):
    stock = package.export_body(PICKER_EXPORT)
    writer.require(writer.sha256(stock) == PICKER_SHA256, "Unsupported title function version")
    vm_size, disk_size = SCRIPT_HEADER.unpack_from(stock, SCRIPT_HEADER_OFFSET)
    end = SCRIPT_OFFSET + disk_size
    insert_at = end - len(PICKER_RETURN)
    writer.require(stock[insert_at:end] == PICKER_RETURN, "Unexpected title function ending")
    items = [_picker_item(name) for _, name in named_rows]
    code = b"".join(token for token, _ in items)
    sizes = SCRIPT_HEADER.pack(vm_size + sum(size for _, size in items), disk_size + len(code))
    body = stock[:SCRIPT_HEADER_OFFSET] + sizes + stock[SCRIPT_OFFSET:insert_at] + code + stock[insert_at:]
    return writer.Edit(PICKER_EXPORT, PICKER_SHA256, body)


def _from_hex_color(package):
    for index in range(1, package.import_count + 1):
        if package.object_path(-index) == "Core.Object.FromHexColor":
            return -index
    raise writer.PackageError("Core.Object.FromHexColor import not found")


def build_tagame(baseline, definitions, keys):
    """Render custom titles by editing only TAGame.upk (EAC-safe, local-only).

    Overrides the local player's title fields on the scoreboard
    (GFxData_PRI_TA.SetPlayerTitle) and the resolved data in the title menu
    (GFxData_PlayerTitles_TA.GetTitleData), and lists the custom IDs in the
    picker (GetOnlinePlayerTitles). ProjectX.upk is never touched.
    """
    package = writer.Package(baseline, keys, physical_names=3865)
    for ref, path in _TAGAME_PATHS.items():
        writer.require(package.object_path(ref) == path,
                       f"Unsupported TAGame title reference: {path}")
    from_hex = _from_hex_color(package)
    extra_names = []
    named_rows = list(_title_names(package, definitions, extra_names))
    if not named_rows:
        raise ValueError("No enabled custom titles to install")
    edits = [
        _display_edit(package, SET_PLAYER_TITLE_EXPORT, SET_PLAYER_TITLE_SHA256,
                      SET_PLAYER_TITLE_TAIL, SET_PLAYER_TITLE_TAIL_VM, named_rows, from_hex, "pri"),
        _display_edit(package, GET_TITLE_DATA_EXPORT, GET_TITLE_DATA_SHA256,
                      GET_TITLE_DATA_TAIL, GET_TITLE_DATA_TAIL_VM, named_rows, from_hex, "menu"),
        _picker_edit(package, named_rows),
    ]
    return writer.build(package, edits, added_names=b"".join(extra_names), name_count=len(extra_names))


def _saved_state(path):
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def _saved_titles(state):
    return [row for row in state.get("titles", []) if row.get("custom")] if state.get("enabled", True) else []


def _write_atomic(path, data):
    fd, temporary = tempfile.mkstemp(prefix="ripple-titles-", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def _install_state(package, backup_dir):
    identity = os.path.normcase(str(package.resolve()))
    directory = backup_dir / writer.sha256(identity.encode("utf-8"))
    return directory / "active.json"


def _restore_package(package, backup_dir):
    state_file = _install_state(package, backup_dir)
    if not state_file.is_file():
        return "inactive"
    state = json.loads(state_file.read_text(encoding="utf-8"))
    current_hash = writer.sha256(package.read_bytes())
    if current_hash != state["baseline"]:
        writer.require(current_hash == state["patched"], f"{package.name} changed after the title patch was installed")
        baseline = (state_file.parent / (state["baseline"] + ".upk")).read_bytes()
        writer.require(writer.sha256(baseline) == state["baseline"], "Custom title backup hash mismatch")
        _write_atomic(package, baseline)
    state_file.unlink()
    return "restored"


def restore(projectx, backup_dir):
    restored = [_restore_package(package, backup_dir)
                for package in (projectx, projectx.with_name("TAGame.upk"))]
    return "restored" if "restored" in restored else "inactive"


def _install_package(package, backup_dir, baseline, result):
    state_file = _install_state(package, backup_dir)
    writer.require(not state_file.exists(), "Restore the previous title patch before preparing another launch")
    writer.require(package.read_bytes() == baseline, f"{package.name} changed while preparing the title patch")
    original_hash = writer.sha256(baseline)
    state_file.parent.mkdir(parents=True, exist_ok=True)
    _write_atomic(state_file.parent / (original_hash + ".upk"), baseline)
    journal = json.dumps({"baseline": original_hash, "patched": writer.sha256(result)}).encode("utf-8")
    _write_atomic(state_file, journal)
    _write_atomic(package, result)


def prepare(projectx, titles, backup_dir, keys):
    # EAC-safe path: patch only TAGame.upk (display call sites + picker). The
    # ProjectX.upk registry patch (build/build_picker) is retained above as the
    # legacy -noeac fallback but is no longer installed here.
    rows = _saved_titles(_saved_state(titles))
    if not rows:
        return "inactive"
    tagame = projectx.with_name("TAGame.upk")
    tagame_stock = tagame.read_bytes()
    tagame_patch = build_tagame(tagame_stock, rows, keys)
    _install_package(tagame, backup_dir, tagame_stock, tagame_patch)
    return "installed"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("projectx", type=Path, help="input ProjectX.upk")
    parser.add_argument("titles", type=Path, help="Ripple feature_titles.json")
    parser.add_argument("output", type=Path, help="new package path, or backup directory in launch mode")
    parser.add_argument("--data-dir", type=Path, default=Path(__file__).resolve().parents[1] / "data")
    parser.add_argument("--launch-mode", choices=("restore", "install"))
    args = parser.parse_args()
    try:
        if args.launch_mode == "restore":
            print(restore(args.projectx, args.output))
            return
        if args.launch_mode:
            print(prepare(args.projectx, args.titles, args.output, upk.load_keys(args.data_dir)))
            return
        rows = _saved_titles(_saved_state(args.titles))
        result = build(args.projectx.read_bytes(), rows, upk.load_keys(args.data_dir))
        with args.output.open("xb") as file:
            file.write(result)
    except (OSError, ValueError, KeyError, TypeError, struct.error, zlib.error) as error:
        parser.exit(1, f"{error}\n")
    print(f"Built {args.output} with {len(rows)} custom title definitions.")


if __name__ == "__main__":
    main()
