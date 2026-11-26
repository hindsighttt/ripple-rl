import os, struct, hashlib, shutil, base64, time
from Crypto.Cipher import AES

DEFAULT_KEY = bytes.fromhex(
    "c7df6b13252acc7147bb51c98ad7e34b7fe500b77fa5fab293e2f24e6b17e779"
)
KEY_CACHE = None


def load_keys(data_dir):
    global KEY_CACHE
    if KEY_CACHE is not None:
        return KEY_CACHE
    keys = [DEFAULT_KEY]
    p = os.path.join(data_dir, "keys.txt")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8-sig", errors="ignore"):
            try:
                k = base64.b64decode(line.strip(), validate=True)
                if len(k) == 32 and k not in keys:
                    keys.append(k)
            except Exception:
                pass
    KEY_CACHE = keys
    return keys


class UPKError(Exception):
    pass


class Header:
    def __init__(self, b):
        self.b = b
        self.tag, self.ver, self.lic = struct.unpack_from("<IHH", b, 0)
        if self.tag != 0x9E2A83C1:
            raise UPKError("not a package")
        self.hs = struct.unpack_from("<i", b, 8)[0]
        self.nc, self.no = struct.unpack_from("<ii", b, 25)
        self.ec, self.eo = struct.unpack_from("<ii", b, 33)
        self.ic, self.io = struct.unpack_from("<ii", b, 41)
        self.dep = struct.unpack_from("<i", b, 49)[0]
        self.u1 = struct.unpack_from("<i", b, 53)[0]
        self.garbage = struct.unpack_from("<i", b, self.no - 12)[0]
        self.ccio = struct.unpack_from("<i", b, self.no - 8)[0]
        self.lbs = struct.unpack_from("<i", b, self.no - 4)[0]
        self.content = self.hs - self.garbage - self.no
        if self.content <= 0 or self.hs > len(b):
            raise UPKError("bad header")
        self.clen = (self.content + 15) // 16 * 16


def try_decrypt(b, keys):
    h = Header(b)
    enc = b[h.no:h.no + h.clen]
    for i, k in enumerate(keys):
        try:
            d = AES.new(k, AES.MODE_ECB).decrypt(enc)
        except Exception:
            continue
        # name table must start with a short FString and be printable
        if len(d) < 12:
            continue
        ln = struct.unpack_from("<i", d, 0)[0]
        if 1 <= ln <= 128:
            s = d[4:4 + ln]
            if s[:ln - 1].replace(b"\x00", b"").isascii():
                return h, d, k, i
    raise UPKError("no key")


def pkg_basename(path):
    name = os.path.basename(path)
    return name[:-7] if name.lower().endswith("_sf.upk") else name[:-4]


def convert(src_path, target_name, data_dir, out_path=None):
    """Copy a package to out_path (or return bytes) renaming its object to target_name."""
    keys = load_keys(data_dir)
    b = bytearray(open(src_path, "rb").read())
    h, d, key, _ = try_decrypt(bytes(b), keys)
    d = bytearray(d)
    p = 0
    srcidx = None
    flags = None
    srcbase = pkg_basename(src_path)
    for i in range(h.nc):
        ln = struct.unpack_from("<i", d, p)[0]
        s = bytes(d[p + 4:p + 4 + ln - 1]).decode("latin1")
        fl = struct.unpack_from("<Q", d, p + 4 + ln)[0]
        p += 4 + ln + 8
        if s.lower() == srcbase.lower():
            srcidx, flags = i, fl
    if srcidx is None:
        raise UPKError("source name missing")
    name_end = p
    entry = struct.pack("<i", len(target_name) + 1) + target_name.encode() + b"\0" + struct.pack("<Q", flags or 0)
    delta = len(entry)
    content_new = h.content + delta
    clen_new = (content_new + 15) // 16 * 16
    hs_delta = clen_new - h.clen
    hs_new = h.hs + hs_delta
    d = d[:name_end] + entry + d[name_end:]
    d += b"\0" * (clen_new - len(d))
    b[8:12] = struct.pack("<i", hs_new)
    b[25:29] = struct.pack("<i", h.nc + 1)
    b[37:41] = struct.pack("<i", h.eo + delta)
    b[45:49] = struct.pack("<i", h.io + delta)
    b[49:53] = struct.pack("<i", h.dep + delta)
    b[53:57] = struct.pack("<i", h.u1 + delta)
    b[h.no - 8:h.no - 4] = struct.pack("<i", h.ccio + delta)
    cnt = struct.unpack_from("<i", d, h.ccio + delta)[0]
    q = h.ccio + delta + 4
    for _ in range(cnt):
        co = struct.unpack_from("<q", d, q + 12)[0]
        d[q + 12:q + 20] = struct.pack("<q", co + hs_delta)
        q += 24
    p = h.eo - h.no + delta
    for _ in range(h.ec):
        oname = struct.unpack_from("<i", d, p + 12)[0]
        if oname == srcidx:
            d[p + 12:p + 16] = struct.pack("<i", h.nc)
        netc = struct.unpack_from("<i", d, p + 48)[0]
        p += 52 + 4 * max(netc, 0) + 16 + 4
    enc = AES.new(key, AES.MODE_ECB).encrypt(bytes(d))
    out = bytes(b[:h.no]) + enc + bytes(b[h.no + h.clen:h.hs]) + bytes(b[h.hs:])
    if out_path:
        with open(out_path, "wb") as f:
            f.write(out)
        return out_path
    return out


def swap_file(game_file, source_file, target_name, data_dir, backup_dir=None):
    from . import store as _store
    bak_dir = backup_dir or os.path.join(_store.BACKUP_DIR, "Epic")
    os.makedirs(bak_dir, exist_ok=True)
    bak = os.path.join(bak_dir, os.path.basename(game_file))
    if not os.path.exists(bak):
        shutil.copy2(game_file, bak)
    convert(source_file, target_name, data_dir, out_path=game_file)


def restore_file(game_file, backup_dir=None):
    from . import store as _store
    bak_dir = backup_dir or os.path.join(_store.BACKUP_DIR, "Epic")
    for bak in (os.path.join(bak_dir, os.path.basename(game_file)), game_file + ".ripplebak"):
        if os.path.exists(bak):
            shutil.copy2(bak, game_file)
            return

