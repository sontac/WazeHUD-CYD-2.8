WazeHUD CYD 2.8 - Touch V15

Function:
- A single tap is ignored.
- A double tap anywhere on the screen cycles V1 -> V2 -> V3 -> V1.
- Holding the screen does not repeat the action.
- The selected mode is saved in NVS, so it survives reboot.

Touch hardware:
- XPT2046 on SPI3
- SCK GPIO25
- MOSI GPIO32
- MISO GPIO39
- CS GPIO33
- IRQ GPIO36

LCD SPI2 and existing HUD/BLE rendering are preserved.


Swipe brightness update (experimental):
- Swipe from top to bottom anywhere on the screen: decrease brightness by 10 percentage points.
- Swipe from bottom to top anywhere on the screen: increase brightness by 10 percentage points.
- Brightness is clamped to 10%-100% and saved to settings. A manual swipe disables automatic ambient-light brightness so the selected level is not immediately overwritten.
- Double-tap still cycles V1 -> V2 -> V3.
- The gesture currently assumes XPT2046 raw Y increases from top to bottom; if the physical panel reports the opposite direction, invert kRawYIncreasesDownward in main/touch_controller.cpp.
