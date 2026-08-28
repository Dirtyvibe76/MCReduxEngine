#pragma once

#include "Combat/DamagePacket.h"

namespace mcr::combat {

struct Health final { float current{100.0F}; float maximum{100.0F}; };

class CombatSystem final {
public:
    [[nodiscard]] static float resolve(float base_damage, float tier_multiplier,
                                       float critical_multiplier, float armor_reduction) noexcept;
};

} // namespace mcr::combat

