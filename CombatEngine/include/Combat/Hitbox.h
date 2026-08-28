#pragma once

#include "Physics/AABB.h"

namespace mcr::combat {
struct Hitbox final { physics::AABB bounds; float damage{0.0F}; };
} // namespace mcr::combat

