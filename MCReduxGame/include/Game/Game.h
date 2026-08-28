#pragma once

#include "Core/ECS/Registry.h"
#include "Core/EventBus/EventBus.h"
#include "Core/Time/GameLoop.h"
#include "Game/GameStateManager.h"
#include "Physics/PhysicsSystem.h"
#include "Render/D3D11Renderer.h"
#include "World/Chunk/ChunkManager.h"

#include <string>

namespace mcr::game {

struct SmokeTestResult final { bool passed{false}; std::string report; };

class Game final {
public:
    [[nodiscard]] SmokeTestResult run_smoke_test();

private:
    core::Registry registry_;
    core::EventBus events_;
    core::GameLoop loop_{20.0};
    world::ChunkManager chunks_;
    physics::PhysicsSystem physics_;
    render::D3D11Renderer renderer_;
    GameStateManager state_;
};

} // namespace mcr::game
