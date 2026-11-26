// ripple_bootstraptool: proves the native ProjectX bootstrap matches patches.py
// at the semantic level (the two patched exports decode identically), since the
// re-compressed bytes legitimately differ (stored vs deflate blocks).
//
//   ripple_bootstraptool build <ProjectX.upk> <dataDir> <out.upk>
//   ripple_bootstraptool show  <ProjectX.upk> <dataDir>        (prints export hex)
#include "Bootstrap.h"
#include "Garage.h"
#include "Upk.h"
#include "WinUtil.h"

#include <cstdlib>

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
static std::string Hex(const std::string& b) {
    static const char* h = "0123456789abcdef";
    std::string o;
    for (unsigned char c : b) {
        o.push_back(h[c >> 4]);
        o.push_back(h[c & 0xF]);
    }
    return o;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::fputs("usage: ripple_bootstraptool build <in> <dataDir> <out> | show <in> <dataDir>\n", stderr);
        return 2;
    }
    try {
        std::string mode = win::Narrow(argv[1]);

        if (mode == "garagestatus") {
            if (argc < 4) {
                std::fputs("garagestatus needs <cookedDir> <patchDir>\n", stderr);
                return 2;
            }
            json st = garage::Status(fs::path(argv[2]), fs::path(argv[3]));
            std::printf("%s\n", st.dump().c_str());
            return 0;
        }

        fs::path in = argv[2];
        std::string data = ReadAll(in);

        if (mode == "flip") {
            if (argc < 5) {
                std::fputs("flip needs <in> <rgbHex> <out>\n", stderr);
                return 2;
            }
            long long rgb = std::strtoll(win::Narrow(argv[3]).c_str(), nullptr, 16);
            std::string out = flip::Build(data, rgb);
            std::ofstream of(fs::path(argv[4]), std::ios::binary | std::ios::trunc);
            of.write(out.data(), (std::streamsize)out.size());
            std::fprintf(stderr, "flipped %zu bytes\n", out.size());
            return 0;
        }

        fs::path dataDir = argv[3];
        const auto& keys = upk::LoadKeys(dataDir);

        if (mode == "build") {
            if (argc < 5) {
                std::fputs("build needs <out>\n", stderr);
                return 2;
            }
            std::string out = bootstrap::Build(data, keys);
            std::ofstream of(fs::path(argv[4]), std::ios::binary | std::ios::trunc);
            of.write(out.data(), (std::streamsize)out.size());
            std::fprintf(stderr, "built %zu bytes\n", out.size());
        } else if (mode == "show") {
            std::string g = bootstrap::ExportBytes(data, keys, "PsyNetConnection_X.GetPsyNetURL");
            std::string i = bootstrap::ExportBytes(data, keys, "PsyNetMessengerWebSocket_X.Init");
            std::printf("GetPsyNetURL %s\n", Hex(g).c_str());
            std::printf("Init %s\n", Hex(i).c_str());
        } else {
            std::fputs("unknown mode\n", stderr);
            return 2;
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
