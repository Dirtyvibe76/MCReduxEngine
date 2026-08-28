#pragma once

#include "Game/ToolEfficiency.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace mcr::game {

class InventorySystem final {
public:
    static constexpr std::size_t toolbelt_slots = 4;

    bool equip_tool(
        std::size_t slot,
        ToolEfficiency tool);

    bool equip_tool(
        std::size_t slot,
        std::string name);

    bool select_slot(
        std::size_t slot) noexcept;

    [[nodiscard]] std::size_t selected_slot() const noexcept {
        return selected_slot_;
    }

    [[nodiscard]] bool has_equipped_tool() const noexcept;

    [[nodiscard]] ToolEfficiency* equipped_tool() noexcept;

    [[nodiscard]] const ToolEfficiency*
    equipped_tool() const noexcept;

    [[nodiscard]] ToolEfficiency* tool(
        std::size_t slot) noexcept;

    [[nodiscard]] const ToolEfficiency* tool(
        std::size_t slot) const noexcept;

private:
    std::array<
        std::optional<ToolEfficiency>,
        toolbelt_slots> toolbelt_{};

    std::size_t selected_slot_{0};
};

} // namespace mcr::game
