#pragma once
#include <cstdint>
#include <cstddef>

/// Compute a 64-bit xxHash of the given data block.
/// Fast, non-cryptographic hash used for dirty-frame detection.
///
/// @param data  Pointer to input bytes
/// @param len   Number of bytes to hash
/// @param seed  Optional seed (default 0)
/// @returns     64-bit hash value
[[nodiscard]] uint64_t XXHash64(const void* data, size_t len, uint64_t seed = 0) noexcept;
