#pragma once

#include "Game/ToolEfficiency.h"

namespace mcr::game {

struct ToolEfficiencyConfig final {
    float maximum{100.0F};
    float minimum{1.0F};
    float use_cost{0.75F};
};

class ToolEfficiencySystem final {
public:
    explicit ToolEfficiencySystem(
        ToolEfficiencyConfig config = {}) noexcept;

    void apply_use(
        ToolEfficiency& tool,
        float use_multiplier = 1.0F) const noexcept;

    void maintain(
        ToolEfficiency& tool,
        float amount) const noexcept;

    [[nodiscard]] float performance_multiplier(
        const ToolEfficiency& tool) const noexcept;

    [[nodiscard]] const ToolEfficiencyConfig& config() const noexcept {
        return config_;
    }

private:
    ToolEfficiencyConfig config_;
};

} // namespace mcr::game
