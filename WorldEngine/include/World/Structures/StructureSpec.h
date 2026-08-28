#pragma once

#include <cstdint>
#include <string>

namespace mcr::world {

enum class StructureRarity : std::uint8_t { common, rare, legendary, worldforged };

struct StructureSpec final {
    std::string name;
    std::uint8_t tier{1};
    StructureRarity rarity{StructureRarity::common};
    bool has_boss{false};
};

} // namespace mcr::world

