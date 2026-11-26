#pragma once
// zlib helpers for the ProjectX compressed-chunk format (patches.py uses
// zlib.decompress / zlib.compress). Decompression uses stb_image's zlib decoder
// (already vendored). Compression emits valid *stored-block* zlib — correct and
// self-consistent; byte-exactness of the compressed bytes is not required
// because patches.build() verifies by decoding what it wrote.
#include <cstdint>
#include <string>

namespace zlibx {
std::string Inflate(const std::string& comp, std::size_t expectedSize);     // RFC1950 zlib stream
std::string InflateRaw(const std::string& comp, std::size_t expectedSize);  // RFC1951 raw DEFLATE (zip)
std::string Store(const std::string& raw);                                  // stored-block zlib stream
std::uint32_t Adler32(const std::string& d);
}  // namespace zlibx
