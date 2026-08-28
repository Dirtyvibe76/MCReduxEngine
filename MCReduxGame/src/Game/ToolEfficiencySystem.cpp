#include "Game/ToolEfficiencySystem.h"

#include <algorithm>

namespace mcr::game {

ToolEfficiencySystem::ToolEfficiencySystem(
    ToolEfficiencyConfig config) noexcept
    : config_(config) {}

void ToolEfficiencySystem::apply_use(
    ToolEfficiency& tool,
    const float use_multiplier) const noexcept {

    const float multiplier =
        std::max(0.0F, use_multiplier);

    tool.efficiency -=
        config_.use_cost * multiplier;

    tool.efficiency = std::clamp(
        tool.efficiency,
        config_.minimum,
        config_.maximum);
}

void ToolEfficiencySystem::maintain(
    ToolEfficiency& tool,
    const float amount) const noexcept {

    tool.efficiency +=
        std::max(0.0F, amount);

    tool.efficiency = std::clamp(
        tool.efficiency,
        config_.minimum,
        config_.maximum);
}

float ToolEfficiencySystem::performance_multiplier(
    const ToolEfficiency& tool) const noexcept {

    const float normalized =
        std::clamp(
            tool.efficiency / config_.maximum,
            0.01F,
            1.0F);

    return 0.35F + normalized * 0.65F;
}

} // namespace mcr::game
