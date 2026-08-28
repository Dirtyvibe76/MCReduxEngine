#pragma once

#include <concepts>

namespace mcr::core {

template <typename T>
concept Component = std::movable<T> && std::destructible<T>;

} // namespace mcr::core

