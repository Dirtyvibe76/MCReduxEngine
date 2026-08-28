#pragma once

#include <cstdint>
#include <functional>
#include <limits>

namespace mcr::core {

struct Entity final {
    using Value = std::uint32_t;
    static constexpr Value invalid_value = std::numeric_limits<Value>::max();

    Value index{invalid_value};
    Value generation{0};

    [[nodiscard]] constexpr bool valid() const noexcept { return index != invalid_value; }
    friend constexpr bool operator==(Entity, Entity) noexcept = default;
};

} // namespace mcr::core

namespace std {
template <>
struct hash<mcr::core::Entity> {
    size_t operator()(const mcr::core::Entity entity) const noexcept {
        return (static_cast<size_t>(entity.generation) << 32U) ^ entity.index;
    }
};
} // namespace std

