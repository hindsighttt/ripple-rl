"""Offline reader and writer for Ripple RL's encrypted, chunked UE3 packages."""

from bisect import bisect_right
from dataclasses import dataclass
import hashlib
import struct

from Crypto.Cipher import AES

from . import bootstrap, patches, upk


class PackageError(ValueError):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise PackageError(message)


def take(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset,
            "Package field extends outside its region")
    return data[offset:offset + size]


def name_table(data, count):
    require(0 <= count <= len(data) // 13, "Invalid name count")
    names = []
    cursor = 0
    for _ in range(count):
        length = struct.unpack("<i", take(data, cursor, 4))[0]
        unit = 2 if length < 0 else 1
        width = abs(length) * unit
        raw = take(data, cursor + 4, width)
        require(width >= unit and raw.endswith(bytes(unit)), "Invalid package name")
        names.append(raw[:-unit].decode("utf-16le" if unit == 2 else "latin1"))
        take(data, cursor + 4 + width, 8)
        cursor += width + 12
    return names, cursor


@dataclass(frozen=True)
class Chunk:
    address: int
    size: int
    physical: int
    packed_size: int


@dataclass(frozen=True)
class Edit:
    number: int  # One-based export number, as used by Unreal object references.
    expected_sha256: str
    body: bytes


class Package:
    def __init__(self, data, keys, physical_names=None):
        self.data = data
        require(len(data) >= 57 and struct.unpack_from("<I", data)[0] == 0x9E2A83C1,
                "Expected a cooked Unreal package")
        self.header_size = struct.unpack_from("<I", data, 8)[0]
        (self.name_count, self.name_address, self.export_count, self.export_address,
         self.import_count, self.import_address, self.depends_address) = struct.unpack_from("<7i", data, 25)
        self.physical_names = self.name_address if physical_names is None else physical_names
        start = self.physical_names
        require(57 <= start < self.header_size <= len(data),
                "Invalid header bounds or missing physical header offset")
        packed_header = take(data, start, (self.header_size - start) // 16 * 16)
        self.map_offset = struct.unpack_from("<I", data, start - 20)[0]
        require(self.map_offset <= len(packed_header) - 4, "Invalid compression map offset")
        for key in keys:
            decoded = AES.new(key, AES.MODE_ECB).decrypt(packed_header)
            count = struct.unpack_from("<I", decoded, self.map_offset)[0]
            if not 1 <= count <= min(4096, (len(decoded) - self.map_offset - 4) // 36):
                continue
            chunks = [
                Chunk(*struct.unpack_from("<QiQi", decoded, self.map_offset + 4 + i * 36))
                for i in range(count)
            ]
            if not all(
                chunk.size > 0 and self.header_size <= chunk.physical <= len(data) - chunk.packed_size
                and chunk.packed_size >= 16 and data[chunk.physical:chunk.physical + 4] == b"\xc1\x83\x2a\x9e"
                for chunk in chunks
            ):
                continue
            self.header, self.key, self.chunks = decoded, key, chunks
            break
        else:
            raise PackageError("No key produced a supported chunk map")
        self.chunk_starts = [chunk.address for chunk in self.chunks]
        regions = sorted((chunk.physical, chunk.physical + chunk.packed_size) for chunk in self.chunks)
        require(all(left[1] <= right[0] for left, right in zip(regions, regions[1:])),
                "Overlapping physical chunks")
        require(all(left.address + left.size <= right.address for left, right in zip(self.chunks, self.chunks[1:])),
                "Overlapping or unordered virtual chunks")
        self._cached_chunk = None
        self.names, name_size = name_table(self.region_from(self.name_address), self.name_count)
        self.name_bytes = self.read(self.name_address, name_size)
        self.import_bytes = self.read(self.import_address, self.import_count * 28)
        cursor = self.export_address
        require(0 <= self.export_count <= len(self.region_from(cursor)) // 72, "Invalid export count")
        self.export_headers = []
        for _ in range(self.export_count):
            count = struct.unpack("<i", self.read(cursor + 48, 4))[0]
            require(0 <= count <= 65536, "Invalid export header length")
            record = self.read(cursor, 72 + count * 4)
            self.export_headers.append(record)
            cursor += len(record)
        self.export_end = cursor
        self._validate_tables()

    def chunk_data(self, index):
        if self._cached_chunk is None or self._cached_chunk[0] != index:
            chunk = self.chunks[index]
            raw = bootstrap._decode_chunk(take(self.data, chunk.physical, chunk.packed_size))
            require(len(raw) == chunk.size, "Decoded chunk length differs from its map")
            self._cached_chunk = index, raw
        return self._cached_chunk[1]

    def header_contains(self, address, size):
        end = min(self.physical_names + len(self.header), self.chunk_starts[0])
        return self.physical_names <= address <= end - size

    def region_at(self, address):
        start = self.physical_names
        if self.header_contains(address, 1):
            return self.header, address - start
        index = bisect_right(self.chunk_starts, address) - 1
        require(index >= 0 and address < self.chunks[index].address + self.chunks[index].size,
                "Package virtual address is unmapped")
        return self.chunk_data(index), address - self.chunks[index].address

    def region_from(self, address):
        region, offset = self.region_at(address)
        end = self.chunk_starts[0] - self.physical_names if region is self.header else len(region)
        return region[offset:end]

    def read(self, address, size):
        if size == 0:
            return b""
        region, offset = self.region_at(address)
        if region is self.header:
            require(self.header_contains(address, size), "Package field extends past the virtual header")
        return take(region, offset, size)

    def export_metadata(self, number):
        require(1 <= number <= self.export_count, "Unknown export number")
        return struct.unpack_from("<ii", self.export_headers[number - 1], 32)

    def export_body(self, number):
        size, address = self.export_metadata(number)
        return self.read(address, size)

    def object_path(self, number):
        parts = []
        seen = set()
        while number:
            require(number not in seen, "Cyclic export path")
            seen.add(number)
            if number > 0:
                parent, name = struct.unpack_from("<ii", self.export_headers[number - 1], 8)
            else:
                parent, name = struct.unpack_from("<ii", self.import_bytes, (-number - 1) * 28 + 16)
            parts.append(self.names[name])
            number = parent
        return ".".join(reversed(parts))

    def _validate_tables(self):
        low = -self.import_count
        high = self.export_count
        for offset in range(0, len(self.import_bytes), 28):
            class_package, _, class_name, _, outer, name, _ = struct.unpack_from("<7i", self.import_bytes, offset)
            require(all(0 <= value < self.name_count for value in (class_package, class_name, name))
                    and low <= outer <= high, "Invalid import table reference")
        for record in self.export_headers:
            cls, parent_class, outer, name, _, archetype = struct.unpack_from("<6i", record)
            require(0 <= name < self.name_count and all(low <= value <= high for value in
                    (cls, parent_class, outer, archetype)), "Invalid export table reference")
            size, address = struct.unpack_from("<ii", record, 32)
            require(size >= 0, "Negative export body size")
            if size:
                index = bisect_right(self.chunk_starts, address) - 1
                in_chunk = index >= 0 and address + size <= self.chunks[index].address + self.chunks[index].size
                in_header = self.header_contains(address, size)
                require(in_chunk or in_header, "Export body extends outside its mapped region")


def verify_edits(package, edits):
    by_number = {}
    for edit in edits:
        require(edit.number not in by_number, "Duplicate replacement for an export")
        require(sha256(package.export_body(edit.number)) == edit.expected_sha256.lower(),
                f"Export {edit.number} does not match the expected SHA-256")
        by_number[edit.number] = edit.body
    return by_number


def build(package, edits, *, table_edits=(), added_names=b"", name_count=0,
          added_imports=b"", added_exports=()):
    """Write selected bodies and optional table additions, then reparse the result."""
    replacements = verify_edits(package, edits)
    require(replacements or table_edits or added_names or added_imports or added_exports, "No package edits selected")
    header = bytearray(package.header)
    physical = package.physical_names
    original_tables = [
        (package.name_address, len(package.name_bytes)),
        (package.import_address, len(package.import_bytes)),
        (package.export_address, package.export_end - package.export_address),
    ]
    for edit in table_edits:
        address = physical + edit.offset
        size = len(edit.before)
        require(size == len(edit.after) and any(
            start <= address and address + size <= start + length for start, length in original_tables
        ), "Table edit is outside the input tables or depends on a different layout")
        require(take(header, edit.offset, size) == edit.before, "Table edit baseline mismatch")
        header[edit.offset:edit.offset + size] = edit.after

    last = package.chunks[-1]
    raw = bytearray(package.chunk_data(len(package.chunks) - 1))
    raw_size = len(raw)

    def place(record, body):
        if body:
            size, address = len(body), last.address + len(raw)
            require(address + size <= 0x7FFFFFFF, "Export address exceeds package limits")
            raw.extend(body)
        else:
            size, address = 0, 0
        return record[:32] + struct.pack("<ii", size, address) + record[40:]

    positions = []
    cursor = package.export_address
    records = []
    for record in package.export_headers:
        positions.append(cursor)
        if package.header_contains(cursor, len(record)):
            record = take(header, cursor - physical, len(record))
        records.append(record)
        cursor += len(record)
    for number, body in replacements.items():
        records[number - 1] = place(records[number - 1], body)
    for addition in added_exports:
        require(len(addition.header) == 72 and struct.unpack_from("<i", addition.header, 48)[0] == 0,
                "Unsupported added export header")
        records.append(place(addition.header, addition.data))
    for number, position in enumerate(positions):
        record = records[number]
        if package.header_contains(position, len(record)):
            start = position - physical
            header[start:start + len(record)] = record

    def table_bytes(address, original):
        if package.header_contains(address, len(original)):
            return bytes(take(header, address - physical, len(original)))
        return original

    require(name_table(added_names, name_count)[1] == len(added_names), "Added name blob does not match its count")
    require(len(added_imports) % 28 == 0, "Incomplete added import record")
    names = table_bytes(package.name_address, package.name_bytes) + added_names
    imports = table_bytes(package.import_address, package.import_bytes) + added_imports
    summary = bytearray(package.data[:physical])
    existing_offsets = (package.name_address, package.import_address, package.export_address)
    relocated = any(not package.header_contains(address, length) for address, length in original_tables)
    if relocated or added_names or added_imports or added_exports:
        raw.append(0)
        import_address = last.address + len(raw)
        raw.extend(imports)
        name_address = last.address + len(raw)
        raw.extend(names)
        export_address = last.address + len(raw)
        raw.extend(b"".join(records))
    else:
        name_address, import_address, export_address = existing_offsets
    total_names = package.name_count + name_count
    if total_names != package.name_count or len(records) != package.export_count:
        counts = struct.pack("<ii", package.export_count, package.name_count)
        generation = summary.find(counts, 57)
        require(generation >= 0, "Cannot identify the generation count pair in this package")
        if summary.find(counts, generation + 1) != -1:
            # Consecutive generations can carry identical counts (for example
            # ProjectX). The cooked summary's generation array follows its GUID.
            count_offset = 85
            count = struct.unpack("<I", take(summary, count_offset, 4))[0]
            generation = count_offset + 4 + (count - 1) * 12
            require(count > 0 and generation + 12 <= len(summary) and
                    take(summary, generation, 8) == counts,
                    "Cannot identify the last generation count pair in this package")
        struct.pack_into("<ii", summary, generation, len(records), total_names)
    struct.pack_into("<6i", summary, 25, total_names, name_address, len(records), export_address,
                     len(imports) // 28, import_address)
    require(last.address + len(raw) <= 0x7FFFFFFF, "Final chunk exceeds package limits")
    packed = bootstrap._encode_chunk(raw)
    map_entry = package.map_offset + 4 + (len(package.chunks) - 1) * 36
    struct.pack_into("<iQi", header, map_entry + 8, len(raw), len(package.data), len(packed))
    encrypted = AES.new(package.key, AES.MODE_ECB).encrypt(bytes(header))
    output = bytes(summary) + encrypted + package.data[physical + len(header):] + packed

    reopened = Package(output, [package.key], physical)
    require((reopened.name_bytes, reopened.import_bytes, reopened.export_headers) == (names, imports, records),
            "Output table verification failed")
    require(reopened.chunks[:-1] == package.chunks[:-1] and reopened.chunk_data(len(reopened.chunks) - 1) == raw
            and raw[:raw_size] == package.chunk_data(len(package.chunks) - 1), "Output chunk verification failed")
    for number, body in replacements.items():
        require(reopened.export_body(number) == body, f"Output export {number} verification failed")
    for number, addition in enumerate(added_exports, package.export_count + 1):
        require(reopened.export_body(number) == addition.data, f"Output added export {number} verification failed")
    return output


def replay(package, group):
    """Translate a nonempty-delta SSP3 group into a verified package rewrite."""
    require(len(group.added_imports) == group.added_import_count * 28, "Import count does not match records")
    edits = []
    for record in group.patches:
        source, target = record.descriptor_fields()
        number = source[0]
        require(record.mode in (0, 2) and record.payload, "Unsupported SSP3 mode or empty delta")
        original = package.export_body(number)
        require(len(original) == source[2], f"SSP3 source size mismatch for export {number}")
        body = patches.apply_delta(original, record.payload)
        require(len(body) == target[2], f"SSP3 output size mismatch for export {number}")
        edits.append(Edit(number, sha256(original), body))
    return build(package, edits, table_edits=group.byte_edits, added_names=group.added_names,
                 name_count=group.added_name_count, added_imports=group.added_imports,
                 added_exports=group.added_exports)
