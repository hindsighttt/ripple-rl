// ripple_upktool: thin CLI around upk::Convert, used to prove the native port is
// byte-for-byte identical to ripple/upk.py's convert().
//
//   ripple_upktool <srcPackage> <targetName> <dataDir> <outPath>
//
// Mirrors upk.convert(src_path, target_name, data_dir, out_path): it reads
// srcPackage, derives the source basename from its filename, converts, and
// writes outPath.
#include "Upk.h"
#include "WinUtil.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 5) {
        std::fputs("usage: ripple_upktool <srcPackage> <targetName> <dataDir> <outPath>\n", stderr);
        return 2;
    }
    try {
        fs::path src = argv[1];
        std::string targetName = win::Narrow(argv[2]);
        fs::path dataDir = argv[3];
        fs::path out = argv[4];

        std::string srcBytes = ReadAll(src);
        const auto& keys = upk::LoadKeys(dataDir);
        std::string srcBase = upk::PkgBasename(src.filename().string());
        std::string result = upk::Convert(srcBytes, srcBase, targetName, keys);

        std::ofstream of(out, std::ios::binary | std::ios::trunc);
        of.write(result.data(), (std::streamsize)result.size());
        std::fprintf(stderr, "wrote %zu bytes\n", result.size());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
