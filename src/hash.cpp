// caseclock · hash.cpp — see hash.h.
#include "hash.h"

#include "app_util.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <cstring>
#include <vector>

namespace caseclock {

// ── BLAKE2b (RFC 7693), unkeyed, 32-byte digest ─────────────────────────────
namespace {
constexpr uint64_t kIV[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull,
};
constexpr uint8_t kSigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
};
inline uint64_t rotr64(uint64_t x, unsigned n) { return (x >> n) | (x << (64 - n)); }
inline uint64_t load64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}
inline void store64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
inline void G(uint64_t* v, int a, int b, int c, int d, uint64_t x, uint64_t y) {
    v[a] = v[a] + v[b] + x;
    v[d] = rotr64(v[d] ^ v[a], 32);
    v[c] = v[c] + v[d];
    v[b] = rotr64(v[b] ^ v[c], 24);
    v[a] = v[a] + v[b] + y;
    v[d] = rotr64(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = rotr64(v[b] ^ v[c], 63);
}
void compress(Blake2b256& s, const uint8_t block[128], bool last) {
    uint64_t v[16], m[16];
    for (int i = 0; i < 8; ++i) { v[i] = s.h[i]; v[i + 8] = kIV[i]; }
    v[12] ^= s.t[0];
    v[13] ^= s.t[1];
    if (last) v[14] = ~v[14];
    for (int i = 0; i < 16; ++i) m[i] = load64(block + 8 * i);
    for (int r = 0; r < 12; ++r) {
        const uint8_t* sg = kSigma[r];
        G(v, 0, 4, 8, 12, m[sg[0]], m[sg[1]]);
        G(v, 1, 5, 9, 13, m[sg[2]], m[sg[3]]);
        G(v, 2, 6, 10, 14, m[sg[4]], m[sg[5]]);
        G(v, 3, 7, 11, 15, m[sg[6]], m[sg[7]]);
        G(v, 0, 5, 10, 15, m[sg[8]], m[sg[9]]);
        G(v, 1, 6, 11, 12, m[sg[10]], m[sg[11]]);
        G(v, 2, 7, 8, 13, m[sg[12]], m[sg[13]]);
        G(v, 3, 4, 9, 14, m[sg[14]], m[sg[15]]);
    }
    for (int i = 0; i < 8; ++i) s.h[i] ^= v[i] ^ v[i + 8];
}
}  // namespace

Blake2b256::Blake2b256() : buflen(0) {
    for (int i = 0; i < 8; ++i) h[i] = kIV[i];
    h[0] ^= 0x01010000ull ^ (0ull << 8) ^ 32ull;   // parameter block: digest 32, key 0, fanout 1, depth 1
    t[0] = t[1] = 0;
    memset(buf, 0, sizeof buf);
}

void Blake2b256::update(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    while (len > 0) {
        if (buflen == 128) {   // a full block, and more to come: it is not the last
            t[0] += 128;
            if (t[0] < 128) t[1]++;
            compress(*this, buf, false);
            buflen = 0;
        }
        const size_t take = std::min(len, (size_t)128 - buflen);
        memcpy(buf + buflen, p, take);
        buflen += take;
        p += take;
        len -= take;
    }
}

void Blake2b256::final(uint8_t out[32]) {
    t[0] += (uint64_t)buflen;
    if (t[0] < (uint64_t)buflen) t[1]++;
    memset(buf + buflen, 0, 128 - buflen);
    compress(*this, buf, true);
    for (int i = 0; i < 4; ++i) store64(out + 8 * i, h[i]);
}

std::string blake2b256_hex(const void* data, size_t len) {
    Blake2b256 s;
    s.update(data, len);
    uint8_t out[32];
    s.final(out);
    return to_hex(out, 32);
}
std::string blake2b256_hex(const std::string& data) { return blake2b256_hex(data.data(), data.size()); }

// ── SHA-256 through bcrypt ──────────────────────────────────────────────────
static bool sha256_raw(const void* data, size_t len, uint8_t out[32]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
    BCRYPT_HASH_HANDLE hh = nullptr;
    bool ok = BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0) == 0;
    if (ok) ok = BCryptHashData(hh, (PUCHAR)data, (ULONG)len, 0) == 0;
    if (ok) ok = BCryptFinishHash(hh, out, 32, 0) == 0;
    if (hh) BCryptDestroyHash(hh);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

std::string sha256_hex(const std::string& data) {
    uint8_t out[32];
    if (!sha256_raw(data.data(), data.size(), out)) return "";
    return to_hex(out, 32);
}

bool sha256_file_hex(const std::string& utf8_path, std::string& out) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(), -1, w.data(), n);
    HANDLE f = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::string data;
    char buf[65536];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof buf, &got, nullptr) && got) data.append(buf, got);
    CloseHandle(f);
    out = sha256_hex(data);
    return !out.empty();
}

}  // namespace caseclock
