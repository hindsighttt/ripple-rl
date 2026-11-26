import os, sys, struct, json, base64, hmac

def path(name):
    return os.path.join(os.environ['USERPROFILE'], 'Documents', 'My Games', 'Rocket League',
                        'TAGame', 'Cache', 'WebCache', name)

name = sys.argv[1] if len(sys.argv) > 1 else 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg=='
b = open(path(name), 'rb').read()
print('len', len(b))
i = b.find(b'{"RegionConfig"')
print('json start', i)
if i < 0:
    i = b.find(b'{')
    print('json start fallback', i)
print('prefix', b[:i].hex(' '))
# parse tail props
tail = b[-500:]
print('tail ascii repr:', repr(tail))
# find DataCRC
k = b.rfind(b'DataCRC')
print('DataCRC', k, b[k-20:k+120].hex(' '))
print('repr', repr(b[k-60:k+200]))
