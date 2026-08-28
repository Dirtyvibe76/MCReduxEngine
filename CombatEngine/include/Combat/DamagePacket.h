#pragma once

#include "Core/ECS/Entity.h"

namespace mcr::combat {
struct DamagePacket final { core::Entity source; core::Entity target; float amount{0.0F}; bool critical{false}; };
} // namespace mcr::combat

