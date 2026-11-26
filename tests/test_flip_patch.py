import struct
import unittest

from Crypto.Cipher import AES

from ripple import bootstrap, flip_patch, upk


def package():
    no = flip_patch.PHYSICAL_NAMES
    decoded = bytearray(4096)
    map_position = 2000
    raw = b"a" * (131072 + 1234) + flip_patch.DEFAULT_VALUES + b"b" * 140000
    chunk = bootstrap._encode_chunk(raw)
    hs = no + len(decoded)
    struct.pack_into("<I", decoded, map_position, 1)
    struct.pack_into("<QiQiIII", decoded, map_position + 4, 1000000, len(raw), hs, len(chunk), 0, 0, 0)
    header = bytearray(no)
    struct.pack_into("<I", header, 8, hs)
    struct.pack_into("<I", header, no - 20, map_position)
    return bytes(header) + AES.new(upk.DEFAULT_KEY, AES.MODE_ECB).encrypt(decoded) + chunk + b"FOOTER", raw, chunk


class FlipPatchTests(unittest.TestCase):
    def test_colour_changes_only_the_material_floats(self):
        base, raw, old_chunk = package()
        changed = flip_patch.build(base, 0x123ABC)
        no = flip_patch.PHYSICAL_NAMES
        hs = struct.unpack_from("<I", changed, 8)[0]
        decoded = AES.new(upk.DEFAULT_KEY, AES.MODE_ECB).decrypt(changed[no:hs])
        size = struct.unpack_from("<i", decoded, 2004 + 20)[0]
        new_chunk = changed[hs:hs + size]
        expected = raw.replace(flip_patch.DEFAULT_VALUES, flip_patch._colour_bytes(0x123ABC))
        self.assertEqual(bootstrap._decode_chunk(new_chunk), expected)
        self.assertTrue(changed.endswith(b"FOOTER"))
        # The first compression block is outside the colour edit and stays exact.
        blocks = (len(raw) + 131071) // 131072
        start = 16 + blocks * 8
        first_size = struct.unpack_from("<I", old_chunk, 16)[0]
        self.assertEqual(old_chunk[start:start + first_size], new_chunk[start:start + first_size])

    def test_game_default_preserves_exact_original_values(self):
        base, _, _ = package()
        self.assertEqual(flip_patch.build(base, -1), base)

    def test_invalid_colour_is_rejected(self):
        base, _, _ = package()
        for rgb in (-2, 0x1000000, "#123abc"):
            with self.subTest(rgb=rgb), self.assertRaises(ValueError):
                flip_patch.build(base, rgb)


if __name__ == "__main__":
    unittest.main()
