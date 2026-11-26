import os, struct, xxhash
base = os.path.join(os.environ['USERPROFILE'], 'Documents', 'My Games', 'Rocket League', 'TAGame', 'Cache')
files = {
 'before': (os.path.join(base, 'WebCache', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='), 0xdf3302ad),
 'after': (os.path.join(base, 'WebCache.before-launcher-only-20261125-163354', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='), 0x20b49e8c),
}

def murmur3_32(data, seed=0):
    c1 = 0xcc9e2d51; c2 = 0x1b873593
    h = seed
    n = len(data) // 4
    for i in range(n):
        k = struct.unpack_from('<I', data, i*4)[0]
        k = (k * c1) & 0xffffffff
        k = ((k << 15) | (k >> 17)) & 0xffffffff
        k = (k * c2) & 0xffffffff
        h ^= k
        h = ((h << 13) | (h >> 19)) & 0xffffffff
        h = (h * 5 + 0xe6546b64) & 0xffffffff
    tail = data[n*4:]
    k = 0
    for i, x in enumerate(tail):
        k |= x << (8*i)
    if tail:
        k = (k * c1) & 0xffffffff
        k = ((k << 15) | (k >> 17)) & 0xffffffff
        k = (k * c2) & 0xffffffff
        h ^= k
    h ^= len(data)
    h ^= h >> 16
    h = (h * 0x85ebca6b) & 0xffffffff
    h ^= h >> 13
    h = (h * 0xc2b2ae35) & 0xffffffff
    h ^= h >> 16
    return h

for label, (path, target) in files.items():
    b = open(path, 'rb').read()
    i = b.find(b'{"RegionConfig"')
    prop = b.rfind(b'\x08\x00\x00\x00DataCRC')
    body = b[i:prop]
    print('===', label, hex(target))
    for seed in (0, 1, 0xffffffff, 0x9747b28c):
        if murmur3_32(body, seed) == target:
            print(' murmur MATCH seed', seed)
        if xxhash.xxh32(body, seed=seed).intdigest() == target:
            print(' xxh32 MATCH seed', seed)
    for d in (body + b'\x00', b[8:]):
        for seed in (0, 1, 0xffffffff):
            if murmur3_32(d, seed) == target:
                print(' murmur range MATCH', seed, len(d))
            if xxhash.xxh32(d, seed=seed).intdigest() == target:
                print(' xxh32 range MATCH', seed, len(d))
