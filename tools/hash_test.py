import os, zlib, struct, hashlib, xxhash
base = os.path.join(os.environ['USERPROFILE'], 'Documents', 'My Games', 'Rocket League', 'TAGame', 'Cache')
files = {
 'before': os.path.join(base, 'WebCache', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='),
 'after': os.path.join(base, 'WebCache.before-launcher-only-20261125-163354', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='),
}
POLY64 = 0xC96C5795D7870F42

def crc64_ecma(data):
    crc = 0xFFFFFFFFFFFFFFFF
    for x in data:
        crc ^= x
        for _ in range(8):
            crc = (crc >> 1) ^ POLY64 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFFFFFFFFFF

for label, path in files.items():
    b = open(path, 'rb').read()
    i = b.find(b'{"RegionConfig"')
    prop = b.rfind(b'\x08\x00\x00\x00DataCRC')
    targets = {
        'hdr2': struct.unpack_from('<I', b, 4)[0],
        'hdr2be': struct.unpack_from('>I', b, 4)[0],
        'propcrc': 0xdf3302ad if label == 'before' else 0x20b49e8c,
    }
    print('===', label, {k: hex(v) for k, v in targets.items()})
    ranges = {
        'body': b[i:prop],
        'from8': b[8:],
        'from43': b[43:],
        'entire8-': b[8:],
        'json_end': b[i:],
        'hdr_excl': b[8:len(b)-8],
    }
    for rn, data in ranges.items():
        vals = {
            'xxh32': xxhash.xxh32(data).intdigest(),
            'xxh64lo': xxhash.xxh64(data).intdigest() & 0xffffffff,
            'xxh64hi': xxhash.xxh64(data).intdigest() >> 32,
            'xxh3lo': xxhash.xxh3_64(data).intdigest() & 0xffffffff,
            'xxh3hi': xxhash.xxh3_64(data).intdigest() >> 32,
            'crc64lo': crc64_ecma(data) & 0xffffffff,
            'crc64hi': crc64_ecma(data) >> 32,
        }
        for vn, v in vals.items():
            for tn, tv in targets.items():
                if v == tv:
                    print('  MATCH', tn, rn, vn)
