#pragma once

#include <functional>
#include <string_view>

namespace mcr::render {

struct PlayerControlHooks final {
    std::function<void(double, bool)> update_stamina;
    std::function<bool()> consume_jump;
    std::function<bool()> can_sprint;
    std::function<float()> movement_multiplier;
    std::function<float()> stamina;
};

class D3D11Renderer final {
public:
    bool initialize() noexcept;
    bool run_visual_demo() noexcept;
    bool run_visual_demo(const PlayerControlHooks& controls) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] std::string_view backend_name() const noexcept;

private:
    bool ready_{false};
};

} // namespace mcr::render
