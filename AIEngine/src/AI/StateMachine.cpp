#include "AI/StateMachine.h"

namespace mcr::ai {

void StateMachine::change(std::unique_ptr<AIState> next) {
    if (current_) current_->exit(context_);
    current_ = std::move(next);
    if (current_) current_->enter(context_);
}

void StateMachine::update(const double fixed_seconds) {
    if (current_) current_->update(context_, fixed_seconds);
}

} // namespace mcr::ai

