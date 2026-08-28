#pragma once

namespace mcr::core {

class Registry;

class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void update(Registry& registry, double fixed_seconds) = 0;
};

} // namespace mcr::core

