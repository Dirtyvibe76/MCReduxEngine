#pragma once

#include "Core/ECS/System.h"
#include "Core/Math/Vec3.h"

namespace mcr::physics {

struct Transform final { core::Vec3 position; };
struct Velocity final { core::Vec3 value; };

class PhysicsSystem final : public core::ISystem {
public:
    explicit PhysicsSystem(float gravity = 9.8F) noexcept : gravity_(gravity) {}
    void update(core::Registry& registry, double fixed_seconds) override;

private:
    float gravity_;
};

} // namespace mcr::physics

