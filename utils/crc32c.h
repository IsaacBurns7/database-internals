#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

#define __arch64__ 


#if defined(__x86_64__) && defined(__SSE4_2__)
    #include <nmmintrin.h>
    #define CRC32C_HW_X86 1
#elif defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
    #include <arm_acle.h>
    #define CRC32C_HW_ARM 1
#endif

namespace crc32c {

inline constexpr uint32_t kInit = 0xFFFFFFFFu;
inline constexpr uint32_t kXorOut = 0xFFFFFFFFu;

// Extend a raw (non-finalized) CRC32C state over [p, p+n).
// Chain calls across header/before/after; call finalize() exactly once.
inline uint32_t extend(uint32_t state, const std::byte* p, size_t n) {
#if defined(CRC32C_HW_X86)
    uint64_t s = state;
    while (n >= 8) {
        uint64_t v; std::memcpy(&v, p, 8);
        s = _mm_crc32_u64(s, v);
        p += 8; n -= 8;
    }
    uint32_t s32 = static_cast<uint32_t>(s);
    while (n--) s32 = _mm_crc32_u8(s32, std::to_integer<uint8_t>(*p++));
    return s32;

#elif defined(CRC32C_HW_ARM)
    while (n >= 8) {
        uint64_t v; std::memcpy(&v, p, 8);
        state = __crc32cd(state, v);
        p += 8; n -= 8;
    }
    while (n--) state = __crc32cb(state, std::to_integer<uint8_t>(*p++));
    return state;

#else
    // Portable fallback: bitwise, reflected Castagnoli poly 0x82F63B78.
    // Correct but slow (~8 iterations per byte). If this path matters, replace it with
    // table-driven slicing-by-8 (8 x 256-entry tables, roughly 1-2 GB/s) or use
    // absl::ExtendCrc32c. Output must match the hardware paths bit for bit,
    // since logs written on one machine may be recovered on another.
    while (n--) {
        state ^= std::to_integer<uint8_t>(*p++);
        for (int k = 0; k < 8; ++k)
            state = (state >> 1) ^ (0x82F63B78u & (0u - (state & 1u)));
    }
    return state;
#endif
}

inline uint32_t finalize(uint32_t state) { return state ^ kXorOut; }

inline uint32_t compute(const std::byte* p, size_t n) {
    return finalize(extend(kInit, p, n));
}

} // namespace crc32c

// Test: compute("123456789") == 0xE3069283
