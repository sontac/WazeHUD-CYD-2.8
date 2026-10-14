WazeHUD-CYD-USB-v1.1.4-OVERSPEED-DISTANCE-FIX1

Merged BLE FIX4 overspeed-border and distance behavior into the known USB LANE-FIX1 baseline.

Changes:
- Overspeed border is 6 px and all four edges share a 500 ms blink cycle (250 ms on / 250 ms off).
- Redraw all four edges on every HUD render so later partial UI updates are less likely to leave missing segments.
- Distances to maneuver and upcoming alerts at 20 m or less display 0 m.
- Preserves USB transport status, USB splash, lane declaration fix, touch controls, and V1/V2/V3 UI from the USB baseline.

Source ZIP was structurally validated. This package has NOT been built with ESP-IDF in this environment; build and test before use.
