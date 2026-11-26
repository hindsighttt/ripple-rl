from base64 import b64encode
from pathlib import Path
import struct
import tempfile
import unittest

from Crypto.Cipher import AES

from ripple import bootstrap, garage_recipe, package_writer as writer, patch_recipe, patches, upk
from tools.package_writer import output_file


def name_entry(name):
    raw = name.encode("ascii") + b"\0"
    return struct.pack("<i", len(raw)) + raw + bytes(8)


def export_entry(name, size=0, address=0):
    record = bytearray(72)
    struct.pack_into("<i", record, 12, name)
    struct.pack_into("<ii", record, 32, size, address)
    return bytes(record)


def fixture():
    physical = 128
    header_size = 512
    virtual = 320  # Virtual chunk overlaps the physical header's numeric range.
    payload = b"originalothers"
    packed = bootstrap._encode_chunk(payload)
    names = name_entry("First") + name_entry("Second")
    tables = names + export_entry(0, 8, virtual) + export_entry(1, 6, virtual + 8)
    map_offset = len(tables)
    mapping = struct.pack("<IQiQiIII", 1, virtual, len(payload), header_size, len(packed), 0, 0, 0)
    header = bytearray(physical)
    struct.pack_into("<IHHI", header, 0, 0x9E2A83C1, 1, 1, header_size)
    export_address = physical + len(names)
    struct.pack_into("<7i", header, 25, 2, physical, 2, export_address, 0, export_address,
                     physical + map_offset)
    struct.pack_into("<ii", header, 80, 2, 2)
    struct.pack_into("<I", header, physical - 20, map_offset)
    encrypted = AES.new(upk.DEFAULT_KEY, AES.MODE_ECB).encrypt((tables + mapping).ljust(header_size - physical, b"\0"))
    return bytes(header) + encrypted + packed


class PackageWriterTests(unittest.TestCase):
    def test_replaces_only_selected_export_and_can_reopen_output(self):
        source = writer.Package(fixture(), [upk.DEFAULT_KEY])
        original = source.export_body(1)
        self.assertEqual(original, b"original")
        output = writer.build(source, [writer.Edit(1, writer.sha256(original), b"an authored replacement")])
        rebuilt = writer.Package(output, [upk.DEFAULT_KEY])
        self.assertEqual(rebuilt.export_body(1), b"an authored replacement")
        self.assertEqual(rebuilt.export_body(2), b"others")
        self.assertEqual(rebuilt.export_headers[1], source.export_headers[1])
        self.assertEqual(rebuilt.object_path(1), "First")
        self.assertEqual(source.export_body(1), b"original")

    def test_updates_relocated_tables_and_keeps_existing_exports(self):
        source = writer.Package(fixture(), [upk.DEFAULT_KEY])
        addition = patches.AddedExport(export_entry(2), b"added body")
        rename = patches.ByteEdit(4, b"First", b"Alpha")
        output = writer.build(source, [], table_edits=[rename], added_names=name_entry("Third"),
                              name_count=1, added_exports=[addition])
        relocated = writer.Package(output, [upk.DEFAULT_KEY], source.physical_names)
        self.assertEqual(relocated.names, ["Alpha", "Second", "Third"])
        self.assertEqual(relocated.export_body(3), b"added body")
        self.assertEqual(relocated.export_body(1), b"original")
        self.assertEqual(relocated.export_body(2), b"others")

    def test_rejects_stale_source_hash_and_table_bytes(self):
        source = writer.Package(fixture(), [upk.DEFAULT_KEY])
        with self.assertRaisesRegex(writer.PackageError, "expected SHA-256"):
            writer.build(source, [writer.Edit(1, "0" * 64, b"replacement")])
        with self.assertRaisesRegex(writer.PackageError, "baseline mismatch"):
            writer.build(source, [], table_edits=[patches.ByteEdit(4, b"Other", b"Alpha")])

    def test_composes_recipe_splices_and_rejects_a_bad_replacement_hash(self):
        baseline = fixture()
        keys = [upk.DEFAULT_KEY]
        first = {
            "export": 1, "objectPath": "First", "sourceSha256": writer.sha256(b"original"),
            "resultSha256": writer.sha256(b"new-al"),
            "splices": [{"offset": 0, "beforeHex": b"origin".hex(), "afterHex": b"new-".hex()}],
        }
        second = {
            "export": 2, "objectPath": "Second", "sourceSha256": writer.sha256(b"others"),
            "resultSha256": writer.sha256(b"other"),
            "splices": [{"offset": 5, "beforeHex": b"s".hex(), "afterHex": ""}],
        }
        result = patch_recipe.build(baseline, writer.sha256(baseline), [second, first], keys)
        reopened = writer.Package(result, keys)
        self.assertEqual((reopened.export_body(1), reopened.export_body(2)), (b"new-al", b"other"))
        with self.assertRaisesRegex(writer.PackageError, "Duplicate replacement"):
            patch_recipe.build(baseline, writer.sha256(baseline), [first, first], keys)
        with self.assertRaisesRegex(writer.PackageError, "result hash"):
            patch_recipe.build(baseline, writer.sha256(baseline), [dict(first, resultSha256="0" * 64)], keys)

    def test_refuses_to_overwrite_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.upk"
            output_file(path, b"first")
            with self.assertRaises(FileExistsError):
                output_file(path, b"second")
            self.assertEqual(path.read_bytes(), b"first")
            self.assertEqual(list(path.parent.iterdir()), [path])

    def test_garage_recipe_validates_source_and_generated_package(self):
        baseline = fixture()
        keys = [upk.DEFAULT_KEY]
        original = b"original"
        replacement = b"new-ginal"
        expected = writer.build(
            writer.Package(baseline, keys), [writer.Edit(1, writer.sha256(original), replacement)],
        )
        edit = {
            "export": 1, "path": "First", "sourceSha256": writer.sha256(original),
            "resultSha256": writer.sha256(replacement), "body": [b64encode(b"new-").decode(), [3, 5]],
        }
        recipe = {
            "vanillaSha256": writer.sha256(baseline), "patchedSha256": writer.sha256(expected),
            "edits": [edit], "tableEdits": [], "addedNames": "", "addedNameCount": 0,
            "addedImports": "", "addedExports": [],
        }
        self.assertEqual(garage_recipe.build(baseline, recipe, keys), expected)
        with self.assertRaisesRegex(writer.PackageError, "Unsupported Garage baseline"):
            garage_recipe.build(baseline + b"x", recipe, keys)
        with self.assertRaisesRegex(writer.PackageError, "generated Garage package hash"):
            garage_recipe.build(baseline, dict(recipe, patchedSha256="0" * 64), keys)
        with self.assertRaisesRegex(writer.PackageError, "path mismatch"):
            garage_recipe.build(baseline, dict(recipe, edits=[dict(edit, path="Second")]), keys)
