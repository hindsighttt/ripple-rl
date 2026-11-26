#include "Hash.h"

#include <windows.h>
#include <bcrypt.h>

#include <stdexcept>
#include <vector>

namespace hashing {

std::string Sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("sha256: provider open failed");
    DWORD objLen = 0, hashLen = 0, cb = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0);
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &cb, 0);
    std::vector<UCHAR> obj(objLen);
    BCRYPT_HASH_HANDLE h = nullptr;
    BCryptCreateHash(alg, &h, obj.data(), objLen, nullptr, 0, 0);
    BCryptHashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0);
    std::string digest(hashLen, '\0');
    BCryptFinishHash(h, (PUCHAR)digest.data(), hashLen, 0);
    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (unsigned char c : digest) {
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0xF]);
    }
    return out;
}

}  // namespace hashing
