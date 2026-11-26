import os, sys, json, tempfile, shutil, struct, hashlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ripple import titles_config, features, store

src = os.path.join(os.environ['USERPROFILE'], 'Documents', 'My Games', 'Rocket League', 'TAGame', 'Cache', 'WebCache', 'NDIwOTYxNTMvUHJvZC9FcGljL0lOVC8_YnVpbGRTZWNyZXQ9PHJlZGFjdGVkPg==')
tmp = tempfile.mkdtemp()
dst = os.path.join(tmp, 'cfg')
shutil.copy2(src, dst)
orig = open(dst, 'rb').read()
state = tempfile.mkdtemp()
store.STATE_DIR = state
features.set_state('titles', {'enabled': True, 'titles': [
    {'id': 'CustomTitle_0123456789ab', 'text': 'REAL FILE TEST é', 'color': '#00FF00', 'glow': '#FF0000', 'custom': True}]})
changed = titles_config.refresh_webcache(tmp)
written = open(dst, 'rb').read()
prefix, body, props = titles_config._split_cache(written)
cfg = json.loads(body)
rec = [t for t in cfg['PlayerTitleConfig']['Titles'] if t['ID'] == 'CustomTitle_0123456789ab']
cat = [c for c in cfg['PlayerTitleConfig']['Categories'] if c['ID'].startswith('ripple_custom_')]
print('changed', changed, 'records', rec, 'cats', cat)
print('lengths ok', struct.unpack_from('<I', written, 0)[0] == len(written) - 8,
      struct.unpack_from('<I', written, 8)[0] == len(written) - 4)
print('props preserved', props == orig[orig.rfind(b'\x08\x00\x00\x00DataCRC'):])
print('prefix tail', prefix[-20:])
print('backup equal', open(dst + '.ripplebak', 'rb').read() == orig)
titles_config.restore_webcache(tmp)
print('restored equal', open(dst, 'rb').read() == orig)
shutil.rmtree(tmp); shutil.rmtree(state)
