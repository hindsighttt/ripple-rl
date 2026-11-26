#pragma once
// AES-256 in ECB mode via Windows CNG (BCrypt) — the exact primitive
// ripple/upk.py used through pycryptodome (AES.new(key, MODE_ECB)).
// Both buffers are raw bytes; data length must be a multiple of 16 (ECB, no
// padding). Throws std::runtime_error on misuse or CNG failure.
#include <string>

namespace aes {
std::string EcbEncrypt(const std::string& key32, const std::string& data);
std::string EcbDecrypt(const std::string& key32, const std::string& data);
}  // namespace aes
