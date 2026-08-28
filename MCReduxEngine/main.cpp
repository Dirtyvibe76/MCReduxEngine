#include "Game/Game.h"
#include "Game/InventorySystem.h"
#include "Game/PlayerController.h"
#include "Game/StaminaSystem.h"
#include "Game/ToolEfficiency.h"
#include "Game/ToolEfficiencySystem.h"
#include "Render/D3D11Renderer.h"

#include <exception>
#include <iostream>

int main() {
    try {
        mcr::game::Game game;
        const auto result = game.run_smoke_test();

        std::cout << result.report;

        if (!result.passed)
            return 1;

#ifdef _WIN32
        std::cout
            << "Opening the streamed MC-Redux PBR voxel world...\n"
            << "Controls: W/A/S/D move, Q/E descend/ascend, hold right mouse to look.\n"
            << "Left click removes the block under the pointer.\n"
            << "Middle click places a block under the pointer.\n"
            << "Press F for walk/fly mode. Space jumps while walking.\n"
            << "Hold Shift to sprint while stamina allows it.\n"
            << "Sprint and jumping consume stamina in WALK mode.\n"
            << "Press Esc or close the window to exit.\n";

        mcr::game::PlayerController live_player;
        mcr::game::StaminaSystem stamina_system;

        mcr::game::InventorySystem inventory;
        mcr::game::ToolEfficiencySystem tool_efficiency_system;

        inventory.equip_tool(
            0,
            mcr::game::ToolEfficiency{
                "Basic Pick",
                100.0F
            });

        inventory.select_slot(0);

        mcr::game::ToolEfficiency live_tool;

        mcr::render::PlayerControlHooks controls;

        controls.update_stamina =
            [&](const double delta_seconds,
                const bool sprint_requested) {
                stamina_system.update(
                    live_player,
                    delta_seconds,
                    sprint_requested);
            };

        controls.consume_jump =
            [&]() {
                return stamina_system.consume_jump(
                    live_player);
            };

        controls.can_sprint =
            [&]() {
                return stamina_system.can_sprint(
                    live_player);
            };

        controls.movement_multiplier =
            [&]() {
                return stamina_system.movement_multiplier(
                    live_player);
            };

        controls.stamina =
            [&]() {
                return live_player.stamina;
            };

        controls.has_equipped_tool =
            [&]() {
                return inventory.has_equipped_tool();
            };

        controls.tool_performance =
            [&]() {
                const auto* tool =
                    inventory.equipped_tool();

                if (!tool)
                    return 0.0F;

                return tool_efficiency_system
                    .performance_multiplier(*tool);
            };

        controls.tool_used =
            [&]() {
                auto* tool =
                    inventory.equipped_tool();

                if (tool)
                    tool_efficiency_system.apply_use(*tool);
            };

        controls.tool_efficiency =
            [&]() {
                const auto* tool =
                    inventory.equipped_tool();

                return tool
                    ? tool->efficiency
                    : 0.0F;
            };

        controls.equipped_tool_name =
            [&]() {
                const auto* tool =
                    inventory.equipped_tool();

                return tool
                    ? tool->name
                    : std::string{};
            };

        controls.select_tool_slot =
            [&](const std::size_t slot) {
                inventory.select_slot(slot);
            };

        controls.selected_tool_slot =
            [&]() {
                return inventory.selected_slot();
            };

        controls.tool_performance =
            [&]() {
                return tool_efficiency_system.performance_multiplier(
                    live_tool);
            };

        controls.tool_used =
            [&]() {
                tool_efficiency_system.apply_use(
                    live_tool);
            };

        controls.tool_efficiency =
            [&]() {
                return live_tool.efficiency;
            };

        mcr::render::D3D11Renderer renderer;

        if (!renderer.run_visual_demo(controls)) {
            std::cerr
                << "DirectX 11 visual demo failed to initialize.\n";
            return 3;
        }
#endif

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "MC-Redux startup failure: "
            << error.what()
            << '\n';
        return 2;
    }
}
