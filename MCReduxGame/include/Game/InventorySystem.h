#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace mcr::game {
class InventorySystem final {
public:
    static constexpr std::size_t toolbelt_slots = 4;
    bool equip_tool(std::size_t slot, std::string tool);
    [[nodiscard]] const std::string& tool(std::size_t slot) const;
private:
    std::array<std::string, toolbelt_slots> toolbelt_{};
};
} // namespace mcr::game

