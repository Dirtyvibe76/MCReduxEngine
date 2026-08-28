#include "Game/StaminaSystem.h"

#include <algorithm>

namespace mcr::game {

StaminaSystem::StaminaSystem(StaminaConfig config) noexcept
    : config_(config) {}

void StaminaSystem::update(
    PlayerController& player,
    const double delta_seconds,
    const bool sprint_requested) const noexcept {

    const float dt =
        static_cast<float>(std::max(0.0, delta_seconds));

    if (sprint_requested) {
        if (can_sprint(player)) {
            player.stamina -=
                config_.sprint_drain_per_second * dt;
        }
    } else {
        player.stamina +=
            config_.regeneration_per_second * dt;
    }

    player.stamina =
        std::clamp(player.stamina, 0.0F, config_.maximum);
}

bool StaminaSystem::consume_jump(
    PlayerController& player) const noexcept {

    if (player.stamina < config_.jump_cost) {
        return false;
    }

    player.stamina -= config_.jump_cost;
    player.stamina =
        std::max(0.0F, player.stamina);

    return true;
}

bool StaminaSystem::can_sprint(
    const PlayerController& player) const noexcept {

    return player.stamina >
        config_.exhausted_threshold;
}

bool StaminaSystem::can_use_ability(
    const PlayerController& player) const noexcept {

    return player.stamina >
        config_.exhausted_threshold;
}

float StaminaSystem::movement_multiplier(
    const PlayerController& player) const noexcept {

    if (player.stamina < config_.low_threshold) {
        return config_.low_movement_multiplier;
    }

    return 1.0F;
}

} // namespace mcr::game
