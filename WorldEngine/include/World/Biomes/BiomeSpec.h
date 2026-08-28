#pragma once

#include <cstdint>
#include <string>

namespace mcr::world {

enum class PhysicsType : std::uint8_t {
    temperate, heat, cold, humidity, toxin, wind, darkness, spores, altitude, worldforged
};

struct BiomeSpec final {
    std::string name;
    std::uint8_t tier{1};
    PhysicsType physics{PhysicsType::temperate};
    float movement_multiplier{1.0F};
    float stamina_regen_multiplier{1.0F};
    float tool_wear_multiplier{1.0F};
};

} // namespace mcr::world

