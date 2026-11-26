#pragma once
// SHA-256 via Windows CNG (BCrypt), for the ProjectX function hash guards.
#include <string>

namespace hashing {
std::string Sha256Hex(const std::string& data);  // lowercase hex digest
}  // namespace hashing
