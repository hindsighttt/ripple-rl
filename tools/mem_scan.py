import os
from pathlib import Path
import sys

p = Path(sys.argv[1]) if len(sys.argv) > 1 else (
    Path(os.environ["APPDATA"]) / "RippleRL" / "research" / "reference-runtime.bin"
)
needles = [b'PlayerTitleConfig', b'PlayerTitle', b'TitleConfig_X',
           b'Categories', b'DateGuard', b'DataCRC', b'PlayerTitles',
           b'GlowColor', b'CustomTitle_', b'WebCache', b'config.psynet.gg']
found = {n: [] for n in needles}
CH = 64 * 1024 * 1024
overlap = 256
with open(p, 'rb') as f:
    pos = 0
    prev = b''
    while True:
        buf = f.read(CH)
        if not buf:
            break
        data = prev + buf
        base = pos - len(prev)
        for n in needles:
            start = 0
            while True:
                i = data.find(n, start)
                if i < 0:
                    break
                absi = base + i
                if len(found[n]) < 8:
                    ctx = data[max(0, i-120):i+200]
                    found[n].append((absi, ctx))
                start = i + 1
        prev = data[-overlap:]
        pos += len(buf)
for n in needles:
    print('=====', n, len(found[n]))
    for off, ctx in found[n][:4]:
        print(' off', off)
        print('  ', repr(ctx))
