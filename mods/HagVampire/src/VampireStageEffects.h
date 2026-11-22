#pragma once

namespace hag::vampire_stage {

inline constexpr bool kDefaultServantLevelCap = true;

void OnDataLoaded(bool reverse);
void ApplyProgression(bool reverse);
bool ServantLevelCapEnabled() noexcept;
void SetServantLevelCapEnabled(bool enabled) noexcept;

}  // namespace hag::vampire_stage
