#include "World/Chunk/Chunk.h"

#include <stdexcept>

namespace mcr::world {

std::size_t Chunk::index(const int x, const int y, const int z) {
    if (x < 0 || x >= width || y < 0 || y >= height || z < 0 || z >= width) {
        throw std::out_of_range("block coordinate is outside the chunk");
    }
    return static_cast<std::size_t>(y) * width * width + static_cast<std::size_t>(z) * width + x;
}

Chunk::BlockId Chunk::block(const int x, const int y, const int z) const {
    return blocks_[index(x, y, z)];
}

void Chunk::set_block(const int x, const int y, const int z, const BlockId block) {
    blocks_[index(x, y, z)] = block;
}

} // namespace mcr::world

