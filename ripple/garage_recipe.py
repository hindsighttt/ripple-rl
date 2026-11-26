"""Build a Garage package from a supported stock UPK and a shipped edit recipe."""

from base64 import b64decode
import json
from pathlib import Path
import struct
import sys
import zlib

from . import package_writer as writer, patches, upk


def decode(value):
    return b64decode(value, validate=True)


def build(baseline, recipe, keys):
    writer.require(writer.sha256(baseline) == recipe["vanillaSha256"], "Unsupported Garage baseline")
    package = writer.Package(baseline, keys)
    edits = []
    for edit in recipe["edits"]:
        number = edit["export"]
        original = package.export_body(number)
        writer.require(package.object_path(number) == edit["path"], f"Garage export {number} path mismatch")
        writer.require(writer.sha256(original) == edit["sourceSha256"], f"Garage export {number} source mismatch")
        body = b"".join(
            decode(op) if isinstance(op, str) else writer.take(original, *op)
            for op in edit["body"]
        )
        writer.require(writer.sha256(body) == edit["resultSha256"], f"Garage export {number} output mismatch")
        edits.append(writer.Edit(number, edit["sourceSha256"], body))

    result = writer.build(
        package, edits,
        table_edits=[patches.ByteEdit(offset, decode(before), decode(after))
                     for offset, before, after in recipe["tableEdits"]],
        added_names=decode(recipe["addedNames"]), name_count=recipe["addedNameCount"],
        added_imports=decode(recipe["addedImports"]),
        added_exports=[patches.AddedExport(decode(header), decode(body))
                       for header, body in recipe["addedExports"]],
    )
    writer.require(writer.sha256(result) == recipe["patchedSha256"], "Unexpected generated Garage package hash")
    return result


def main():
    manifest, baseline, output, data_dir = map(Path, sys.argv[1:])
    try:
        catalog = json.loads(manifest.read_text(encoding="utf-8"))
        writer.require(catalog["schema"] == 1, "Unsupported Garage recipe schema")
        recipe = catalog["packages"][baseline.name]
        result = build(baseline.read_bytes(), recipe, upk.load_keys(data_dir))
        with output.open("xb") as stream:
            stream.write(result)
    except (OSError, ValueError, KeyError, TypeError, struct.error, zlib.error, upk.UPKError) as error:
        raise SystemExit(str(error))


if __name__ == "__main__":
    main()
