#include "Aes.h"

#include <windows.h>
#include <bcrypt.h>

#include <stdexcept>
#include <vector>

namespace {

struct AesAlg {
    BCRYPT_ALG_HANDLE h = nullptr;
    AesAlg() {
        if (BCryptOpenAlgorithmProvider(&h, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("AES: BCryptOpenAlgorithmProvider failed");
        if (BCryptSetProperty(h, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
                              sizeof(BCRYPT_CHAIN_MODE_ECB), 0) < 0)
            throw std::runtime_error("AES: set ECB chaining mode failed");
    }
    ~AesAlg() {
        if (h) BCryptCloseAlgorithmProvider(h, 0);
    }
};

AesAlg& Alg() {
    static AesAlg a;
    return a;
}

std::string Transform(const std::string& key, const std::string& data, bool encrypt) {
    if (key.size() != 32) throw std::runtime_error("AES: key must be 32 bytes");
    if (data.size() % 16 != 0) throw std::runtime_error("AES: data not block-aligned");

    ULONG objLen = 0, got = 0;
    if (BCryptGetProperty(Alg().h, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &got, 0) < 0)
        throw std::runtime_error("AES: object length query failed");
    std::vector<UCHAR> keyObj(objLen);

    BCRYPT_KEY_HANDLE key_h = nullptr;
    if (BCryptGenerateSymmetricKey(Alg().h, &key_h, keyObj.data(), objLen, (PUCHAR)key.data(),
                                   (ULONG)key.size(), 0) < 0)
        throw std::runtime_error("AES: key import failed");

    std::string out(data.size(), '\0');
    ULONG done = 0;
    NTSTATUS st;
    if (encrypt)
        st = BCryptEncrypt(key_h, (PUCHAR)data.data(), (ULONG)data.size(), nullptr, nullptr, 0,
                           (PUCHAR)out.data(), (ULONG)out.size(), &done, 0);
    else
        st = BCryptDecrypt(key_h, (PUCHAR)data.data(), (ULONG)data.size(), nullptr, nullptr, 0,
                           (PUCHAR)out.data(), (ULONG)out.size(), &done, 0);
    BCryptDestroyKey(key_h);
    if (st < 0) throw std::runtime_error("AES: transform failed");
    out.resize(done);
    return out;
}

}  // namespace

namespace aes {
std::string EcbEncrypt(const std::string& key32, const std::string& data) {
    return Transform(key32, data, true);
}
std::string EcbDecrypt(const std::string& key32, const std::string& data) {
    return Transform(key32, data, false);
}
}  // namespace aes
