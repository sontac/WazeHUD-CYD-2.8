WazeHUD-CYD USB V3 merge test 1
================================

Base: USB source package supplied in this conversation (USB lane declaration fix branch).
Display changes ported from accepted BLE V3-LANE-CLOCK-TEST1:
- V3 top header shows ETA: remaining distance.
- V3 maneuver panel moved left and maneuver distance position updated.
- V3 speed uses native kNumberLarge font; speed cluster and speed-limit sign match accepted BLE V3 alignment.
- V3 speed-limit sign uses 108px red/white circular style and is moved left to avoid clipping.
- V3 lower area has two side-by-side alerts; icon and distance labels raised to the accepted TEST4-ALIGN positions.
- V3 lane area is 160px wide; up to 10 lanes are spaced to fit. When no lane guidance is present, a large HH:MM clock is shown with blinking colon.
- V3 street name is limited to the left 160px viewport and scrolls when long.
- Distance correction is shared by maneuver and alert distances: <20m => 0; 20-30m => 10; >30m => distance-20.
- V2 street geometry remains a full-width row; V3 uses a separate V3Street rectangle.

USB-specific serial transport, HLP protocol and device declaration were retained from the USB base. BLE transport code was not imported.

Validation:
- ZIP integrity checked.
- Source-level checks confirm USB transportColor remains and BLE-only bleSignalColor was not imported.
- This package has NOT been built with ESP-IDF here. Please build and test before treating it as stable.
