#pragma once

#include "Core/ECS/Component.h"
#include "Core/ECS/Entity.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mcr::core {

class Registry final {
public:
    [[nodiscard]] Entity create() {
        if (!free_.empty()) {
            const auto index = free_.back();
            free_.pop_back();
            alive_[index] = true;
            return {index, generations_[index]};
        }
        const auto index = static_cast<Entity::Value>(generations_.size());
        generations_.push_back(0);
        alive_.push_back(true);
        return {index, 0};
    }

    void destroy(const Entity entity) {
        require_alive(entity);
        for (auto& [_, pool] : pools_) pool->erase(entity);
        alive_[entity.index] = false;
        ++generations_[entity.index];
        free_.push_back(entity.index);
    }

    [[nodiscard]] bool alive(const Entity entity) const noexcept {
        return entity.valid() && entity.index < generations_.size() && alive_[entity.index]
            && generations_[entity.index] == entity.generation;
    }

    template <Component T, typename... Args>
    T& emplace(const Entity entity, Args&&... args) {
        require_alive(entity);
        auto& values = pool<T>().values;
        return values.insert_or_assign(entity, T{std::forward<Args>(args)...}).first->second;
    }

    template <Component T>
    [[nodiscard]] bool has(const Entity entity) const {
        const auto* values = find_pool<T>();
        return alive(entity) && values && values->values.contains(entity);
    }

    template <Component T>
    T& get(const Entity entity) {
        require_alive(entity);
        return pool<T>().values.at(entity);
    }

    template <Component T>
    const T& get(const Entity entity) const {
        require_alive(entity);
        const auto* values = find_pool<T>();
        if (!values) throw std::out_of_range("component not found");
        return values->values.at(entity);
    }

    template <Component T>
    bool remove(const Entity entity) {
        require_alive(entity);
        auto* values = find_pool<T>();
        return values && values->values.erase(entity) != 0;
    }

    template <Component T, typename Function>
    void each(Function&& function) {
        for (auto& [entity, component] : pool<T>().values) {
            if (alive(entity)) function(entity, component);
        }
    }

private:
    struct IPool {
        virtual ~IPool() = default;
        virtual void erase(Entity entity) = 0;
    };

    template <Component T>
    struct Pool final : IPool {
        std::unordered_map<Entity, T> values;
        void erase(const Entity entity) override { values.erase(entity); }
    };

    void require_alive(const Entity entity) const {
        if (!alive(entity)) throw std::invalid_argument("entity is not alive");
    }

    template <Component T>
    Pool<T>& pool() {
        const std::type_index key{typeid(T)};
        auto [it, inserted] = pools_.try_emplace(key);
        if (inserted) it->second = std::make_unique<Pool<T>>();
        return *static_cast<Pool<T>*>(it->second.get());
    }

    template <Component T>
    Pool<T>* find_pool() {
        const auto it = pools_.find(std::type_index{typeid(T)});
        return it == pools_.end() ? nullptr : static_cast<Pool<T>*>(it->second.get());
    }

    template <Component T>
    const Pool<T>* find_pool() const {
        const auto it = pools_.find(std::type_index{typeid(T)});
        return it == pools_.end() ? nullptr : static_cast<const Pool<T>*>(it->second.get());
    }

    std::vector<Entity::Value> generations_;
    std::vector<bool> alive_;
    std::vector<Entity::Value> free_;
    std::unordered_map<std::type_index, std::unique_ptr<IPool>> pools_;
};

} // namespace mcr::core

