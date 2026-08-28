#pragma once

#include "AI/AIContext.h"

#include <string_view>

namespace mcr::ai {

class AIState {
public:
    virtual ~AIState() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    virtual void enter(AIContext&) {}
    virtual void update(AIContext&, double fixed_seconds) = 0;
    virtual void exit(AIContext&) {}
};

} // namespace mcr::ai

