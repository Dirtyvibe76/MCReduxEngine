#pragma once

#include <cmath>

namespace mcr::core {

struct Vec3 final {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};

    constexpr Vec3 operator+(const Vec3& rhs) const noexcept { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    constexpr Vec3 operator-(const Vec3& rhs) const noexcept { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    constexpr Vec3 operator*(const float scalar) const noexcept { return {x * scalar, y * scalar, z * scalar}; }
    Vec3& operator+=(const Vec3& rhs) noexcept { x += rhs.x; y += rhs.y; z += rhs.z; return *this; }
    [[nodiscard]] float length_squared() const noexcept { return x * x + y * y + z * z; }
    [[nodiscard]] float length() const noexcept { return std::sqrt(length_squared()); }
};

} // namespace mcr::core

