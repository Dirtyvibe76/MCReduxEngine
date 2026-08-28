#pragma once

#include "Core/ECS/Entity.h"

namespace mcr::ai {

struct AIContext final {
    core::Entity self;
    core::Entity target;
    float target_distance{0.0F};
    float health_fraction{1.0F};
    float stamina_fraction{1.0F};
};

} // namespace mcr::ai

