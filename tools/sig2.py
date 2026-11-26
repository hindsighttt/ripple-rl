import os, base64, hmac, struct
KEY = b"3b932153785842ac927744b292e40e52"
base = os.path.join(os.environ['USERPROFILE'], 'Documents', 'My Games', 'Rocket League', 'TAGame', 'Cache')
files = {
 'before': os.path.join(base, 'WebCache', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='),
 'after': os.path.join(base, 'WebCache.before-launcher-only-20261125-163354', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='),
}
for label, path in files.items():
    b = open(path, 'rb').read()
    i = b.find(b'{"RegionConfig"')
    prop = b.rfind(b'\x08\x00\x00\x00DataCRC')
    body = b[i:prop]
    # signature parse
    k = b.find(b'Signature\x00', prop)
    p = k + len(b'Signature\x00')
    tl = struct.unpack_from('<I', b, p)[0]; p += 4 + tl
    p += 4
    p += 4
    sl = struct.unpack_from('<I', b, p)[0]; p += 4
    sig = b[p:p+sl].rstrip(b'\x00')
    targets = {
        'hdr0': str(struct.unpack_from('<I', b, 0)[0]),
        'hdr1': str(struct.unpack_from('<I', b, 4)[0]),
        'hdr2': str(struct.unpack_from('<I', b, 8)[0]),
        'datalen': str(len(body)),
        'etag': '3208553',
    }
    print('===', label)
    for n, t in targets.items():
        for sep in ('-', ''):
            s = base64.b64encode(hmac.digest(KEY, t.encode() + sep.encode() + body, 'sha256'))
            if s == sig:
                print(' MATCH', n, repr(sep))
    # try each header as epoch sec with .000? none
