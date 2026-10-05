# WazeHUD cho màn hình CYD 2.8 inch (Bản tối ưu Taplo Ô tô)

> [!NOTE]
> **Lời cảm ơn & Tôn trọng bản quyền tác giả (Credits & Respect):**
> Dự án này là phiên bản phát triển & tối ưu hóa mở rộng dựa trên mã nguồn gốc [WazeHUD của tác giả ShindouAris](https://github.com/ShindouAris/WazeHUD) và cộng đồng [WazeMod Vietnam](https://wazemod.io.vn). Xin trân trọng ghi nhận và cảm ơn công sức to lớn của tác giả gốc đã tạo nên nền tảng ban đầu tuyệt vời cho cộng đồng người dùng Waze! Xin hãy luôn tôn trọng tác giả gốc và chia sẻ có trích nguồn.

---

## 📥 Tải Firmware (Download)

👉 **Truy cập trang phát hành chính thức:** **[GitHub Releases - WazeHUD](https://github.com/quangdo92/waze-hud/releases/latest)**

Tại trang Releases, chọn tải file `.bin` All-in-One phù hợp với phiên bản mạch phần cứng của bạn:

| Phiên bản mạch phần cứng | File Firmware trong Release | Link tải trực tiếp (.bin) | Nhánh mã nguồn |
|---|---|:---:|:---:|
| **CYD 2.8" (2 Cổng USB Type-C / Micro - BLE)** | `waze_hud_cyd_28_factory.bin` | [📥 **Tải về**](https://github.com/quangdo92/waze-hud/releases/download/v2.8.2/waze_hud_cyd_28_factory.bin) | [`CYD2.8-V2USB`](https://github.com/quangdo92/waze-hud/tree/CYD2.8-V2USB) |
| **CYD 2.8" (1 Cổng USB Micro - BLE)** | `waze_hud_cyd_28_1usb_factory.bin` | [📥 **Tải về**](https://github.com/quangdo92/waze-hud/releases/download/v2.8.2/waze_hud_cyd_28_1usb_factory.bin) | [`CYD2.8-V1USB`](https://github.com/quangdo92/waze-hud/tree/CYD2.8-V1USB) |
| **CYD 2.8" Cáp USB Serial (CH340 115200 baud)** | `waze_hud_cyd_28_usb_factory.bin` | [📥 **Tải về**](https://github.com/quangdo92/waze-hud/releases/download/v2.8.2/waze_hud_cyd_28_usb_factory.bin) | [`2.8-in-CYD-USB`](https://github.com/quangdo92/waze-hud/tree/2.8-in-CYD-USB) |

> [!TIP]
> Toàn bộ các file trên đều là bản **Factory All-in-One** (đã tích hợp sẵn Bootloader, Partition Table và Firmware). Chỉ cần nạp duy nhất 1 file tại offset **`0x0`** là thiết bị khởi động chạy ngay!

### 🌐 Cách 1: Nạp trực tiếp qua Web Flasher (Khuyên dùng - Cực dễ cho mọi người)
Bạn có thể sử dụng công cụ Web Flasher của tác giả WazeMod trực tiếp trên trình duyệt web (Chrome, Edge, Cốc Cốc trên máy tính hoặc điện thoại Android) mà không cần cài đặt Python hay bất kỳ phần mềm nào:

1. Bấm nút **📥 Tải về** ở bảng trên để tải file `.bin` phù hợp về máy.
2. Cắm cáp kết nối mạch CYD với máy tính qua cổng USB.
3. Truy cập công cụ web flasher của tác giả: 👉 **[https://wazemod.io.vn/flash-firmware](https://wazemod.io.vn/flash-firmware)**
4. Bấm **Kết nối**, chọn đúng cổng COM của mạch ESP32 CYD.
5. Chọn file `.bin` đã tải, đảm bảo địa chỉ nạp là **`0x0`** và bấm **Flash** để hoàn tất.

*(Mẹo: Nếu mạch không vào được chế độ flash, hãy nhấn giữ nút **BOOT**, bấm nhả nút **RESET**, sau đó thả nút **BOOT** rồi kết nối lại).*

### 💻 Cách 2: Nạp qua dòng lệnh esptool (Dành cho Developer / Terminal)

```bash
# Nạp file Factory tương ứng tại offset 0x0
python -m esptool --chip esp32 -b 460800 write_flash 0x0 <ten_file_factory.bin>
```

---

## Tính năng chính & Cải tiến nổi bật

### 🚗 Tối ưu chuyên biệt cho Taplo Ô tô & Lái xe an toàn
- **Nền đen tuyệt đối (OLED-style True Black):** Toàn bộ hình nền và các panel giao diện sử dụng màu đen thuần túy `0x0000`, tăng tối đa độ tương phản cho màn LCD, loại bỏ hoàn toàn ánh sáng viền xám mờ và chống chói lóa mắt khi đặt trên taplo xe.
- **Tự động điều chỉnh độ sáng Ngày / Đêm theo thời gian thực:**
  - **06:00 – 17:30 (Ban ngày):** Đèn nền tự động sáng tối đa 100% để hiển thị sắc nét ngay cả dưới ánh nắng gắt.
  - **17:30 – 19:00 (Hoàng hôn):** Tự động giảm dần độ sáng từ 100% xuống 30% một cách êm ái.
  - **19:00 – 05:00 (Ban đêm):** Duy trì độ sáng 30% dịu mắt, chống lóa khoang lái trong đêm tối.
  - **05:00 – 06:00 (Bình minh):** Tăng dần độ sáng từ 30% trở lại 100%.
- **Biển báo cảnh báo & khoảng cách cỡ lớn:**
  - Icon biển báo cảnh báo phía trước được phóng to lên kích thước **60×60 px / 56×56 px** (so với 44px ban đầu).
  - Chữ hiển thị số mét khoảng cách cảnh báo được nâng cấp lên font cỡ vừa **`kTextMedium` (22 px)** to rõ, quan sát cực kỳ dễ dàng khi xe chạy ở tốc độ cao.
- **Tối ưu khu vực điều hướng rẽ:**
  - Mũi tên chỉ hướng rẽ được nâng cao lên sát mép trên (`y=16`), giải phóng không gian phía dưới.
  - Chữ số mét khoảng cách tới khúc rẽ tiếp theo được phóng to thành font lớn **`kTextMedium` ở vị trí `y=88`**.
- **Viền biển báo Đỏ tươi bão hòa 100% (`0xF800`):**
  - Chuyển toàn bộ màu viền của tất cả biển báo giới hạn tốc độ (10–120 km/h) và biển báo cấm (cấm vượt, cấm ô tô, cấm rẽ, camera đèn đỏ...) từ màu hồng phấn/cam san hô cũ sang màu đỏ cờ rực rỡ chuẩn biển báo giao thông đường bộ, khử răng cưa mượt mà.
- **Tối ưu đèn LED RGB lưng:**
  - Khi xe di chuyển ở tốc độ cho phép, đèn LED lưng tự động **TẮT HOÀN TOÀN** để không gây hắt sáng khó chịu lên kính lái ban đêm.
  - Chỉ nhấp nháy đỏ cảnh báo với tần số 2 Hz khi phát hiện xe chạy quá tốc độ giới hạn.
- **Giao diện HUD tối giản, không phân tâm:**
  - Loại bỏ biểu tượng Bluetooth và vạch sóng ở góc trên màn hình lái xe để giao diện sạch sẽ, tập trung hoàn toàn vào cảnh báo và điều hướng.
  - Loại bỏ hoàn toàn mảng bitmap Boot Logo để **tiết kiệm gần 28 KB bộ nhớ Flash ROM**, đồng thời căn giữa màn hình chờ kết nối chữ WazeHUD đẹp mắt.

### 📡 Tính năng cốt lõi kế thừa
- Kết nối không dây BLE tức thì với Waze Mod (tên thiết bị `WazeHUD`), tốc độ làm tươi 4 Hz.
- Hiển thị tốc độ xe thực tế, biển báo tốc độ giới hạn, hướng rẽ và khoảng cách tới lượt rẽ.
- Cảnh báo chướng ngại vật: Tai nạn, kẹt xe (kèm mức độ & số phút trễ), công trường, xe dừng, ngập nước, đường đóng, camera phạt nguội, kiểm tra khoảng cách, v.v.
- Hiển thị làn đường (Lane Guidance) tối đa 10 làn với mũi tên chỉ hướng được khuyến nghị.
- Hiển thị ETA (giờ đến), tên đường tiếng Việt có dấu (tự động chạy chữ nếu quá dài) và đồng hồ.
- Hai chế độ hiển thị: Ưu tiên tốc độ xe hoặc Ưu tiên biển báo giới hạn tốc độ.
- Hỗ trợ lật gương (Mirror HUD) để chiếu hắt kính lái và xoay màn hình 180° linh hoạt qua nút bấm BOOT.
- Cơ chế Dirty-region rendering: Chỉ vẽ lại đúng vùng có dữ liệu thay đổi, đảm bảo tốc độ đáp ứng tối đa và không rung giật khung hình.

## Nhật ký thay đổi (Changelog)

### [CYD2.82USB] - 2026-10-05
#### 🚦 Cảnh báo Đèn giao thông mới (Traffic Light Alert - Mã 75)
- **Hỗ trợ mã cảnh báo 75:** Bổ sung `AlertKind::TrafficLight = 75` đồng bộ với bản cập nhật mới nhất của WazeMod Android (thay vì bị fallback về cảnh báo tam giác chấm than chung `Hazard`).
- **Biểu tượng hộp đèn giao thông dạng viên thuốc (Pill shape):** Thiết kế độc quyền chuẩn pixel-art với viền bạc mảnh 1px, thân đen, 3 bóng đèn Đỏ - Vàng - Xanh to tròn rực rỡ nổi bật rõ nét trên nền đen taplo HUD (chuẩn 44×44 px cho cảnh báo chính và 26×26 px cho cảnh báo phụ).
- **Thuật toán ưu tiên cảnh báo:** Tích hợp mức ưu tiên 60 điểm cho đèn giao thông trong phạm vi 250m để hiển thị kịp thời trước ngã tư.

#### 🛣️ Cơ chế lưu giữ làn đường thông minh theo vận tốc (Speed-Aware Lane Retention)
- Tạm ngưng đếm ngược 15s khi tốc độ < 10 km/h (dừng đèn đỏ, kẹt xe): Giữ nguyên số làn đường hiển thị giúp tài xế không bị mất thông tin dẫn làn khi đang dừng chờ.
- Chỉ tính đếm thời gian 15s khi xe bắt đầu di chuyển đạt tốc độ >= 10 km/h.
- Tự động ghi đè và cập nhật tức thì không độ trễ ngay khi Waze gửi thông tin làn đường tiếp theo.

### [CYD2.82USB / 2.81USB] - 2026-09-24
#### ✨ Đồ họa & Tương phản (Display & Contrast)
- **True Black OLED Mode:** Thay thế màu nền xám mờ (`0x030507`) và màu panel (`0x080B0E`) bằng màu đen tuyệt đối `rgb565(0, 0, 0)`, đem lại tương phản cao nhất và chống chói khi để trên taplo xe.
- **Vivid Red Speed & Prohibition Signs:** Thay thế định nghĩa `colors::Red` thành `rgb565(255, 0, 0)` (`0xF800`). Viết thuật toán tái lập trình 33.514 pixel viền trên 59 mảng asset biển báo trong `generated_assets.cpp` (toàn bộ các biển 10–120 km/h, biển `NoSpeed`, và các biển cấm rẽ, cấm vượt, camera...) sang màu đỏ cờ rực rỡ, giữ nguyên khử răng cưa viền trắng.

#### 📐 Bố cục & Kích thước (Layout & Typography)
- **Phóng to biển cảnh báo:** Tăng kích thước bán kính biển cảnh báo chính từ 22 px lên 28 px / 30 px (đường kính lên đến 60 px) khi hiển thị trên CYD 2.8".
- **Phóng to số khoảng cách cảnh báo:** Thay thế font `kTextSmall` thành `kTextMedium` (22 px) tại `y=64` / `y=72`.
- **Tối ưu mũi tên rẽ:** Đẩy mũi tên rẽ lên `y=16` (thay vì `y=34`), tăng kích thước khoảng cách rẽ thành `kTextMedium` đặt tại `y=88`.

#### ☀️ Độ sáng & Tiện nghi (Auto-Brightness & Comfort)
- **Lịch trình độ sáng Ngày / Đêm:** Bổ sung thuật toán tự động điều chỉnh độ sáng đèn nền theo giờ địa phương:
  - `06:00 – 17:30`: Sáng 100% (chống chói nắng).
  - `17:30 – 19:00`: Giảm dần đều từ 100% về 30%.
  - `19:00 – 05:00`: Duy trì 30% (êm dịu mắt ban đêm).
  - `05:00 – 06:00`: Tăng dần đều từ 30% lên 100%.
- **Chế độ LED lưng thông minh:** Trong `overspeedLedTask`, tắt hoàn toàn LED xanh lục khi xe chạy bình thường (chỉ nháy đỏ cảnh báo khi chạy quá tốc độ).

#### 🧹 Tối ưu bộ nhớ & Tinh giản (Cleanup & ROM Optimization)
- **Gỡ bỏ Boot Logo:** Xóa bỏ mảng dữ liệu ảnh tĩnh `kBootIcon` (96×96 px) trong `generated_assets.cpp`, giải phóng **27.6 KB bộ nhớ Flash**.
- **Căn giữa màn hình chờ:** Điều chỉnh chữ `WazeHUD` và trạng thái kết nối nằm cân đối ngay giữa màn hình (rộng 320 px).
- **Ẩn biểu tượng kết nối phụ:** Loại bỏ việc vẽ biểu tượng Bluetooth và các vạch sóng RSSI ở góc trên giao diện lái xe.

---

## Kết nối với Waze Mod

1. Bật Bluetooth trên điện thoại.
2. Cấp quyền Thiết bị ở gần (Nearby devices) cho ứng dụng Waze Mod.
3. Trong phần cài đặt **HUD Link** của Waze Mod, chọn kết nối với thiết bị **`WazeHUD`**.
4. Thiết lập lộ trình dẫn đường trong Waze để HUD bắt đầu nhận dữ liệu.

---

## Trạng thái đèn LED RGB phía sau

| Trạng thái xe & kết nối | Màu LED RGB | Ý nghĩa |
|---|---|---|
| Chưa kết nối điện thoại | Đổi màu RGB liên tục | Đang phát sóng BLE chờ kết nối |
| Đã kết nối, chờ dữ liệu dẫn đường | Xanh dương | Đã kết nối BLE thành công |
| Tốc độ bình thường (đang dẫn đường) | **TẮT** | Giữ không gian tối êm dịu, không gây chói mắt |
| Chạy quá tốc độ giới hạn | **Nháy đỏ 2 Hz** | Cảnh báo xe đang chạy vượt tốc độ cho phép |

---

## Thao tác với nút cứng (BOOT / KEY)

| Thao tác nút BOOT | Tác dụng |
|---|---|
| Nhấn 1 lần | Xoay ngược màn hình 180° (phù hợp cắm cáp từ cạnh trên hoặc dưới) |
| Nhấn đúp (2 lần) | Bật / Tắt chế độ lật gương (Mirror HUD) chiếu hắt kính lái |
| Nhấn giữ | Hiển thị màn hình chuẩn đoán trạng thái thiết bị và BLE |

*Các thiết lập xoay và lật gương được tự động lưu vào bộ nhớ NVS và không bị mất khi rút nguồn.*

---

## Cấu hình từ Waze Mod

Khi Waze Mod hỗ trợ `device_config`, HUD gửi lên các thiết lập:

| Thiết lập | Giá trị | Ý nghĩa |
|---|---|---|
| Độ sáng | 10–100%, bước 5% | Điều chỉnh đèn nền màn hình thủ công |
| Giao diện | Tự động / Ban ngày / Ban đêm | Chọn màu giao diện |
| Hiển thị tốc độ | Tốc độ hiện tại / Biển giới hạn | Chọn thành phần tốc độ chính |
| Hiện tên đường | Bật / Tắt | Ẩn hoặc hiện tên đường |
| Phản chiếu HUD | Bật / Tắt | Lật ngang để phản chiếu kính lái |
| Xoay màn hình | Bật / Tắt | Xoay 180° theo hướng lắp mạch |
| Ngưỡng quá tốc | −10 đến +5 km/h | Bù vào giới hạn trước khi cảnh báo |
| Dịch ngang | −5 đến +5 px | Tinh chỉnh vị trí giao diện |
| Dịch dọc | −5 đến +5 px | Tinh chỉnh vị trí giao diện |

---

## Thông số phần cứng ESP32-2432S028 (CYD 2.8")

| Thành phần | Thông số / Chân kết nối |
|---|---|
| Vi điều khiển | ESP32-WROOM-32 (Dual Core 240 MHz, 4MB Flash, No PSRAM) |
| Màn hình LCD | ILI9341 2.8 inch, SPI2 @ 40 MHz, BGR |
| Độ phân giải | 320×240 Landscape (sử dụng dirty stripe xoay mềm 240×320) |
| Chân SPI LCD | MOSI: GPIO 13 \| MISO: GPIO 12 \| SCLK: GPIO 14 \| CS: GPIO 15 \| DC: GPIO 2 |
| Đèn nền (Backlight) | GPIO 21 (LEDC PWM active-high) |
| LED RGB sau lưng | Đỏ: GPIO 4 \| Xanh lá: GPIO 16 \| Xanh dương: GPIO 17 (active-low) |
| Nút bấm BOOT | GPIO 0 (active-low) |# WazeHUD-CYD-2.8
