#pragma once
// Minimal zip extractor (stored + DEFLATE entries) for workshop maps and custom
// packs. Zip-slip protected. Uses stb_image's raw-DEFLATE decoder via Zlib.
#include <filesystem>

namespace zip {

// Extract every file entry of `zipPath` into `destDir` (created if needed).
// Throws std::runtime_error on a malformed archive, an unsupported method,
// zip64 sizes, or an entry that would escape `destDir`.
void Extract(const std::filesystem::path& zipPath, const std::filesystem::path& destDir);

}  // namespace zip
