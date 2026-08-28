#pragma once

#include <cstdint>
#include <string>

namespace mcr::worldgen {
struct MobSpec final { std::string name; std::uint8_t tier{1}; bool apex{false}; };
class MobGenerator final {
public:
    [[nodiscard]] static MobSpec generate(std::uint64_t seed, std::uint8_t biome_tier, bool apex);
};
} // namespace mcr::worldgen

