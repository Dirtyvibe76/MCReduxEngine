#include "Core/Math/Noise.h"

namespace mcr::core {

float Noise::value(const int x, const int y, const int z) const noexcept {
    std::uint64_t hash = seed_;
    hash ^= static_cast<std::uint64_t>(x) * 0x9E3779B185EBCA87ULL;
    hash ^= static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL;
    hash ^= static_cast<std::uint64_t>(z) * 0x165667B19E3779F9ULL;
    hash ^= hash >> 30U;
    hash *= 0xBF58476D1CE4E5B9ULL;
    hash ^= hash >> 27U;
    hash *= 0x94D049BB133111EBULL;
    hash ^= hash >> 31U;
    return static_cast<float>((hash >> 40U) & 0xFFFFFFU) / 8388607.5F - 1.0F;
}

} // namespace mcr::core
