#pragma once

#include <cstdint>

namespace mcr::combat {

struct FrameData final {
    std::uint16_t startup{0};
    std::uint16_t active{0};
    std::uint16_t recovery{0};
};

} // namespace mcr::combat

