// Compile stb_image's implementation purely for its public zlib decoder.
// STB_IMAGE_STATIC keeps these symbols file-local so they don't collide with the
// UI's own stb_image implementation when ripple_core is linked into ripple_rl.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#include "stb_image.h"

#include "Zlib.h"

#include <algorithm>
#include <stdexcept>

namespace zlibx {

std::uint32_t Adler32(const std::string& d) {
    std::uint32_t a = 1, b = 0;
    std::size_t i = 0, n = d.size();
    while (i < n) {
        std::size_t k = std::min<std::size_t>(n - i, 5552);
        for (std::size_t j = 0; j < k; ++j) {
            a += (unsigned char)d[i + j];
            b += a;
        }
        a %= 65521;
        b %= 65521;
        i += k;
    }
    return (b << 16) | a;
}

std::string Inflate(const std::string& comp, std::size_t expectedSize) {
    std::string out(expectedSize, '\0');
    int n = stbi_zlib_decode_buffer(out.data(), (int)expectedSize, comp.data(), (int)comp.size());
    if (n < 0 || (std::size_t)n != expectedSize) throw std::runtime_error("zlib: inflate failed");
    return out;
}

std::string InflateRaw(const std::string& comp, std::size_t expectedSize) {
    if (expectedSize == 0) return "";
    std::string out(expectedSize, '\0');
    int n = stbi_zlib_decode_noheader_buffer(out.data(), (int)expectedSize, comp.data(), (int)comp.size());
    if (n < 0 || (std::size_t)n != expectedSize) throw std::runtime_error("zlib: raw inflate failed");
    return out;
}

std::string Store(const std::string& raw) {
    std::string out;
    out.push_back((char)0x78);  // CMF: CM=8, CINFO=7
    out.push_back((char)0x01);  // FLG: FLEVEL=0, FCHECK so 0x7801 % 31 == 0
    std::size_t n = raw.size(), off = 0;
    if (n == 0) {
        out.push_back((char)0x01);  // BFINAL=1, BTYPE=00 (stored)
        out.push_back(0x00);
        out.push_back(0x00);  // LEN = 0
        out.push_back((char)0xFF);
        out.push_back((char)0xFF);  // NLEN = ~0
    } else {
        while (off < n) {
            std::size_t c = std::min<std::size_t>(65535, n - off);
            bool final = (off + c >= n);
            out.push_back((char)(final ? 1 : 0));  // BFINAL in bit 0, BTYPE=00
            unsigned short len = (unsigned short)c;
            unsigned short nlen = (unsigned short)(~len);
            out.push_back((char)(len & 0xFF));
            out.push_back((char)((len >> 8) & 0xFF));
            out.push_back((char)(nlen & 0xFF));
            out.push_back((char)((nlen >> 8) & 0xFF));
            out.append(raw, off, c);
            off += c;
        }
    }
    std::uint32_t ad = Adler32(raw);  // zlib stores adler32 big-endian
    out.push_back((char)((ad >> 24) & 0xFF));
    out.push_back((char)((ad >> 16) & 0xFF));
    out.push_back((char)((ad >> 8) & 0xFF));
    out.push_back((char)(ad & 0xFF));
    return out;
}

}  // namespace zlibx
