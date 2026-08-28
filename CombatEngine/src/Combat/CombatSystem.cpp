#include "Combat/CombatSystem.h"

#include <algorithm>

namespace mcr::combat {

float CombatSystem::resolve(const float base_damage, const float tier_multiplier,
                            const float critical_multiplier, const float armor_reduction) noexcept {
    return std::max(0.0F, base_damage * tier_multiplier * critical_multiplier - armor_reduction);
}

} // namespace mcr::combat
