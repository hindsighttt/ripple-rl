import os, base64, hmac, struct, zlib

KEY = b"3b932153785842ac927744b292e40e52"
REQ = b"c338bd36fb8c42b1a431d30add939fc7"
base = os.path.join(os.environ['USERPROFILE'], 'Documents', 'My Games', 'Rocket League', 'TAGame', 'Cache')
files = {
 'before': os.path.join(base, 'WebCache', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='),
 'after': os.path.join(base, 'WebCache.before-launcher-only-20261125-163354', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='),
}


def parse(path):
    b = open(path, 'rb').read()
    i = b.find(b'{"RegionConfig"')
    prop = b.rfind(b'\x08\x00\x00\x00DataCRC')
    body = b[i:prop]
    p = prop
    props = {}
    while p + 4 <= len(b):
        n = struct.unpack_from('<I', b, p)[0]
        if n == 0 or n > 64 or p + 4 + n > len(b):
            break
        p += 4
        key = b[p:p+n].rstrip(b'\x00'); p += n
        tl = struct.unpack_from('<I', b, p)[0]; p += 4
        if tl == 0 or p + tl > len(b):
            break
        typ = b[p:p+tl].rstrip(b'\x00'); p += tl
        a = struct.unpack_from('<I', b, p)[0]; p += 4
        c = struct.unpack_from('<I', b, p)[0]; p += 4
        if typ == b'IntProperty':
            v = struct.unpack_from('<I', b, p)[0]; p += 4
        else:
            sl = struct.unpack_from('<I', b, p)[0]; p += 4
            v = b[p:p+sl]; p += sl
        props[key.decode()] = v
    return b, body, props

for label, path in files.items():
    b, body, props = parse(path)
    print('===', label, 'filelen', len(b), 'bodylen', len(body))
    print(' prefix', b[:43].hex(' '))
    print(' DataCRC', hex(props['DataCRC']), 'ETag', props['ETag'], 'Sig', props['Signature'].rstrip(b'\x00'))
    print(' has custom', b'CustomTitle_' in body)
    tgt = props['Signature'].rstrip(b'\x00')
    cands = {}
    for tn, tv in [('crc', str(props['DataCRC'])), ('crc0', str(props['DataCRC'] & 0xffffffff)),
                   ('etag', props['ETag'].rstrip(b'\x00').decode())]:
        for sep in ('-', ''):
            cands[tn+sep+'body'] = tv.encode() + sep.encode() + body
    for kn, key in [('resp', KEY), ('req', REQ)]:
        for cn, msg in cands.items():
            s = base64.b64encode(hmac.digest(key, msg, 'sha256'))
            if s == tgt:
                print('  MATCH', kn, cn)
    # crc candidates
    for cn, v in [('body', body), ('body0', body + b'\x00')]:
        c = zlib.crc32(v) & 0xffffffff
        if c == props['DataCRC']:
            print('  CRC32 MATCH', cn)
