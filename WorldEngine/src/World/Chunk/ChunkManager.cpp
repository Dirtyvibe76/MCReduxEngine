#include "World/Chunk/ChunkManager.h"

namespace mcr::world {

Chunk& ChunkManager::get_or_create(const ChunkCoord coord) {
    auto [it, inserted] = chunks_.try_emplace(coord);
    if (inserted) it->second = std::make_unique<Chunk>(coord);
    return *it->second;
}

Chunk* ChunkManager::find(const ChunkCoord coord) noexcept {
    const auto it = chunks_.find(coord);
    return it == chunks_.end() ? nullptr : it->second.get();
}

const Chunk* ChunkManager::find(const ChunkCoord coord) const noexcept {
    const auto it = chunks_.find(coord);
    return it == chunks_.end() ? nullptr : it->second.get();
}

} // namespace mcr::world

