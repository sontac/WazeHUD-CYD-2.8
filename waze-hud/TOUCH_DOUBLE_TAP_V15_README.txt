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
