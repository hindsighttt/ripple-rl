"""Guarded startup flip-reset material colour patch for the captured TAGame build."""

import hashlib
import json
import shutil
import struct
import zlib
from pathlib import Path

from Crypto.Cipher import AES

from . import bootstrap, garage_patch, store, upk

# Native material's exact default floats; the UI's #9297FE is rounded.
DEFAULT_VALUES = bytes.fromhex(
    "b50e133f83197f3fb50e133f350f183f83197f3fb50e133f350f183f83197f3f"
)
PHYSICAL_NAMES = 3865  # This build adds a 12-byte generation record to its summary.


def _colour_bytes(rgb):
    r, g, b = ((rgb >> shift & 255) / 255 for shift in (16, 8, 0))
    return struct.pack("<8f", r, b, r, g, b, r, g, b)


def build(base, rgb):
    """Preserve every unaffected compression block, table field and footer."""
    if rgb == -1:
        return base
    if not isinstance(rgb, int) or not 0 <= rgb <= 0xFFFFFF:
        raise ValueError("Invalid flip-reset RGB colour")
    no = PHYSICAL_NAMES
    hs = struct.unpack_from("<I", base, 8)[0]
    if not no < hs <= len(base):
        raise upk.UPKError("Unsupported flip-reset package header")
    encrypted_size = (hs - no) // 16 * 16
    decoded = bytearray(AES.new(upk.DEFAULT_KEY, AES.MODE_ECB).decrypt(base[no:no + encrypted_size]))
    map_position = struct.unpack_from("<I", base, no - 20)[0]
    if map_position + 4 > len(decoded):
        raise upk.UPKError("Invalid flip-reset compression map")
    count = struct.unpack_from("<I", decoded, map_position)[0]
    if not 1 <= count <= 4096 or map_position + 4 + count * 36 > len(decoded):
        raise upk.UPKError("Invalid flip-reset compression records")
    # The material and relocated script tables are in the final mapped chunk.
    entry = map_position + 4 + (count - 1) * 36
    _, raw_size, offset, size = struct.unpack_from("<QiQi", decoded, entry)
    if offset < hs or offset + size > len(base):
        raise upk.UPKError("Invalid flip-reset chunk bounds")
    chunk = base[offset:offset + size]
    raw = bootstrap._decode_chunk(chunk)
    if len(raw) != raw_size or raw.count(DEFAULT_VALUES) != 1:
        raise upk.UPKError("Unsupported flip-reset material revision")
    position = raw.index(DEFAULT_VALUES)
    _, block_size, _, total_size = struct.unpack_from("<IIII", chunk)
    block_index, within = divmod(position, block_size)
    if within + len(DEFAULT_VALUES) > block_size:
        raise upk.UPKError("Flip-reset colour crosses compression blocks")
    blocks_count = (total_size + block_size - 1) // block_size
    cursor = 16 + blocks_count * 8
    blocks = []
    for index in range(blocks_count):
        compressed, uncompressed = struct.unpack_from("<II", chunk, 16 + index * 8)
        encoded = chunk[cursor:cursor + compressed]
        cursor += compressed
        if index == block_index:
            block = bytearray(zlib.decompress(encoded))
            block[within:within + 32] = _colour_bytes(rgb)
            encoded = zlib.compress(bytes(block), 9)  # Verified byte-exact against the reference app.
        blocks.append((encoded, uncompressed))
    header = struct.pack("<IIII", 0x9E2A83C1, block_size, sum(len(b) for b, _ in blocks), total_size)
    changed = header + b"".join(struct.pack("<II", len(b), n) for b, n in blocks) + b"".join(b for b, _ in blocks)
    struct.pack_into("<i", decoded, entry + 20, len(changed))
    result = bytearray(base[:offset])
    result[no:no + encrypted_size] = AES.new(upk.DEFAULT_KEY, AES.MODE_ECB).encrypt(bytes(decoded))
    result.extend(changed)
    result.extend(base[offset + size:])
    expected = raw[:position] + _colour_bytes(rgb) + raw[position + 32:]
    if bootstrap._decode_chunk(changed) != expected:
        raise upk.UPKError("Flip-reset compression verification failed")
    return bytes(result)


def prepare(cooked_dir, patch_dir, backup_dir, rgb):
    """Only replace the captured base or the exact last variant installed by us."""
    target = Path(cooked_dir) / "TAGame.upk"
    source = Path(patch_dir) / "after" / target.name
    manifest = garage_patch.load_manifest(patch_dir)
    base = source.read_bytes()
    base_hash = hashlib.sha256(base).hexdigest()
    if base_hash != manifest["packages"][target.name]["patchedSha256"]:
        raise upk.UPKError("Garage patch data failed flip-reset hash verification")
    current = target.read_bytes()
    current_hash = hashlib.sha256(current).hexdigest()
    state = store.read_json("flip-reset-patch.json", {})
    ours = (state.get("path") == str(target.resolve()) and state.get("baseHash") == base_hash
            and state.get("installedHash") == current_hash)
    if current != base and not ours:
        if rgb == -1:
            return {"changed": False, "skipped": "unknown-package"}
        raise upk.UPKError("TAGame changed outside the rebuild; flip-reset colour was not applied")
    desired = build(base, rgb)
    if current == desired:
        return {"changed": False}
    backup = Path(backup_dir) / "flip-reset" / current_hash / target.name
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(target, backup)
    bootstrap._replace(target, desired)
    info = {"path": str(target.resolve()), "baseHash": base_hash,
            "installedHash": hashlib.sha256(desired).hexdigest(), "colour": rgb,
            "backup": str(backup)}
    store.write_json("flip-reset-patch.json", info)
    return {"changed": True, **info}
