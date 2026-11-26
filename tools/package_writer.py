"""Inspect and rebuild local UPKs. Run with python -m tools.package_writer."""

import argparse
import json
import os
from pathlib import Path
import struct
import tempfile
import zlib

from ripple import package_writer as writer, patches, upk


def output_file(path, data):
    # Publish a verified result without replacing an existing file.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".ripple-upk-", delete=False) as stream:
            temporary = stream.name
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if os.name == "nt":
            os.rename(temporary, path)
        else:
            os.link(temporary, path)
    finally:
        if temporary is not None:
            Path(temporary).unlink(missing_ok=True)


def verified_input(path, expected_sha256=None):
    data = path.read_bytes()
    if expected_sha256 is not None:
        writer.require(writer.sha256(data) == expected_sha256.lower(), f"SHA-256 mismatch for {path}")
    return data


def sha256_argument(value):
    if len(value) != 64 or any(c not in "0123456789abcdefABCDEF" for c in value):
        raise argparse.ArgumentTypeError("Expected a 64-digit SHA-256")
    return value.lower()


def commands():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=Path(__file__).resolve().parents[1] / "data",
                        help="Directory containing optional package keys")
    actions = parser.add_subparsers(dest="command", required=True)
    for command in ("inspect", "extract", "replace", "replay"):
        action = actions.add_parser(command)
        action.add_argument("package", type=Path)
        action.add_argument("--header-offset", type=lambda value: int(value, 0),
                            help="Physical names/header offset when the active name table was relocated")
        if command != "inspect":
            action.add_argument("--output", type=Path, required=True)
        if command == "inspect":
            selection = action.add_mutually_exclusive_group()
            selection.add_argument("--export", type=int, help="One-based export number")
            selection.add_argument("--find", help="Find exports by a case-insensitive object path substring")
        elif command == "extract":
            action.add_argument("--export", type=int, required=True,
                                help="One-based export number")
        else:
            action.add_argument("--base-sha256", type=sha256_argument, required=True)
        if command == "replace":
            action.add_argument("--edit", nargs=3, action="append", required=True,
                                metavar=("NUMBER", "SOURCE_SHA256", "BODY_FILE"))
        if command == "replay":
            action.add_argument("--manifest", type=Path, required=True)
            action.add_argument("--manifest-sha256", type=sha256_argument, required=True)
    return parser


def main():
    parser = commands()
    args = parser.parse_args()
    try:
        data = verified_input(args.package, getattr(args, "base_sha256", None))
        package = writer.Package(data, upk.load_keys(args.data_dir), args.header_offset)
        details = {
            "input": str(args.package.resolve()),
            "inputSha256": writer.sha256(data),
            "physicalHeaderOffset": package.physical_names,
            "exports": package.export_count,
        }
        if args.command in ("inspect", "extract") and args.export is not None:
            body = package.export_body(args.export)
            details["export"] = {
                "number": args.export, "path": package.object_path(args.export),
                "bytes": len(body), "sha256": writer.sha256(body),
            }
        if args.command == "inspect" and args.find is not None:
            matches = []
            query = args.find.casefold()
            for number in range(1, package.export_count + 1):
                path = package.object_path(number)
                if query in path.casefold():
                    matches.append({"number": number, "path": path})
            details["matches"] = matches
        if args.command == "extract":
            result = body
        elif args.command == "replace":
            edits = [
                writer.Edit(int(number), sha256_argument(expected), Path(path).read_bytes())
                for number, expected, path in args.edit
            ]
            result = writer.build(package, edits)
        elif args.command == "replay":
            manifest = verified_input(args.manifest, args.manifest_sha256)
            matches = [group for group in patches.parse_manifest(manifest)
                       if group.name.casefold() == args.package.name.casefold()]
            writer.require(len(matches) == 1, "Expected one manifest group matching the input package filename")
            result = writer.replay(package, matches[0])
        if args.command != "inspect":
            output_file(args.output, result)
            details.update(output=str(args.output.resolve()), outputSha256=writer.sha256(result), bytes=len(result))
        print(json.dumps(details, indent=2))
    except (OSError, ValueError, struct.error, zlib.error, upk.UPKError, argparse.ArgumentTypeError) as error:
        parser.exit(1, f"package-writer: {error}\n")


if __name__ == "__main__":
    main()
