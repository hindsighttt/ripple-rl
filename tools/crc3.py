import os, zlib, struct
base = os.path.join(os.environ['USERPROFILE'], 'Documents', 'My Games', 'Rocket League', 'TAGame', 'Cache')
files = {
 'before': (os.path.join(base, 'WebCache', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='), 0xdf3302ad),
 'after': (os.path.join(base, 'WebCache.before-launcher-only-20261125-163354', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='), 0x20b49e8c),
}

def unreal(data, crc=0):
    tab = []
    for n in range(256):
        c = (n << 24) & 0xffffffff
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) & 0xffffffff if c & 0x80000000 else (c << 1) & 0xffffffff
        tab.append(c)
    for x in data:
        crc = ((crc << 8) ^ tab[((crc >> 24) ^ x) & 0xff]) & 0xffffffff
    return crc

for label, (path, target) in files.items():
    b = open(path, 'rb').read()
    i = b.find(b'{"RegionConfig"')
    prop = b.rfind(b'\x08\x00\x00\x00DataCRC')
    body = b[i:prop]
    fields = [str(struct.unpack_from('<I', b, x)[0]).encode() for x in (0, 4, 8)]
    fields += [b'3208553', str(len(body)).encode(), b'application/json']
    print('===', label, hex(target))
    cands = {'body': body, 'body0': body + b'\x00'}
    for f in fields:
        for sep in (b'-', b'', b':'):
            cands['f%s%s' % (f[:6], sep)] = f + sep + body
            cands['body+%s' % f[:6]] = body + f
    for n, d in cands.items():
        if zlib.crc32(d) & 0xffffffff == target:
            print(' crc32 MATCH', n)
        if unreal(d) == target:
            print(' unreal MATCH', n)
