WazeHUD-CYD V3 Calendar - Left of Speed (BLE/USB variant)

Placement:
- The calendar is drawn ONLY in the V3 next-turn/maneuver panel (layout::V2Maneuver),
  at x=0..119, immediately to the LEFT of the speed cluster.
- It is shown only when SpeedDisplayMode::NoNavigation is selected and state.navigationActive is false.
- While navigation is active, the original turn arrow and maneuver distance are rendered unchanged.
- No calendar is added to the lane-guidance panel or any other HUD region.

Calendar layout:
1. "THỨ" in green.
2. Weekday in white.
3. Gregorian date in larger white native bitmap text, format DD/MM/YY.
4. Vietnamese lunar date in larger white native bitmap text, format DD/MM ÂL (or DD/MM NH for a leap month).
5. Lunar year Can Chi in white.

Time:
- Wi-Fi/NTP support is retained.
- When NTP is unavailable, the calendar uses a valid navigation-provided clock if present,
  otherwise the ESP system clock if it has a valid epoch.
- If no valid time source exists, the panel shows a synchronization message.

Preservation:
- The V3 lane guidance, turn arrows while navigating, speed readout, speed-limit sign,
  alerts, and BLE/USB transport are otherwise unchanged.
- No bitmap-font renderer or bitmap asset is changed.

Build status:
- ZIP structure and patch placement were checked.
- Full ESP-IDF firmware build has NOT been run in this environment; run `idf.py build`
  with the appropriate ESP-IDF toolchain before flashing.
