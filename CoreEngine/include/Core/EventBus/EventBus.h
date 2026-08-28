#pragma once

#include <cstddef>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mcr::core {

class EventBus final {
public:
    using Subscription = std::size_t;

    template <typename Event, typename Handler>
    Subscription subscribe(Handler&& handler) {
        const Subscription id = next_id_++;
        handlers_[std::type_index{typeid(Event)}].push_back({
            id,
            [fn = std::forward<Handler>(handler)](const void* event) {
                fn(*static_cast<const Event*>(event));
            }});
        return id;
    }

    template <typename Event>
    void publish(const Event& event) const {
        const auto it = handlers_.find(std::type_index{typeid(Event)});
        if (it == handlers_.end()) return;
        const auto snapshot = it->second;
        for (const auto& handler : snapshot) handler.callback(&event);
    }

    void unsubscribe(Subscription id);
    void clear() noexcept { handlers_.clear(); }

private:
    struct Handler {
        Subscription id;
        std::function<void(const void*)> callback;
    };

    Subscription next_id_{1};
    std::unordered_map<std::type_index, std::vector<Handler>> handlers_;
};

} // namespace mcr::core

