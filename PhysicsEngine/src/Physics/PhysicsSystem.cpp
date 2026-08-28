#include "Physics/PhysicsSystem.h"

#include "Core/ECS/Registry.h"

namespace mcr::physics {

void PhysicsSystem::update(core::Registry& registry, const double fixed_seconds) {
    registry.each<Transform>([&](const core::Entity entity, Transform& transform) {
        if (!registry.has<Velocity>(entity)) return;
        auto& velocity = registry.get<Velocity>(entity).value;
        velocity.y -= gravity_ * static_cast<float>(fixed_seconds);
        transform.position += velocity * static_cast<float>(fixed_seconds);
    });
}

} // namespace mcr::physics

