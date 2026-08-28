#include "Game/Game.h"
#include "Render/D3D11Renderer.h"

#include <exception>
#include <iostream>

int main() {
    try {
        mcr::game::Game game;
        const auto result = game.run_smoke_test();
        std::cout << result.report;
        if (!result.passed) return 1;

#ifdef _WIN32
        std::cout << "Opening the first generated MC-Redux voxel chunk...\n"
                  << "Controls: W/A/S/D move, Q/E descend/ascend, hold right mouse to look.\n"
                  << "Hold Shift to move faster. Press Esc or close the window to exit.\n";
        mcr::render::D3D11Renderer renderer;
        if (!renderer.run_visual_demo()) {
            std::cerr << "DirectX 11 visual demo failed to initialize.\n";
            return 3;
        }
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MC-Redux startup failure: " << error.what() << '\n';
        return 2;
    }
}
