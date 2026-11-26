import os, json, struct
d = os.path.join(os.environ['USERPROFILE'], 'Documents', 'My Games', 'Rocket League', 'TAGame', 'Cache', 'WebCache')
for n in os.listdir(d):
    if n.endswith('.ripplebak'):
        continue
    b = open(os.path.join(d, n), 'rb').read()
    if b'LIVE REGISTER TEST' not in b:
        continue
    i = b.find(b'{"RegionConfig"')
    p = b.rfind(b'\x08\x00\x00\x00DataCRC')
    c = json.loads(b[i:p])
    t = [x for x in c['PlayerTitleConfig']['Titles'] if x['ID'] == 'CustomTitle_abcdef123456']
    cat = [x for x in c['PlayerTitleConfig']['Categories'] if x['ID'].startswith('ripple_custom_')]
    print(n[:22], 'len', len(b), 'hdr', struct.unpack_from('<I', b, 0)[0] == len(b) - 8,
          'field', struct.unpack_from('<I', b, 8)[0] == len(b) - 4, t, cat)
print('backups', len([n for n in os.listdir(d) if n.endswith('.ripplebak')]))
