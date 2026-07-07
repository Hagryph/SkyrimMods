#pragma once

#include <cstdint>

namespace hag::animation {

bool PlayIdleWithTargetAutoStop(std::uint32_t actorFormID,
                                std::uint32_t idleFormID,
                                std::uint32_t targetFormID,
                                std::uint32_t stopIdleFormID,
                                std::uint32_t stopDelayMs);
bool StopIdle(std::uint32_t actorFormID, std::uint32_t stopIdleFormID);

}  // namespace hag::animation
