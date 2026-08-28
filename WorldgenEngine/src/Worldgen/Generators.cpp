#include "Worldgen/BiomeGenerator.h"
#include "Worldgen/BossGenerator.h"
#include "Worldgen/MobGenerator.h"
#include "Worldgen/StructureGenerator.h"

#include "Core/Math/Noise.h"

#include <algorithm>

namespace mcr::worldgen {

world::BiomeSpec BiomeGenerator::plains() {
    return {"Plains", 1, world::PhysicsType::temperate, 1.05F, 1.05F, 0.95F};
}

world::BiomeSpec BiomeGenerator::generate(const std::uint64_t seed, const int x, const int z) {
    const core::Noise noise{seed};
    const float value = noise.value(x, z);
    if (value < -0.5F) return {"Beach", 1, world::PhysicsType::temperate, 0.9F, 1.0F, 1.0F};
    if (value < 0.0F) return {"Forest", 1, world::PhysicsType::temperate, 0.95F, 1.0F, 1.0F};
    if (value < 0.5F) return plains();
    return {"Desert", 2, world::PhysicsType::heat, 0.85F, 0.8F, 1.15F};
}

MobSpec MobGenerator::generate(const std::uint64_t seed, const std::uint8_t tier, const bool apex) {
    return {(seed & 1U) ? (apex ? "Generated Stalker" : "Generated Forager")
                        : (apex ? "Generated Hunter" : "Generated Grazer"),
            std::clamp<std::uint8_t>(tier, 1, 5), apex};
}


BossSpec BossGenerator::generate(const std::uint64_t seed, const std::uint8_t tier) {
    const auto safe_tier = std::clamp<std::uint8_t>(tier, 2, 6);
    const std::uint8_t phases = safe_tier < 4 ? 0 : safe_tier == 4 ? 1 : safe_tier == 5 ? 3 : 4;
    return {(seed & 1U) ? "Generated Warden" : "Generated Titan", safe_tier, phases};
}

world::StructureSpec StructureGenerator::generate(const std::uint64_t seed, const std::uint8_t tier) {
    const auto safe_tier = std::clamp<std::uint8_t>(tier, 1, 6);
    const auto rarity = safe_tier == 6 ? world::StructureRarity::worldforged
        : safe_tier >= 4 && seed % 10 == 0 ? world::StructureRarity::legendary
        : seed % 4 == 0 ? world::StructureRarity::rare : world::StructureRarity::common;
    return {"Generated Structure", safe_tier, rarity, rarity == world::StructureRarity::legendary};
}

} // namespace mcr::worldgen
