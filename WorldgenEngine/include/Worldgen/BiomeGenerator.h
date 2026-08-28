#pragma once

#include "World/Biomes/BiomeSpec.h"

#include <cstdint>

namespace mcr::worldgen {
class BiomeGenerator final {
public:
    [[nodiscard]] static world::BiomeSpec plains();
    [[nodiscard]] static world::BiomeSpec generate(std::uint64_t seed, int region_x, int region_z);
};
} // namespace mcr::worldgen

