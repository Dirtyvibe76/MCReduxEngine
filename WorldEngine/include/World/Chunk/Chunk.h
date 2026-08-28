#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mcr::world {

struct ChunkCoord final {
    int x{0};
    int z{0};
    friend constexpr bool operator==(ChunkCoord, ChunkCoord) noexcept = default;
};

class Chunk final {
public:
    static constexpr int width = 16;
    static constexpr int height = 256;
    static constexpr std::size_t volume = static_cast<std::size_t>(width) * height * width;
    using BlockId = std::uint16_t;

    explicit Chunk(ChunkCoord coord = {}) noexcept : coord_(coord) {}

    [[nodiscard]] ChunkCoord coord() const noexcept { return coord_; }
    [[nodiscard]] BlockId block(int x, int y, int z) const;
    void set_block(int x, int y, int z, BlockId block);

private:
    [[nodiscard]] static std::size_t index(int x, int y, int z);

    ChunkCoord coord_;
    std::array<BlockId, volume> blocks_{};
};

} // namespace mcr::world

