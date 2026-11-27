"""Binary envelope shared by Rocket League WebCache entries."""

import os
import struct


DATA_PROPERTY = b"ArrayProperty\x00"
PROPERTY_START = b"\x08\x00\x00\x00DataCRC\x00"
HEADER_CRC_SEED = 0x6EEFBED4


def _crc_table():
    table = []
    for n in range(256):
        crc = n << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xFFFFFFFF
        table.append(crc)
    return table


_CRC_TABLE = _crc_table()


def cache_crc(data, seed):
    crc = (~seed) & 0xFFFFFFFF
    for value in data:
        crc = ((crc << 8) ^ _CRC_TABLE[((crc >> 24) ^ value) & 0xFF]) & 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF


def header_crc(data):
    return cache_crc(data, HEADER_CRC_SEED)


def webcache_dir():
    return os.path.join(os.environ.get("USERPROFILE", ""), "Documents", "My Games",
                        "Rocket League", "TAGame", "Cache", "WebCache")


def split_entry(data):
    marker = data.find(DATA_PROPERTY)
    if marker < 0:
        raise ValueError("no Data array property")
    body_start = marker + len(DATA_PROPERTY) + 12
    if body_start > len(data):
        raise ValueError("truncated array header")
    body_end = body_start + struct.unpack_from("<I", data, body_start - 4)[0]
    properties = data[body_end:]
    if not properties.startswith(PROPERTY_START):
        raise ValueError("missing cache properties")
    return data[:body_start], data[body_start:body_end], properties


def pack_entry(prefix, body, properties):
    head = bytearray(prefix)
    total = len(head) + len(body) + len(properties)
    struct.pack_into("<I", head, 0, total - 8)
    struct.pack_into("<I", head, 8, total - 4)
    struct.pack_into("<I", head, len(head) - 12, len(body) + 4)
    struct.pack_into("<I", head, len(head) - 4, len(body))
    rebuilt = head + body + properties
    struct.pack_into("<I", rebuilt, 4, header_crc(memoryview(rebuilt)[8:]))
    return bytes(rebuilt)
