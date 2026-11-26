import os, base64, hmac, struct, zlib

KEYS = {
    'resp': b"3b932153785842ac927744b292e40e52",
    'req': b"c338bd36fb8c42b1a431d30add939fc7",
}
d = os.path.join(os.environ['USERPROFILE'], 'Documents', 'My Games', 'Rocket League',
                 'TAGame', 'Cache', 'WebCache')
name = 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='
b = open(os.path.join(d, name), 'rb').read()
i = b.find(b'{"RegionConfig"')
prop = b.rfind(b'\x08\x00\x00\x00DataCRC')
body = b[i:prop]
print('filelen', len(b), 'jsonstart', i, 'props', prop, 'bodylen', len(body))


def parse_props(buf, p):
    out = {}
    while p + 4 <= len(buf):
        n = struct.unpack_from('<I', buf, p)[0]
        if n == 0 or n > 64 or p + 4 + n > len(buf):
            break
        p += 4
        key = buf[p:p+n].rstrip(b'\x00')
        p += n
        if p + 4 > len(buf):
            break
        typelen = struct.unpack_from('<I', buf, p)[0]; p += 4
        if typelen == 0 or typelen > 64 or p + typelen > len(buf):
            break
        typ = buf[p:p+typelen].rstrip(b'\x00'); p += typelen
        a = struct.unpack_from('<I', buf, p)[0]; p += 4
        c = struct.unpack_from('<I', buf, p)[0]; p += 4
        if typ == b'IntProperty':
            v = struct.unpack_from('<I', buf, p)[0]; p += 4
        else:
            slen = struct.unpack_from('<I', buf, p)[0]; p += 4
            v = buf[p:p+slen]
            p += slen
        out[key.decode()] = (typ.decode(), v, (a, c))
    return out, p

props, end = parse_props(b, prop)
for k, v in props.items():
    print('PROP', k, v[0], (v[1] if not isinstance(v[1], bytes) else repr(v[1][:60])), v[2])
print('end', end, 'filelen', len(b), 'tail', repr(b[end:]))

crc = props['DataCRC'][1]
print('stored crc', hex(crc))
for k, v in {'crc32': zlib.crc32(body) & 0xffffffff, 'crc32+0': zlib.crc32(body + b'\x00') & 0xffffffff,
             'adler': zlib.adler32(body) & 0xffffffff}.items():
    print(k, hex(v), v == crc)

sig = props['Signature'][1].rstrip(b'\x00')
print('sig', sig)
for kn, key in KEYS.items():
    for ml, msg in {'body': body, 'body+0': body + b'\x00', 'dash-body': b'-' + body,
                    'body+crctime': body}.items():
        s = base64.b64encode(hmac.digest(key, msg, 'sha256'))
        print(kn, ml, s == sig, s.decode())
