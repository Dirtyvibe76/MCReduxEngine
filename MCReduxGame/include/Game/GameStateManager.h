#pragma once

#include <cstdint>

namespace mcr::game {
enum class GameState : std::uint8_t { booting, title, playing, paused, shutting_down };
class GameStateManager final {
public:
    void set(GameState state) noexcept { state_ = state; }
    [[nodiscard]] GameState current() const noexcept { return state_; }
private:
    GameState state_{GameState::booting};
};
} // namespace mcr::game

