#pragma once

#include <cstdint>
#include <string>

namespace mcr::worldgen {
struct BossSpec final { std::string name; std::uint8_t tier{2}; std::uint8_t phases{0}; };
class BossGenerator final {
public:
    [[nodiscard]] static BossSpec generate(std::uint64_t seed, std::uint8_t tier);
};
} // namespace mcr::worldgen

