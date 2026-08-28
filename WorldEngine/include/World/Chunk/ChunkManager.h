#pragma once

#include "World/Chunk/Chunk.h"

#include <cstddef>
#include <memory>
#include <unordered_map>

namespace std {
template <>
struct hash<mcr::world::ChunkCoord> {
    size_t operator()(const mcr::world::ChunkCoord coord) const noexcept {
        return (static_cast<size_t>(static_cast<unsigned>(coord.x)) << 32U)
            ^ static_cast<unsigned>(coord.z);
    }
};
} // namespace std

namespace mcr::world {

class ChunkManager final {
public:
    Chunk& get_or_create(ChunkCoord coord);
    [[nodiscard]] Chunk* find(ChunkCoord coord) noexcept;
    [[nodiscard]] const Chunk* find(ChunkCoord coord) const noexcept;
    [[nodiscard]] std::size_t loaded_count() const noexcept { return chunks_.size(); }

private:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> chunks_;
};

} // namespace mcr::world

