#pragma once

#include "Core/Math/Vec3.h"

namespace mcr::physics {

struct AABB final {
    core::Vec3 minimum;
    core::Vec3 maximum;

    [[nodiscard]] constexpr bool intersects(const AABB& rhs) const noexcept {
        return minimum.x <= rhs.maximum.x && maximum.x >= rhs.minimum.x
            && minimum.y <= rhs.maximum.y && maximum.y >= rhs.minimum.y
            && minimum.z <= rhs.maximum.z && maximum.z >= rhs.minimum.z;
    }
};

} // namespace mcr::physics

