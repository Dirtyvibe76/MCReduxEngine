#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace mcr::render {

struct PlayerControlHooks final {
    std::function<void(double, bool)> update_stamina;
    std::function<bool()> consume_jump;
    std::function<bool()> can_sprint;
    std::function<float()> movement_multiplier;
    std::function<float()> stamina;

    std::function<bool()> has_equipped_tool;
    std::function<float()> tool_performance;
    std::function<void()> tool_used;
    std::function<float()> tool_efficiency;
    std::function<std::string()> equipped_tool_name;

    std::function<void(std::size_t)> select_tool_slot;
    std::function<std::size_t()> selected_tool_slot;
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
