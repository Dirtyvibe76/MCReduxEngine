#include "Game/Game.h"

#include "Combat/CombatSystem.h"
#include "Game/InventorySystem.h"
#include "Game/PlayerController.h"
#include "Worldgen/BiomeGenerator.h"

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace mcr::game {

bool InventorySystem::equip_tool(const std::size_t slot, std::string tool) {
    if (slot >= toolbelt_.size()) return false;
    toolbelt_[slot] = std::move(tool);
    return true;
}

const std::string& InventorySystem::tool(const std::size_t slot) const {
    if (slot >= toolbelt_.size()) throw std::out_of_range("toolbelt slot is invalid");
    return toolbelt_[slot];
}

SmokeTestResult Game::run_smoke_test() {
    state_.set(GameState::playing);
    if (!renderer_.initialize()) return {false, "renderer boundary failed to initialize"};

    const auto player = registry_.create();
    registry_.emplace<PlayerController>(player);
    registry_.emplace<physics::Transform>(player, core::Vec3{0.0F, 10.0F, 0.0F});
    registry_.emplace<physics::Velocity>(player, core::Vec3{1.0F, 0.0F, 0.0F});
    registry_.emplace<combat::Health>(player);

    auto& chunk = chunks_.get_or_create({0, 0});
    chunk.set_block(0, 0, 0, 1);

    loop_.advance(std::chrono::duration<double>{0.25}, [&](const double step) {
        physics_.update(registry_, step);
    });

    InventorySystem inventory;
    inventory.equip_tool(0, "Basic Pick");
    const auto biome = worldgen::BiomeGenerator::generate(0xC0770ULL, 0, 0);
    const auto& transform = registry_.get<physics::Transform>(player);

    const bool passed = registry_.alive(player) && chunks_.loaded_count() == 1
        && chunk.block(0, 0, 0) == 1 && loop_.tick_count() == 5
        && inventory.tool(0) == "Basic Pick" && renderer_.ready()
        && transform.position.x > 0.0F;

    std::ostringstream report;
    report << "MC-Redux engine foundation\n"
           << "  ECS entity: " << (registry_.alive(player) ? "alive" : "failed") << '\n'
           << "  Fixed ticks: " << loop_.tick_count() << '\n'
           << "  Loaded chunks: " << chunks_.loaded_count() << '\n'
           << "  Generated biome: " << biome.name << '\n'
           << "  Renderer: " << renderer_.backend_name() << '\n'
           << "  Result: " << (passed ? "PASS" : "FAIL") << '\n';
    renderer_.shutdown();
    return {passed, report.str()};
}

} // namespace mcr::game

