#pragma once

#include <chrono>
#include <cstddef>
#include <functional>

namespace mcr::core {

class GameLoop final {
public:
    using Tick = std::function<void(double)>;

    explicit GameLoop(double ticks_per_second = 20.0);
    void advance(std::chrono::duration<double> elapsed, const Tick& tick);
    void reset() noexcept;

    [[nodiscard]] double fixed_seconds() const noexcept { return fixed_seconds_; }
    [[nodiscard]] std::size_t tick_count() const noexcept { return tick_count_; }
    [[nodiscard]] double interpolation_alpha() const noexcept;

private:
    double fixed_seconds_;
    double accumulator_{0.0};
    std::size_t tick_count_{0};
};

} // namespace mcr::core

