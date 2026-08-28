#include "Game/Game.h"

#include "Combat/CombatSystem.h"
#include "Game/InventorySystem.h"
#include "Game/PlayerController.h"
#include "Game/StaminaSystem.h"
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

    StaminaSystem stamina_system;
    auto& controller = registry_.get<PlayerController>(player);

    stamina_system.update(controller, 1.0, true);
    const float stamina_after_sprint = controller.stamina;

    const bool jump_consumed =
        stamina_system.consume_jump(controller);
    const float stamina_after_jump = controller.stamina;

    controller.stamina = 25.0F;
    const float low_movement =
        stamina_system.movement_multiplier(controller);

    controller.stamina = 10.0F;
    const bool sprint_blocked =
        !stamina_system.can_sprint(controller);
    const bool ability_blocked =
        !stamina_system.can_use_ability(controller);

    controller.stamina = 50.0F;
    stamina_system.update(controller, 1.0, false);
    const float stamina_after_regen = controller.stamina;

    const auto biome = worldgen::BiomeGenerator::generate(0xC0770ULL, 0, 0);
    const auto& transform = registry_.get<physics::Transform>(player);

    const bool stamina_passed =
        stamina_after_sprint < 100.0F
        && jump_consumed
        && stamina_after_jump < stamina_after_sprint
        && low_movement < 1.0F
        && sprint_blocked
        && ability_blocked
        && stamina_after_regen > 50.0F;

    const bool passed = registry_.alive(player) && chunks_.loaded_count() == 1
        && chunk.block(0, 0, 0) == 1 && loop_.tick_count() == 5
        && inventory.tool(0) == "Basic Pick" && renderer_.ready()
        && transform.position.x > 0.0F
        && stamina_passed;

    std::ostringstream report;
    report << "MC-Redux engine foundation\n"
           << "  ECS entity: " << (registry_.alive(player) ? "alive" : "failed") << '\n'
           << "  Fixed ticks: " << loop_.tick_count() << '\n'
           << "  Loaded chunks: " << chunks_.loaded_count() << '\n'
           << "  Generated biome: " << biome.name << '\n'
           << "  Renderer: " << renderer_.backend_name() << '\n'
           << "  Stamina sprint drain: " << stamina_after_sprint << '\n'
           << "  Stamina jump cost: " << stamina_after_jump << '\n'
           << "  Low-stamina movement: " << low_movement << '\n'
           << "  Exhaustion blocks sprint: " << (sprint_blocked ? "yes" : "no") << '\n'
           << "  Exhaustion blocks abilities: " << (ability_blocked ? "yes" : "no") << '\n'
           << "  Stamina regeneration: " << stamina_after_regen << '\n'
           << "  Result: " << (passed ? "PASS" : "FAIL") << '\n';
    renderer_.shutdown();
    return {passed, report.str()};
}

} // namespace mcr::game

