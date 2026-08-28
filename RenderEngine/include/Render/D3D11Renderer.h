#pragma once

#include <string_view>

namespace mcr::render {

class D3D11Renderer final {
public:
    bool initialize() noexcept;
    bool run_visual_demo() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] std::string_view backend_name() const noexcept;

private:
    bool ready_{false};
};

} // namespace mcr::render

