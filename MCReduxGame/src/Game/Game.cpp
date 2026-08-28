#include "Game/Game.h"

#include "Combat/CombatSystem.h"
#include "Game/InventorySystem.h"
#include "Game/PlayerController.h"
#include "Game/StaminaSystem.h"
#include "Game/ToolEfficiency.h"
#include "Game/ToolEfficiencySystem.h"
#include "Worldgen/BiomeGenerator.h"

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace mcr::game {

bool InventorySystem::equip_tool(
    const std::size_t slot,
    ToolEfficiency tool) {

    if (slot >= toolbelt_.size())
        return false;

    toolbelt_[slot] = std::move(tool);
    return true;
}

bool InventorySystem::equip_tool(
    const std::size_t slot,
    std::string name) {

    return equip_tool(
        slot,
        ToolEfficiency{
            std::move(name),
            100.0F
        });
}

bool InventorySystem::select_slot(
    const std::size_t slot) noexcept {

    if (slot >= toolbelt_.size())
        return false;

    selected_slot_ = slot;
    return true;
}

bool InventorySystem::has_equipped_tool() const noexcept {
    return toolbelt_[selected_slot_].has_value();
}

ToolEfficiency*
InventorySystem::equipped_tool() noexcept {

    return tool(selected_slot_);
}

const ToolEfficiency*
InventorySystem::equipped_tool() const noexcept {

    return tool(selected_slot_);
}

ToolEfficiency*
InventorySystem::tool(
    const std::size_t slot) noexcept {

    if (slot >= toolbelt_.size())
        return nullptr;

    auto& item = toolbelt_[slot];

    return item
        ? &item.value()
        : nullptr;
}

const ToolEfficiency*
InventorySystem::tool(
    const std::size_t slot) const noexcept {

    if (slot >= toolbelt_.size())
        return nullptr;

    const auto& item = toolbelt_[slot];

    return item
        ? &item.value()
        : nullptr;
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

    ToolEfficiency tool;
    ToolEfficiencySystem tool_efficiency_system;

    tool_efficiency_system.apply_use(tool);
    const float efficiency_after_use = tool.efficiency;

    const float performance_after_use =
        tool_efficiency_system.performance_multiplier(tool);

    tool.efficiency = 1.2F;
    tool_efficiency_system.apply_use(tool, 10.0F);
    const float efficiency_floor = tool.efficiency;

    tool_efficiency_system.maintain(tool, 50.0F);
    const float efficiency_after_maintenance = tool.efficiency;

    tool.efficiency = 99.8F;
    tool_efficiency_system.maintain(tool, 50.0F);
    const float efficiency_ceiling = tool.efficiency;

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

    const bool tool_efficiency_passed =
        efficiency_after_use < 100.0F
        && performance_after_use < 1.0F
        && efficiency_floor == 1.0F
        && efficiency_after_maintenance > efficiency_floor
        && efficiency_ceiling == 100.0F;

    const bool passed = registry_.alive(player) && chunks_.loaded_count() == 1
        && chunk.block(0, 0, 0) == 1 && loop_.tick_count() == 5
        && inventory.tool(0) != nullptr
        && inventory.tool(0)->name == "Basic Pick"
        && renderer_.ready()
        && transform.position.x > 0.0F
        && stamina_passed
        && tool_efficiency_passed;

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
           << "  Tool efficiency after use: " << efficiency_after_use << '\n'
           << "  Tool performance: " << performance_after_use << '\n'
           << "  Tool efficiency floor: " << efficiency_floor << '\n'
           << "  Tool efficiency after maintenance: " << efficiency_after_maintenance << '\n'
           << "  Tool efficiency ceiling: " << efficiency_ceiling << '\n'
           << "  Result: " << (passed ? "PASS" : "FAIL") << '\n';
    renderer_.shutdown();
    return {passed, report.str()};
}

} // namespace mcr::game

