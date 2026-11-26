#include "Zip.h"

#include "Zlib.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

uint16_t U16(const std::string& b, size_t o) {
    if (o + 2 > b.size()) throw std::runtime_error("zip: truncated");
    return (uint8_t)b[o] | ((uint8_t)b[o + 1] << 8);
}
uint32_t U32(const std::string& b, size_t o) {
    if (o + 4 > b.size()) throw std::runtime_error("zip: truncated");
    return (uint32_t)(uint8_t)b[o] | ((uint32_t)(uint8_t)b[o + 1] << 8) |
           ((uint32_t)(uint8_t)b[o + 2] << 16) | ((uint32_t)(uint8_t)b[o + 3] << 24);
}

std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("zip: cannot open " + p.string());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

namespace zip {

void Extract(const fs::path& zipPath, const fs::path& destDir) {
    std::string b = ReadAll(zipPath);
    if (b.size() < 22) throw std::runtime_error("zip: too small");

    // Locate the End Of Central Directory record.
    long long eocd = -1;
    long long minI = (long long)b.size() - 22 - 65536;
    for (long long i = (long long)b.size() - 22; i >= 0 && i >= minI; --i) {
        if ((uint8_t)b[i] == 0x50 && (uint8_t)b[i + 1] == 0x4b && (uint8_t)b[i + 2] == 0x05 &&
            (uint8_t)b[i + 3] == 0x06) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) throw std::runtime_error("zip: end-of-central-directory not found");

    uint16_t entries = U16(b, eocd + 10);
    uint32_t cdOffset = U32(b, eocd + 16);

    std::error_code ec;
    fs::create_directories(destDir, ec);
    fs::path root = fs::weakly_canonical(destDir, ec);

    size_t p = cdOffset;
    for (uint16_t idx = 0; idx < entries; ++idx) {
        if (U32(b, p) != 0x02014b50) throw std::runtime_error("zip: bad central-directory header");
        uint16_t method = U16(b, p + 10);
        uint32_t compSize = U32(b, p + 20);
        uint32_t uncompSize = U32(b, p + 24);
        uint16_t nameLen = U16(b, p + 28);
        uint16_t extraLen = U16(b, p + 30);
        uint16_t commentLen = U16(b, p + 32);
        uint32_t localOff = U32(b, p + 42);
        std::string name = b.substr(p + 46, nameLen);
        p += 46 + nameLen + extraLen + commentLen;

        if (compSize == 0xFFFFFFFFu || uncompSize == 0xFFFFFFFFu || localOff == 0xFFFFFFFFu)
            throw std::runtime_error("zip: zip64 archives are not supported");

        std::string rel = name;
        for (char& c : rel) if (c == '\\') c = '/';
        bool isDir = !rel.empty() && rel.back() == '/';

        fs::path target = fs::weakly_canonical(destDir / fs::path(rel), ec);
        std::wstring rootStr = root.wstring(), tgtStr = target.wstring();
        if (tgtStr.size() < rootStr.size() || tgtStr.compare(0, rootStr.size(), rootStr) != 0)
            throw std::runtime_error("zip: entry escapes destination: " + rel);

        if (isDir) {
            fs::create_directories(target, ec);
            continue;
        }
        fs::create_directories(target.parent_path(), ec);

        if (U32(b, localOff) != 0x04034b50) throw std::runtime_error("zip: bad local header");
        uint16_t lNameLen = U16(b, localOff + 26);
        uint16_t lExtraLen = U16(b, localOff + 28);
        size_t dataPos = (size_t)localOff + 30 + lNameLen + lExtraLen;
        if (dataPos + compSize > b.size()) throw std::runtime_error("zip: truncated entry data");
        std::string comp = b.substr(dataPos, compSize);

        std::string raw;
        if (method == 0)
            raw = comp;
        else if (method == 8)
            raw = zlibx::InflateRaw(comp, uncompSize);
        else
            throw std::runtime_error("zip: unsupported compression method");

        std::ofstream of(target, std::ios::binary | std::ios::trunc);
        of.write(raw.data(), (std::streamsize)raw.size());
    }
}

}  // namespace zip
