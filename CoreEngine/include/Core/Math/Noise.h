#pragma once

#include <cstdint>

namespace mcr::core {

class Noise final {
public:
    explicit Noise(std::uint64_t seed = 0) noexcept : seed_(seed) {}
    [[nodiscard]] float value(int x, int y, int z = 0) const noexcept;

private:
    std::uint64_t seed_;
};

} // namespace mcr::core

