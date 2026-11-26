"""Build native patch-manager requests against a verified baseline on disk."""

import json
from pathlib import Path
import struct
import sys
import zlib

from . import package_writer as writer, upk


def build(baseline, expected_sha256, edits, keys):
    writer.require(writer.sha256(baseline) == expected_sha256, "Baseline package hash mismatch")
    package = writer.Package(baseline, keys)
    replacements = []
    for edit in sorted(edits, key=lambda record: record["export"]):
        number = edit["export"]
        original = package.export_body(number)
        writer.require(package.object_path(number) == edit["objectPath"], f"Export {number} path mismatch")
        writer.require(writer.sha256(original) == edit["sourceSha256"], f"Export {number} source hash mismatch")
        # Offsets are relative to the original body. Walk backwards so a resized splice
        # cannot shift the offsets of earlier splices.
        body = bytearray(original)
        end = len(original)
        for splice in sorted(edit["splices"], key=lambda record: record["offset"], reverse=True):
            offset = splice["offset"]
            before = bytes.fromhex(splice["beforeHex"])
            after = bytes.fromhex(splice["afterHex"])
            writer.require(0 <= offset < offset + len(before) <= end, "Invalid or overlapping export splice")
            writer.require(original[offset:offset + len(before)] == before, "Export splice baseline mismatch")
            body[offset:offset + len(before)] = after
            end = offset
        writer.require(writer.sha256(body) == edit["resultSha256"], f"Export {number} result hash mismatch")
        replacements.append(writer.Edit(number, edit["sourceSha256"], bytes(body)))
    return writer.build(package, replacements)


def main():
    request, baseline, output, data_dir = map(Path, sys.argv[1:])
    try:
        plan = json.loads(request.read_text(encoding="utf-8"))
        result = build(baseline.read_bytes(), plan["baselineSha256"], plan["edits"], upk.load_keys(data_dir))
        with output.open("xb") as stream:
            stream.write(result)
        print(json.dumps({"outputSha256": writer.sha256(result)}))
    except (OSError, ValueError, KeyError, TypeError, struct.error, zlib.error, upk.UPKError) as error:
        raise SystemExit(str(error))


if __name__ == "__main__":
    main()
