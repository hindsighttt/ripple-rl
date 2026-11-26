import os, zlib, struct, hashlib
base = os.path.join(os.environ['USERPROFILE'], 'Documents', 'My Games', 'Rocket League', 'TAGame', 'Cache')
files = {
 'before': os.path.join(base, 'WebCache', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='),
 'after': os.path.join(base, 'WebCache.before-launcher-only-20261125-163354', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='),
}

def unreal_crc(data, crc=0):
    tab = []
    for n in range(256):
        c = (n << 24) & 0xffffffff
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) & 0xffffffff if c & 0x80000000 else (c << 1) & 0xffffffff
        tab.append(c)
    for x in data:
        crc = ((crc << 8) ^ tab[((crc >> 24) ^ x) & 0xff]) & 0xffffffff
    return crc


def fnv1a(data, init=0x811c9dc5):
    h = init
    for x in data:
        h ^= x
        h = (h * 0x01000193) & 0xffffffff
    return h


def fnv1(data, init=0x811c9dc5):
    h = init
    for x in data:
        h = (h * 0x01000193) & 0xffffffff
        h ^= x
    return h

for label, path in files.items():
    b = open(path, 'rb').read()
    i = b.find(b'{"RegionConfig"')
    prop = b.rfind(b'\x08\x00\x00\x00DataCRC')
    targets = {
        'hdr1': struct.unpack_from('<I', b, 0)[0],
        'hdr2': struct.unpack_from('<I', b, 4)[0],
        'hdr3': struct.unpack_from('<I', b, 8)[0],
        'propcrc': 0xdf3302ad if label == 'before' else 0x20b49e8c,
    }
    print('===', label, 'len', len(b), {k: hex(v) for k, v in targets.items()})
    ranges = {
        'body': (i, prop),
        'from8': (8, len(b)),
        'from4': (4, len(b)),
        'from43': (43, len(b)),
        'from51': (51, prop),
        'allbut8': (8, len(b)),
        'json_to_end': (i, len(b)),
        'entire': (0, len(b)),
    }
    algos = {
        'crc32': lambda d: zlib.crc32(d) & 0xffffffff,
        'crc32xor': lambda d: zlib.crc32(d) ^ 0xffffffff,
        'unreal': unreal_crc,
        'unrealxor': lambda d: unreal_crc(d) ^ 0xffffffff,
        'fnv1a': fnv1a,
        'fnv1': fnv1,
        'md5lo': lambda d: struct.unpack('<I', hashlib.md5(d).digest()[:4])[0],
        'md5be': lambda d: struct.unpack('>I', hashlib.md5(d).digest()[:4])[0],
    }
    for rn, (a, z) in ranges.items():
        for an, fn in algos.items():
            v = fn(b[a:z])
            for tn, tv in targets.items():
                if v == tv:
                    print('  MATCH', tn, rn, an)
