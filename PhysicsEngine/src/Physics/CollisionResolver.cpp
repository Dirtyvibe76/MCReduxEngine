#include "Physics/CollisionResolver.h"

#include <array>
#include <cmath>

namespace mcr::physics {

core::Vec3 CollisionResolver::separate(const AABB& moving, const AABB& obstacle) noexcept {
    if (!moving.intersects(obstacle)) return {};
    const std::array<core::Vec3, 6> candidates{{
        {obstacle.minimum.x - moving.maximum.x, 0, 0}, {obstacle.maximum.x - moving.minimum.x, 0, 0},
        {0, obstacle.minimum.y - moving.maximum.y, 0}, {0, obstacle.maximum.y - moving.minimum.y, 0},
        {0, 0, obstacle.minimum.z - moving.maximum.z}, {0, 0, obstacle.maximum.z - moving.minimum.z}}};
    auto best = candidates.front();
    for (const auto& candidate : candidates) {
        if (candidate.length_squared() < best.length_squared()) best = candidate;
    }
    return best;
}

} // namespace mcr::physics

