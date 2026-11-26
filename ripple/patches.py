"""Read recovered SSP3 manifests while preserving opaque patch descriptors and payloads."""

import struct
from dataclasses import dataclass


class ManifestError(ValueError):
    pass


class _Reader:
    def __init__(self, data):
        self.data = memoryview(data)
        self.offset = 0

    def take(self, size):
        end = self.offset + size
        if size < 0 or end > len(self.data):
            raise ManifestError("Truncated SSP3 record at byte %d" % self.offset)
        value = bytes(self.data[self.offset:end])
        self.offset = end
        return value

    def integer(self, width=4):
        return int.from_bytes(self.take(width), "little")

    def count(self, minimum_size):
        count = self.integer()
        if count > (len(self.data) - self.offset) // minimum_size:
            raise ManifestError("SSP3 count exceeds remaining record data")
        return count

    def blob(self):
        return self.take(self.integer())


@dataclass(frozen=True)
class Patch:
    descriptor_a: bytes
    descriptor_b: bytes
    mode: int
    payload: bytes

    def descriptor_fields(self):
        return (
            struct.unpack("<IQIQ", self.descriptor_a),
            struct.unpack("<IQIQ", self.descriptor_b),
        )


@dataclass(frozen=True)
class ByteEdit:
    offset: int
    before: bytes
    after: bytes


@dataclass(frozen=True)
class AddedExport:
    header: bytes
    data: bytes


@dataclass(frozen=True)
class CopyRange:
    offset: int
    size: int


@dataclass(frozen=True)
class InsertData:
    data: bytes


@dataclass(frozen=True)
class PackagePatches:
    name: str
    patches: tuple[Patch, ...]
    byte_edits: tuple[ByteEdit, ...]
    added_name_count: int
    added_names: bytes
    added_import_count: int
    added_imports: bytes
    added_exports: tuple[AddedExport, ...]


def parse_manifest(data):
    reader = _Reader(data)
    if reader.take(4) != b"SSP3":
        raise ManifestError("Expected SSP3 magic")
    packages = []
    for _ in range(reader.count(26)):
        try:
            name = reader.take(reader.integer(2)).decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ManifestError("Invalid SSP3 package name") from exc
        patches = []
        for _ in range(reader.count(53)):
            patches.append(Patch(reader.take(24), reader.take(24), reader.integer(1), reader.blob()))
        byte_edits = []
        for _ in range(reader.count(8)):
            offset = reader.integer()
            size = reader.integer()
            byte_edits.append(ByteEdit(offset, reader.take(size), reader.take(size)))
        name_count = reader.integer()
        names = reader.blob()
        import_count = reader.integer()
        imports = reader.blob()
        if len(imports) != import_count * 28:
            raise ManifestError("SSP3 import count does not match its 28-byte records")
        exports = []
        for _ in range(reader.count(8)):
            exports.append(AddedExport(reader.blob(), reader.blob()))
        packages.append(PackagePatches(
            name, tuple(patches), tuple(byte_edits), name_count, names,
            import_count, imports, tuple(exports),
        ))
    if reader.offset != len(reader.data):
        raise ManifestError("Unexpected trailing SSP3 data at byte %d" % reader.offset)
    return tuple(packages)


def decode_delta(payload):
    if not payload:
        raise ManifestError("Empty Legacy delta semantics are unresolved")
    reader = _Reader(payload)
    operations = []
    for _ in range(reader.count(5)):
        opcode = reader.integer(1)
        if opcode == 0:
            operations.append(CopyRange(reader.integer(), reader.integer()))
        elif opcode == 1:
            operations.append(InsertData(reader.blob()))
        else:
            raise ManifestError("Unknown SSP3 delta opcode %d" % opcode)
    if reader.offset != len(reader.data):
        raise ManifestError("Unexpected trailing SSP3 delta data")
    return tuple(operations)


def apply_delta(original, payload):
    result = []
    for operation in decode_delta(payload):
        if isinstance(operation, CopyRange):
            end = operation.offset + operation.size
            if end > len(original):
                raise ManifestError("SSP3 copy exceeds original export data")
            result.append(original[operation.offset:end])
        else:
            result.append(operation.data)
    return b"".join(result)
