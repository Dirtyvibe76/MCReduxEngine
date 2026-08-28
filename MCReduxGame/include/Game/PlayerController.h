#pragma once

#include "Core/Math/Vec3.h"

namespace mcr::game {
struct PlayerController final {
    float stamina{100.0F};
    core::Vec3 intended_movement{};
};
} // namespace mcr::game

