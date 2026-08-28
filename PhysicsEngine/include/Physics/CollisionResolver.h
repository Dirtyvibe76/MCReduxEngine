#pragma once

#include "Physics/AABB.h"

namespace mcr::physics {

class CollisionResolver final {
public:
    [[nodiscard]] static core::Vec3 separate(const AABB& moving, const AABB& obstacle) noexcept;
};

} // namespace mcr::physics

