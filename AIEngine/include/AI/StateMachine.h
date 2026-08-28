#pragma once

#include "AI/AIState.h"

#include <memory>

namespace mcr::ai {

class StateMachine final {
public:
    explicit StateMachine(AIContext context = {}) noexcept : context_(context) {}
    void change(std::unique_ptr<AIState> next);
    void update(double fixed_seconds);
    [[nodiscard]] const AIState* current() const noexcept { return current_.get(); }
    [[nodiscard]] AIContext& context() noexcept { return context_; }

private:
    AIContext context_;
    std::unique_ptr<AIState> current_;
};

} // namespace mcr::ai

