#pragma once

#include "Physics/AABB.h"

namespace mcr::combat {
struct Hurtbox final { physics::AABB bounds; float multiplier{1.0F}; };
} // namespace mcr::combat

