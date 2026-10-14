WazeHUD-CYD-USB-v1.1.4-LANE-DIAG-FIX1

Purpose
- Diagnostic test for missing lane-guidance arrows on the USB/UART HLP branch.
- Preserves the splash screen from USB v1.1.4 SPLASH-TEST-FIX2 and existing renderer/UI logic.
- Does not change BLE source or lane drawing geometry.

Selective changes
1. USB HLP sends a device declaration once it receives the first valid HLP frame.
2. The declaration advertises lanes capability and requests the "lan" state field, matching the BLE branch's field list, with transport="usb".
3. Serial logs report whether incoming state frames contain "lan", how many lane entries were received, and how many were decoded.
4. Declaration/log counters reset on transport reset or HLP session timeout.

Build and test
1. Extract this ZIP into a new folder and build using ESP-IDF.
2. Flash and connect to the Android app using USB as usual.
3. At an intersection where BLE previously showed lane arrows, wait for the lane guidance to appear.
4. Capture the Serial/ESP-IDF log around these messages:
   - "Sent USB HLP dev declaration requesting 4 Hz, including field lan"
   - "Lane diagnostic: lan_field=... raw_count=... decoded_count=..."

How to interpret logs
- lan_field=missing/non-array raw_count=-1: app is not sending a usable "lan" array. If this persists after the declaration, we need to investigate app-side USB handling or the HLP response timing.
- raw_count > 0 and decoded_count > 0: lane data reaches the decoder; investigate renderer state/UI selection or whether the navigation snapshot is stale.
- raw_count > 0 and decoded_count=0: payload shape/values are not accepted by the decoder; capture the relevant HLP state JSON (remove any private data first).
- raw_count=0: app sent an empty lane array, likely no lane guidance available for that state.

Important
- This is a diagnostic test build, not a confirmed fix.
- The source has not been built with ESP-IDF in this environment. Build before flashing. Keep the known-good USB firmware ZIP for rollback.
