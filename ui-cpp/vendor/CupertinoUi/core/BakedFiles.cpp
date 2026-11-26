#include "BakedFiles.h"

#include <algorithm>

namespace Cupertino {
    std::span<const unsigned char> FindBakedFile(std::span<const BakedFile> files, std::string_view name) {
        const auto found = std::lower_bound(files.begin(), files.end(), name, [](const BakedFile& file, std::string_view key) { return std::string_view(file.name) < key; });
        if (found == files.end() || name != found->name)
            return {};
        return {reinterpret_cast<const unsigned char*>(found->words), found->size};
    }

#if defined(RIPPLE_RL_USE_INSTRUMENT_SANS) || __has_include("generated/BakedFonts.inc")
    namespace Baked {
#if defined(RIPPLE_RL_USE_INSTRUMENT_SANS)
#include "RippleFonts.inc"
#else
#include "generated/BakedFonts.inc"
#endif
    } // namespace Baked

    std::span<const BakedFile> BakedFonts() {
        return Baked::BakedFontFiles;
    }
#else
    std::span<const BakedFile> BakedFonts() {
        return {};
    }
#endif
} // namespace Cupertino
