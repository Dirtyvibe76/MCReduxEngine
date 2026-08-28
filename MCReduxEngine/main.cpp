#include "Game/Game.h"
#include "Game/PlayerController.h"
#include "Game/StaminaSystem.h"
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
