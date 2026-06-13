#include "src/utils/Hash.h"
#include <cstring>

// -- xxHash64 constants --------------------------------------------------------
static constexpr uint64_t P1 = 0x9E3779B185EBCA87ULL;
static constexpr uint64_t P2 = 0xC2B2AE3D27D4EB4FULL;
static constexpr uint64_t P3 = 0x165667B19E3779F9ULL;
static constexpr uint64_t P4 = 0x85EBCA77C2B2AE63ULL;
static constexpr uint64_t P5 = 0x27D4EB2F165667C5ULL;

static inline uint64_t rotl64(uint64_t v, int r) noexcept
{
    return (v << r) | (v >> (64 - r));
}

// Safe unaligned 64-bit read via memcpy (avoids UB on strict-alignment platforms)
static inline uint64_t read64(const uint8_t* p) noexcept
{
    uint64_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static inline uint32_t read32(const uint8_t* p) noexcept
{
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static inline uint64_t xxRound(uint64_t acc, uint64_t input) noexcept
{
    acc += input * P2;
    acc  = rotl64(acc, 31);
    acc *= P1;
    return acc;
}

static inline uint64_t xxMergeAcc(uint64_t h, uint64_t acc) noexcept
{
    h ^= xxRound(0, acc);
    h  = h * P1 + P4;
    return h;
}

uint64_t XXHash64(const void* data, size_t len, uint64_t seed) noexcept
{
    const uint8_t* p   = static_cast<const uint8_t*>(data);
    const uint8_t* end = p + len;
    uint64_t h;

    if (len >= 32)
    {
        uint64_t v1 = seed + P1 + P2;
        uint64_t v2 = seed + P2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - P1;

        do {
            v1 = xxRound(v1, read64(p)); p += 8;
            v2 = xxRound(v2, read64(p)); p += 8;
            v3 = xxRound(v3, read64(p)); p += 8;
            v4 = xxRound(v4, read64(p)); p += 8;
        } while (p <= end - 32);

        h = rotl64(v1,  1) + rotl64(v2,  7)
          + rotl64(v3, 12) + rotl64(v4, 18);
        h = xxMergeAcc(h, v1);
        h = xxMergeAcc(h, v2);
        h = xxMergeAcc(h, v3);
        h = xxMergeAcc(h, v4);
    }
    else
    {
        h = seed + P5;
    }

    h += static_cast<uint64_t>(len);

    // Consume remaining 8-byte chunks
    while (p <= end - 8)
    {
        uint64_t k1 = xxRound(0, read64(p)); p += 8;
        h ^= k1;
        h  = rotl64(h, 27) * P1 + P4;
    }

    // Remaining 4-byte chunk
    if (p <= end - 4)
    {
        h ^= static_cast<uint64_t>(read32(p)) * P1; p += 4;
        h  = rotl64(h, 23) * P2 + P3;
    }

    // Remaining bytes
    while (p < end)
    {
        h ^= static_cast<uint64_t>(*p++) * P5;
        h  = rotl64(h, 11) * P1;
    }

    // Finalisation mix
    h ^= h >> 33; h *= P2;
    h ^= h >> 29; h *= P3;
    h ^= h >> 32;

    return h;
}
