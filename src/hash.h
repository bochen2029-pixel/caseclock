// caseclock · hash.h — BLAKE2b-256 (the tape chain, as REGISTRAR core/tape.py: hashlib.blake2b with
// digest_size=32) written from RFC 7693, and SHA-256 through the OS (bcrypt) for file, rule-set and
// sign-out hashes. No vendored code: the BLAKE2b below is this repository's own transcription of the
// RFC, checked against hashlib's vectors in tests/expected/hash-vectors.json by --selftest.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace caseclock {

struct Blake2b256 {
    uint64_t h[8];
    uint64_t t[2];
    uint8_t buf[128];
    size_t buflen;
    Blake2b256();
    void update(const void* data, size_t len);
    void final(uint8_t out[32]);
};
std::string blake2b256_hex(const std::string& data);
std::string blake2b256_hex(const void* data, size_t len);

std::string sha256_hex(const std::string& data);   // bcrypt; empty string if the provider fails
bool sha256_file_hex(const std::string& utf8_path, std::string& out);

}  // namespace caseclock
