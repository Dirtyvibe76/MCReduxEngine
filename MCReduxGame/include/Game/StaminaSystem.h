#pragma once

#include "Game/PlayerController.h"

namespace mcr::game {

struct StaminaConfig final {
    float maximum{100.0F};
    float sprint_drain_per_second{14.0F};
    float regeneration_per_second{10.0F};
    float jump_cost{12.0F};

    float low_threshold{30.0F};
    float exhausted_threshold{10.0F};

    float low_movement_multiplier{0.85F};
};

class StaminaSystem final {
public:
    explicit StaminaSystem(StaminaConfig config = {}) noexcept;

    void update(
        PlayerController& player,
        double delta_seconds,
        bool sprint_requested) const noexcept;

    [[nodiscard]] bool consume_jump(PlayerController& player) const noexcept;

    [[nodiscard]] bool can_sprint(
        const PlayerController& player) const noexcept;

    [[nodiscard]] bool can_use_ability(
        const PlayerController& player) const noexcept;

    [[nodiscard]] float movement_multiplier(
        const PlayerController& player) const noexcept;

    [[nodiscard]] const StaminaConfig& config() const noexcept {
        return config_;
    }

private:
    StaminaConfig config_;
};

} // namespace mcr::game
