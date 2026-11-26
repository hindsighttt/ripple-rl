"""Prepare the two guarded ProjectX functions used by the local proxy."""

import hashlib
import math
import os
import shutil
import struct
import tempfile
import zlib
from pathlib import Path

from Crypto.Cipher import AES

from . import upk, store


GUARDS = {
    "PsyNetConnection_X.GetPsyNetURL": "f3c9120da415906a138ad2a125013108dc2e85125f65f0b2c4b34db99e4e38df",
    "PsyNetMessengerWebSocket_X.Init": "859c94b1fe6b5d5be301da415d7f94631c0ae9cf92ab9a82155c12a9c242eec1",
}


class ProjectX:
    def __init__(self, data, keys):
        self.data = data
        self.header, decoded, self.key, _ = upk.try_decrypt(data, keys)
        self.decoded = bytearray(decoded)
        h = self.header
        self.export_positions = []
        cursor = h.eo - h.no
        for _ in range(h.ec):
            if cursor < 0 or cursor + 72 > len(decoded):
                raise upk.UPKError("Truncated script-package export table")
            net_count = struct.unpack_from("<i", decoded, cursor + 48)[0]
            if not 0 <= net_count <= 65536:
                raise upk.UPKError("Invalid script-package export net count")
            self.export_positions.append(cursor)
            cursor += 72 + net_count * 4
        if cursor + h.no != h.dep:
            raise upk.UPKError("Unsupported script-package export table boundary")
        self.map_offset = h.no + struct.unpack_from("<I", data, h.no - 20)[0]
        count = struct.unpack_from("<I", decoded, self.map_offset - h.no)[0]
        if not 1 <= count <= 4096:
            raise upk.UPKError("Invalid ProjectX chunk map")
        self.records = []
        self.regions = []
        for index in range(count):
            record = decoded[self.map_offset - h.no + 4 + index * 36:][:36]
            start, size, compressed_offset, compressed_size = struct.unpack_from("<QiQi", record)
            chunk = data[compressed_offset:compressed_offset + compressed_size]
            raw = _decode_chunk(chunk)
            if len(raw) != size:
                raise upk.UPKError("ProjectX mapped chunk length mismatch")
            self.records.append(record)
            self.regions.append((start, raw))
        self.names = []
        cursor = 0
        for _ in range(h.nc):
            length = struct.unpack_from("<i", decoded, cursor)[0]
            if not 1 <= length <= 65536:
                raise upk.UPKError("Invalid ProjectX name")
            self.names.append(decoded[cursor + 4:cursor + 4 + length - 1].decode("latin1"))
            cursor += length + 12
        if cursor + h.no != h.io:
            raise upk.UPKError("ProjectX name table boundary mismatch")
        self.exports = {}
        for index in range(h.ec):
            self.exports[self.object_path(index + 1)] = index

    def object_path(self, index):
        parts = []
        seen = set()
        h = self.header
        while index:
            if index in seen:
                raise upk.UPKError("Cyclic ProjectX object path")
            seen.add(index)
            if index < 0:
                entry = h.io - h.no + (-index - 1) * 28
                name_offset, parent_offset = 20, 16
            else:
                entry = self.export_positions[index - 1]
                name_offset, parent_offset = 12, 8
            name = struct.unpack_from("<i", self.decoded, entry + name_offset)[0]
            parts.append(self.names[name])
            index = struct.unpack_from("<i", self.decoded, entry + parent_offset)[0]
        return ".".join(reversed(parts))

    def export_position(self, name):
        return self.export_positions[self.exports[name]]

    def export(self, name):
        entry = self.export_position(name)
        size, offset = struct.unpack_from("<ii", self.decoded, entry + 32)
        if size == 0:
            return b""
        for start, raw in self.regions:
            if start <= offset and offset + size <= start + len(raw):
                return raw[offset - start:offset - start + size]
        raise upk.UPKError("Unmapped ProjectX export " + name)


def _decode_chunk(chunk):
    if len(chunk) < 16:
        raise upk.UPKError("Missing ProjectX compressed chunk")
    tag, block_size, compressed_size, size = struct.unpack_from("<IIII", chunk)
    if tag != 0x9E2A83C1 or not 1 <= block_size <= 1048576:
        raise upk.UPKError("Unsupported ProjectX compressed chunk")
    count = math.ceil(size / block_size)
    cursor = 16 + count * 8
    result = bytearray()
    for index in range(count):
        compressed, uncompressed = struct.unpack_from("<II", chunk, 16 + index * 8)
        raw = zlib.decompress(chunk[cursor:cursor + compressed])
        if len(raw) != uncompressed:
            raise upk.UPKError("ProjectX compression block length mismatch")
        result.extend(raw)
        cursor += compressed
    if cursor != len(chunk) or len(result) != size or compressed_size != len(chunk) - 16 - count * 8:
        raise upk.UPKError("ProjectX compressed chunk boundary mismatch")
    return bytes(result)


def _encode_chunk(data):
    block_size = 131072
    blocks = [data[offset:offset + block_size] for offset in range(0, len(data), block_size)]
    compressed = [zlib.compress(block) for block in blocks]
    header = struct.pack("<IIII", 0x9E2A83C1, block_size, sum(map(len, compressed)), len(data))
    sizes = b"".join(struct.pack("<II", len(encoded), len(raw)) for encoded, raw in zip(compressed, blocks))
    return header + sizes + b"".join(compressed)


def _function(record, script, reference_count):
    stored_size = struct.unpack_from("<I", record, 44)[0]
    prefix = bytearray(record[:48])
    struct.pack_into("<II", prefix, 40, len(script) + reference_count * 4, len(script))
    return bytes(prefix) + script + record[48 + stored_size:]


def build(data, keys, port=49320):
    package = ProjectX(data, keys)
    records = {name: package.export(name) for name in GUARDS}
    http_url = ("http://127.0.0.1:%d" % port).encode()
    socket_url = ("ws://127.0.0.1:%d/ws/gc2" % port).encode()
    if http_url in records["PsyNetConnection_X.GetPsyNetURL"] and socket_url in records["PsyNetMessengerWebSocket_X.Init"]:
        return data
    for name, record in records.items():
        if hashlib.sha256(record).hexdigest() != GUARDS[name]:
            raise upk.UPKError("Unsupported game function revision: " + name)
    get_url = _function(records["PsyNetConnection_X.GetPsyNetURL"], b"\x04\x1f" + http_url + b"\x00\x4c", 0)
    original_init = records["PsyNetMessengerWebSocket_X.Init"]
    old_script = original_init[48:73]
    destination = struct.unpack_from("<i", old_script, 2)[0]
    source = struct.unpack_from("<i", old_script, 7)[0]
    if package.object_path(destination) != "PsyNetMessengerWebSocket_X.URL" or package.object_path(source) != "PsyNetMessengerWebSocket_X.Init.InURL":
        raise upk.UPKError("Unsupported WebSocket URL assignment")
    script = old_script[:6] + b"\x1f" + socket_url + b"\x00" + old_script[11:]
    init = _function(original_init, script, 3)
    start = max(offset + len(raw) for offset, raw in package.regions)
    payload = get_url + init
    chunk = _encode_chunk(payload)
    h = package.header
    table_start = package.map_offset - h.no
    old_table_end = package.map_offset + 4 + len(package.records) * 36
    garbage = struct.unpack_from("<I", data, h.no - 24)[0]
    if old_table_end + garbage != h.hs or garbage < 36:
        raise upk.UPKError("Unsupported ProjectX chunk-map padding")
    for name, size, offset in [
        ("PsyNetConnection_X.GetPsyNetURL", len(get_url), start),
        ("PsyNetMessengerWebSocket_X.Init", len(init), start + len(get_url)),
    ]:
        struct.pack_into("<ii", package.decoded, package.export_position(name) + 32, size, offset)
    struct.pack_into("<I", package.decoded, table_start, len(package.records) + 1)
    new_map = struct.pack("<QiQiIII", start, len(payload), len(data), len(chunk), 0, 0, 0)
    position = old_table_end - h.no
    package.decoded[position:position + 36] = new_map
    result = bytearray(data)
    struct.pack_into("<I", result, h.no - 24, garbage - 36)
    encrypted_size = (h.hs - h.no) // 16 * 16
    result[h.no:h.no + encrypted_size] = AES.new(package.key, AES.MODE_ECB).encrypt(bytes(package.decoded[:encrypted_size]))
    result.extend(chunk)
    rebuilt = ProjectX(bytes(result), keys)
    if rebuilt.export("PsyNetConnection_X.GetPsyNetURL") != get_url or rebuilt.export("PsyNetMessengerWebSocket_X.Init") != init:
        raise upk.UPKError("ProjectX patch verification failed")
    return bytes(result)


def prepare(cooked_dir, data_dir, backup_dir):
    target = Path(cooked_dir) / "ProjectX.upk"
    original = target.read_bytes()
    modified = build(original, upk.load_keys(data_dir))
    if modified == original:
        return {"changed": False}
    backup = Path(backup_dir) / "proxy-bootstrap" / hashlib.sha256(original).hexdigest() / target.name
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(target, backup)
    _replace(target, modified)
    return {"changed": True, "backup": str(backup)}


def _replace(target, data):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".ripple-bootstrap-", delete=False) as stream:
            temporary = stream.name
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, target)
    finally:
        if temporary and os.path.exists(temporary):
            os.unlink(temporary)


def restore_active():
    state = store.read_json("proxy-bootstrap.json", {})
    if not state.get("changed") or not state.get("backup"):
        return False
    backup = Path(state["backup"])
    backup.resolve().relative_to(Path(store.BACKUP_DIR).resolve())
    original = backup.read_bytes()
    target = Path(state["cookedDir"]) / "ProjectX.upk"
    current = target.read_bytes()
    if current != original:
        if build(original, upk.load_keys(store.DATA_DIR)) != current:
            raise upk.UPKError("ProjectX changed after proxy preparation; retained its backup")
        _replace(target, original)
    state["changed"] = False
    state["restored"] = True
    store.write_json("proxy-bootstrap.json", state)
    return True
