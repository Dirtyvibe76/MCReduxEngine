#include "Core/EventBus/EventBus.h"

#include <algorithm>

namespace mcr::core {

void EventBus::unsubscribe(const Subscription id) {
    for (auto& [_, handlers] : handlers_) {
        std::erase_if(handlers, [id](const Handler& handler) { return handler.id == id; });
    }
}

} // namespace mcr::core

