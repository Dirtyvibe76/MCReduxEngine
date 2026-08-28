#pragma once

#include "World/Structures/StructureSpec.h"

#include <cstdint>

namespace mcr::worldgen {
class StructureGenerator final {
public:
    [[nodiscard]] static world::StructureSpec generate(std::uint64_t seed, std::uint8_t tier);
};
} // namespace mcr::worldgen

