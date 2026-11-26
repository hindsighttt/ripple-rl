#include "Upk.h"

#include "Aes.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

// ---- little-endian field access with bounds checks (mirrors struct.unpack) ----
int32_t RdI32(const std::string& b, long long o) {
    if (o < 0 || (size_t)(o + 4) > b.size()) throw std::runtime_error("upk: read past end");
    int32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
int64_t RdI64(const std::string& b, long long o) {
    if (o < 0 || (size_t)(o + 8) > b.size()) throw std::runtime_error("upk: read past end");
    int64_t v;
    std::memcpy(&v, b.data() + o, 8);
    return v;
}
uint64_t RdU64(const std::string& b, long long o) {
    if (o < 0 || (size_t)(o + 8) > b.size()) throw std::runtime_error("upk: read past end");
    uint64_t v;
    std::memcpy(&v, b.data() + o, 8);
    return v;
}
void WrI32(std::string& b, long long o, int32_t v) {
    if (o < 0 || (size_t)(o + 4) > b.size()) throw std::runtime_error("upk: write past end");
    std::memcpy(&b[o], &v, 4);
}
void WrI64(std::string& b, long long o, int64_t v) {
    if (o < 0 || (size_t)(o + 8) > b.size()) throw std::runtime_error("upk: write past end");
    std::memcpy(&b[o], &v, 8);
}

std::string LowerAscii(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool IEquals(const std::string& a, const std::string& b) { return LowerAscii(a) == LowerAscii(b); }

bool EndsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// Strict base64 decode (matches base64.b64decode(..., validate=True)); throws on
// any non-alphabet character.
int B64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
std::string B64Decode(const std::string& in) {
    std::string s;
    for (char c : in)
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t') s.push_back(c);
    if (s.size() % 4 != 0) throw std::runtime_error("b64");
    std::string out;
    size_t pad = 0;
    for (size_t i = 0; i < s.size(); i += 4) {
        int n[4];
        for (int j = 0; j < 4; ++j) {
            if (s[i + j] == '=') {
                n[j] = 0;
                ++pad;
            } else {
                n[j] = B64Val(s[i + j]);
                if (n[j] < 0) throw std::runtime_error("b64");
            }
        }
        uint32_t trip = (n[0] << 18) | (n[1] << 12) | (n[2] << 6) | n[3];
        out.push_back((char)((trip >> 16) & 0xFF));
        out.push_back((char)((trip >> 8) & 0xFF));
        out.push_back((char)(trip & 0xFF));
    }
    if (pad) out.resize(out.size() - pad);
    return out;
}

std::string HexToBytes(const std::string& hex) {
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back((char)((v(hex[i]) << 4) | v(hex[i + 1])));
    return out;
}

const char* kDefaultKeyHex = "c7df6b13252acc7147bb51c98ad7e34b7fe500b77fa5fab293e2f24e6b17e779";

std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("upk: cannot open " + p.string());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void WriteAll(const fs::path& p, const std::string& data) {
    fs::path tmp = p;
    tmp += ".ripple-tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(data.data(), (std::streamsize)data.size());
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
}

}  // namespace

namespace upk {

Header ParseHeader(const std::string& b) {
    if (b.size() < 60) throw std::runtime_error("not a package");
    uint32_t tag;
    std::memcpy(&tag, b.data(), 4);
    if (tag != 0x9E2A83C1u) throw std::runtime_error("not a package");
    Header h;
    h.hs = RdI32(b, 8);
    h.nc = RdI32(b, 25);
    h.no = RdI32(b, 29);
    h.ec = RdI32(b, 33);
    h.eo = RdI32(b, 37);
    h.ic = RdI32(b, 41);
    h.io = RdI32(b, 45);
    h.dep = RdI32(b, 49);
    h.u1 = RdI32(b, 53);
    if (h.no < 12 || (size_t)h.no > b.size()) throw std::runtime_error("bad header");
    h.garbage = RdI32(b, h.no - 12);
    h.ccio = RdI32(b, h.no - 8);
    h.lbs = RdI32(b, h.no - 4);
    h.content = h.hs - h.garbage - h.no;
    if (h.content <= 0 || (size_t)h.hs > b.size()) throw std::runtime_error("bad header");
    h.clen = (h.content + 15) / 16 * 16;
    return h;
}

Decrypted TryDecrypt(const std::string& b, const std::vector<std::string>& keys) {
    Header h = ParseHeader(b);
    std::string enc =
        ((size_t)(h.no + h.clen) <= b.size()) ? b.substr((size_t)h.no, (size_t)h.clen) : std::string();
    for (const auto& k : keys) {
        std::string dec;
        try {
            dec = aes::EcbDecrypt(k, enc);
        } catch (...) {
            continue;
        }
        if (dec.size() < 12) continue;
        int32_t ln;
        std::memcpy(&ln, dec.data(), 4);
        if (ln >= 1 && ln <= 128) {
            bool ok = true;
            for (int i = 0; i < ln - 1 && (size_t)(4 + i) < dec.size(); ++i) {
                unsigned char c = (unsigned char)dec[4 + i];
                if (c == 0) continue;
                if (c >= 128) {
                    ok = false;
                    break;
                }
            }
            if (ok) return {h, std::move(dec), k};
        }
    }
    throw std::runtime_error("no key");
}

const std::string& DefaultKey() {
    static std::string k = HexToBytes(kDefaultKeyHex);
    return k;
}

const std::vector<std::string>& LoadKeys(const fs::path& dataDir) {
    static std::vector<std::string> cache;
    static bool done = false;
    if (done) return cache;
    cache.push_back(HexToBytes(kDefaultKeyHex));
    std::ifstream f(dataDir / "keys.txt", std::ios::binary);
    if (f) {
        std::string line;
        bool first = true;
        while (std::getline(f, line)) {
            if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
                (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                line.erase(0, 3);  // strip UTF-8 BOM
            first = false;
            // trim
            size_t a = line.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) continue;
            size_t z = line.find_last_not_of(" \t\r\n");
            std::string tok = line.substr(a, z - a + 1);
            try {
                std::string k = B64Decode(tok);
                if (k.size() == 32 && std::find(cache.begin(), cache.end(), k) == cache.end())
                    cache.push_back(k);
            } catch (...) {
            }
        }
    }
    done = true;
    return cache;
}

std::string PkgBasename(const std::string& filename) {
    if (EndsWith(LowerAscii(filename), "_sf.upk")) return filename.substr(0, filename.size() - 7);
    return filename.size() >= 4 ? filename.substr(0, filename.size() - 4) : filename;
}

std::string Convert(const std::string& srcBytes, const std::string& srcBasename,
                    const std::string& targetName, const std::vector<std::string>& keys) {
    std::string b = srcBytes;
    Header h = ParseHeader(b);

    // try_decrypt: find the key whose plaintext name table starts sane.
    std::string enc =
        ((size_t)(h.no + h.clen) <= b.size()) ? b.substr((size_t)h.no, (size_t)h.clen) : std::string();
    std::string d, key;
    bool found = false;
    for (const auto& k : keys) {
        std::string dec;
        try {
            dec = aes::EcbDecrypt(k, enc);
        } catch (...) {
            continue;
        }
        if (dec.size() < 12) continue;
        int32_t ln;
        std::memcpy(&ln, dec.data(), 4);
        if (ln >= 1 && ln <= 128) {
            bool ok = true;
            for (int i = 0; i < ln - 1 && (size_t)(4 + i) < dec.size(); ++i) {
                unsigned char c = (unsigned char)dec[4 + i];
                if (c == 0) continue;
                if (c >= 128) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                d = std::move(dec);
                key = k;
                found = true;
                break;
            }
        }
    }
    if (!found) throw std::runtime_error("no key");

    // Walk the name table to find the source package's own name entry.
    long long p = 0, srcidx = -1;
    uint64_t flags = 0;
    for (long long i = 0; i < h.nc; ++i) {
        int32_t ln = RdI32(d, p);
        std::string s;
        if (ln >= 1 && (size_t)(p + 4 + ln - 1) <= d.size()) s = d.substr((size_t)(p + 4), (size_t)(ln - 1));
        uint64_t fl = RdU64(d, p + 4 + ln);
        p += 4 + ln + 8;
        if (IEquals(s, srcBasename)) {
            srcidx = i;
            flags = fl;
        }
    }
    if (srcidx < 0) throw std::runtime_error("source name missing");
    long long name_end = p;

    // New name entry: <len+1><bytes>\0<flags u64>.
    std::string entry;
    int32_t L = (int32_t)(targetName.size() + 1);
    entry.append((const char*)&L, 4);
    entry += targetName;
    entry.push_back('\0');
    entry.append((const char*)&flags, 8);

    long long delta = (long long)entry.size();
    long long content_new = h.content + delta;
    long long clen_new = (content_new + 15) / 16 * 16;
    long long hs_delta = clen_new - h.clen;
    long long hs_new = h.hs + hs_delta;

    d = d.substr(0, (size_t)name_end) + entry + d.substr((size_t)name_end);
    if (clen_new > (long long)d.size()) d.append((size_t)(clen_new - (long long)d.size()), '\0');

    // Header fixups (offsets after the name table shift by delta).
    WrI32(b, 8, (int32_t)hs_new);
    WrI32(b, 25, (int32_t)(h.nc + 1));
    WrI32(b, 37, (int32_t)(h.eo + delta));
    WrI32(b, 45, (int32_t)(h.io + delta));
    WrI32(b, 49, (int32_t)(h.dep + delta));
    WrI32(b, 53, (int32_t)(h.u1 + delta));
    WrI32(b, h.no - 8, (int32_t)(h.ccio + delta));

    // Compressed-chunk table: bump each chunk's file offset by the header growth.
    int32_t cnt = RdI32(d, h.ccio + delta);
    long long q = h.ccio + delta + 4;
    for (int32_t i = 0; i < cnt; ++i) {
        int64_t co = RdI64(d, q + 12);
        WrI64(d, q + 12, co + hs_delta);
        q += 24;
    }

    // Export table: repoint the source export's ObjectName to the new name.
    long long pe = h.eo - h.no + delta;
    for (long long i = 0; i < h.ec; ++i) {
        int32_t oname = RdI32(d, pe + 12);
        if (oname == (int32_t)srcidx) WrI32(d, pe + 12, (int32_t)h.nc);
        int32_t netc = RdI32(d, pe + 48);
        pe += 52 + 4 * (long long)std::max(netc, 0) + 16 + 4;
    }

    std::string enc_out = aes::EcbEncrypt(key, d);

    auto sub = [](const std::string& s, long long pos, long long cnt) -> std::string {
        if (pos < 0) pos = 0;
        if ((size_t)pos >= s.size()) return std::string();
        long long avail = (long long)s.size() - pos;
        if (cnt < 0 || cnt > avail) cnt = avail;
        return s.substr((size_t)pos, (size_t)cnt);
    };
    return sub(b, 0, h.no) + enc_out + sub(b, h.no + h.clen, h.hs - (h.no + h.clen)) + sub(b, h.hs, -1);
}

void SwapFile(const fs::path& gameFile, const fs::path& sourceFile, const std::string& targetName,
              const fs::path& dataDir, const fs::path& backupDir) {
    std::error_code ec;
    fs::create_directories(backupDir, ec);
    fs::path bak = backupDir / gameFile.filename();
    if (!fs::exists(bak, ec)) fs::copy_file(gameFile, bak, ec);
    const auto& keys = LoadKeys(dataDir);
    std::string src = ReadAll(sourceFile);
    std::string srcbase = PkgBasename(sourceFile.filename().string());
    std::string out = Convert(src, srcbase, targetName, keys);
    WriteAll(gameFile, out);
}

void RestoreFile(const fs::path& gameFile, const fs::path& backupDir) {
    std::error_code ec;
    fs::path b1 = backupDir / gameFile.filename();
    fs::path b2 = gameFile;
    b2 += ".ripplebak";
    for (const auto& bak : {b1, b2}) {
        if (fs::exists(bak, ec)) {
            fs::copy_file(bak, gameFile, fs::copy_options::overwrite_existing, ec);
            return;
        }
    }
}

}  // namespace upk
