#pragma once
// Native port of ripple/upk.py: UE3 (Rocket League) script-package
// header parsing, AES-256-ECB name-region decrypt/encrypt, and the item-swap
// "convert" that renames a package's object so one cosmetic masquerades as
// another. Output is intended to be byte-for-byte identical to the Python.
#include <filesystem>
#include <string>
#include <vector>

namespace upk {

// UE3 script-package header fields (see upk.py Header).
struct Header {
    long long hs = 0, nc = 0, no = 0, ec = 0, eo = 0, ic = 0, io = 0, dep = 0, u1 = 0;
    long long garbage = 0, ccio = 0, lbs = 0, content = 0, clen = 0;
};
Header ParseHeader(const std::string& b);

struct Decrypted {
    Header h;
    std::string decoded;  // AES-decrypted name/export/import region (length h.clen)
    std::string key;      // the key that worked (32 bytes)
};
// Decrypt the header region, trying each key until the name table looks sane.
Decrypted TryDecrypt(const std::string& data, const std::vector<std::string>& keys);

// The hardcoded default AES key (32 raw bytes).
const std::string& DefaultKey();

// Default key + any 32-byte base64 keys from <dataDir>/keys.txt (cached).
const std::vector<std::string>& LoadKeys(const std::filesystem::path& dataDir);

// "Antenna_8Ball_SF.upk" -> "Antenna_8Ball"; "Foo.upk" -> "Foo".
std::string PkgBasename(const std::string& filename);

// Rename the object in `srcBytes` (whose own package name is `srcBasename`) to
// `targetName`, returning the rebuilt package bytes. Throws std::runtime_error.
std::string Convert(const std::string& srcBytes, const std::string& srcBasename,
                    const std::string& targetName, const std::vector<std::string>& keys);

// Back up `gameFile` into `backupDir` (once), then overwrite it with the
// converted `sourceFile` renamed to `targetName`.
void SwapFile(const std::filesystem::path& gameFile, const std::filesystem::path& sourceFile,
              const std::string& targetName, const std::filesystem::path& dataDir,
              const std::filesystem::path& backupDir);

// Restore `gameFile` from its backup (backupDir\<name> or <gameFile>.ripplebak).
void RestoreFile(const std::filesystem::path& gameFile, const std::filesystem::path& backupDir);

}  // namespace upk
