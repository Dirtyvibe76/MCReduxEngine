#include "Core/Time/GameLoop.h"

#include <algorithm>
#include <stdexcept>

namespace mcr::core {

GameLoop::GameLoop(const double ticks_per_second)
    : fixed_seconds_(1.0 / ticks_per_second) {
    if (ticks_per_second <= 0.0) throw std::invalid_argument("tick rate must be positive");
}

void GameLoop::advance(const std::chrono::duration<double> elapsed, const Tick& tick) {
    accumulator_ += std::min(elapsed.count(), 0.25);
    while (accumulator_ >= fixed_seconds_) {
        tick(fixed_seconds_);
        accumulator_ -= fixed_seconds_;
        ++tick_count_;
    }
}

void GameLoop::reset() noexcept {
    accumulator_ = 0.0;
    tick_count_ = 0;
}

double GameLoop::interpolation_alpha() const noexcept {
    return accumulator_ / fixed_seconds_;
}

} // namespace mcr::core

