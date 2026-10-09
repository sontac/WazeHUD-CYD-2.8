#pragma once

namespace waze_hud {

// Starts the CYD 2.8" XPT2046 touch controller.
// A valid double-tap cycles V1 -> V2 -> V3 -> V1.
void startTouchController();

}  // namespace waze_hud
