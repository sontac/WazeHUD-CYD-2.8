#include "touch_controller.h"

#include "config/device_config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace waze_hud {
namespace {
constexpr char kTag[] = "TOUCH";

// CYD 2.8" (ESP32-2432S028-class) XPT2046 wiring.
// Touch uses SPI3, completely separate from the LCD SPI2 bus.
constexpr gpio_num_t kTouchSck  = GPIO_NUM_25;
constexpr gpio_num_t kTouchMosi = GPIO_NUM_32;  // T_DIN
constexpr gpio_num_t kTouchMiso = GPIO_NUM_39;  // T_OUT
constexpr gpio_num_t kTouchCs   = GPIO_NUM_33;
constexpr gpio_num_t kTouchIrq  = GPIO_NUM_36;
constexpr spi_host_device_t kTouchHost = SPI3_HOST;

// Gesture tuning: vertical swipes work anywhere on the screen.
// A swipe segment of kSwipeThresholdRaw changes brightness by 10%; longer
// swipes apply multiple 10% steps in one gesture.
constexpr TickType_t kDoubleTapWindow = pdMS_TO_TICKS(350);
constexpr TickType_t kDebounceDelay = pdMS_TO_TICKS(25);
constexpr TickType_t kPollDelay = pdMS_TO_TICKS(8);
constexpr int kMinPressure = 40;
constexpr int kSwipeThresholdRaw = 120;
constexpr int kBrightnessStepPercent = 10;
// The supplied hardware logs show physical vertical swipes changing raw X
// much more than raw Y. Select the dominant raw axis instead of switching to Y
// merely because its movement crosses the threshold. Raw X direction is
// inverted relative to the desired screen gesture direction on this device.
constexpr bool kRawYIncreasesDownward = true;
constexpr bool kRawXIncreasesDownward = false;
constexpr int kEndpointSamples = 5;
constexpr int kMaxBrightnessSegmentsPerSwipe = 3;

spi_device_handle_t s_dev = nullptr;

uint16_t read12(uint8_t command) {
    if (!s_dev) return 0;
    uint8_t tx[3] = {command, 0, 0};
    uint8_t rx[3] = {};
    spi_transaction_t transaction{};
    transaction.length = 24;
    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;
    if (spi_device_transmit(s_dev, &transaction) != ESP_OK) return 0;
    return static_cast<uint16_t>((static_cast<uint16_t>(rx[1]) << 8 | rx[2]) >> 3);
}

bool readTouchSample(uint16_t *xOut = nullptr, uint16_t *yOut = nullptr) {
    if (gpio_get_level(kTouchIrq) != 0) return false;
    const uint16_t x = read12(0xD0);
    const uint16_t y = read12(0x90);
    const uint16_t z1 = read12(0xB0);
    (void)read12(0xC0);  // z2 is not needed; IRQ + z1 are sufficient here.
    const bool valid = z1 > kMinPressure && x > 20 && x < 4070 && y > 20 && y < 4070;
    if (valid) {
        if (xOut) *xOut = x;
        if (yOut) *yOut = y;
    }
    return valid;
}

void waitForRelease() {
    int releasedSamples = 0;
    while (releasedSamples < 3) {
        if (gpio_get_level(kTouchIrq) != 0) {
            ++releasedSamples;
        } else {
            releasedSamples = 0;
        }
        vTaskDelay(kDebounceDelay);
    }
}

uint16_t medianSamples(uint16_t *values, int count) {
    if (count <= 0) return 0;
    for (int i = 1; i < count; ++i) {
        const uint16_t value = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > value) {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = value;
    }
    return values[count / 2];
}

// Finger is already down. Use medians of several first/last samples rather
// than a single noisy ADC reading. For diagonal swipes, direction is determined
// by vertical movement (raw Y) only; horizontal movement is intentionally ignored.
// Track both raw axes. Use the first press sample as the start point and a
// median of the final samples as the endpoint, avoiding a moving "start" window
// that can swallow most of a straight swipe.
void captureGesture(uint16_t initialX, uint16_t initialY,
                    uint16_t &endX, uint16_t &endY) {
    uint16_t lastX[kEndpointSamples] = {};
    uint16_t lastY[kEndpointSamples] = {};
    int count = 0;
    lastX[count] = initialX;
    lastY[count] = initialY;
    ++count;

    while (gpio_get_level(kTouchIrq) == 0) {
        uint16_t x = 0, y = 0;
        if (readTouchSample(&x, &y)) {
            if (count < kEndpointSamples) {
                lastX[count] = x;
                lastY[count] = y;
                ++count;
            } else {
                for (int i = 1; i < kEndpointSamples; ++i) {
                    lastX[i - 1] = lastX[i];
                    lastY[i - 1] = lastY[i];
                }
                lastX[kEndpointSamples - 1] = x;
                lastY[kEndpointSamples - 1] = y;
            }
        }
        vTaskDelay(kPollDelay);
    }
    waitForRelease();

    endX = medianSamples(lastX, count);
    endY = medianSamples(lastY, count);
}

bool waitForPressUntil(TickType_t deadline) {
    while (xTaskGetTickCount() < deadline) {
        if (readTouchSample()) return true;
        vTaskDelay(kPollDelay);
    }
    return false;
}

bool handleVerticalSwipe(uint16_t startX, uint16_t startY,
                        uint16_t endX, uint16_t endY) {
    const int deltaX = static_cast<int>(endX) - static_cast<int>(startX);
    const int deltaY = static_cast<int>(endY) - static_cast<int>(startY);
    const int absX = std::abs(deltaX);
    const int absY = std::abs(deltaY);

    // Pick the axis with the larger displacement. The device logs show that
    // a vertical swipe can produce a small but >threshold deltaY, so a simple
    // "Y crossed threshold" test can incorrectly select Y over a much larger X.
    const bool useXAxis = absX >= absY;
    const int delta = useXAxis ? deltaX : deltaY;
    const int distance = std::abs(delta);
    if (distance < kSwipeThresholdRaw) {
        ESP_LOGD(kTag, "No swipe: dx=%d dy=%d", deltaX, deltaY);
        return false;
    }

    const bool increasesDownward = useXAxis
        ? kRawXIncreasesDownward : kRawYIncreasesDownward;
    const bool movedDown = increasesDownward ? delta > 0 : delta < 0;
    const int detectedSegments = std::max(1, distance / kSwipeThresholdRaw);
    const int segments = std::min(detectedSegments, kMaxBrightnessSegmentsPerSwipe);
    const int magnitude = segments * kBrightnessStepPercent;
    const int adjustment = movedDown ? -magnitude : magnitude;
    const esp_err_t result = DeviceConfig::instance().adjustBrightness(adjustment);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Swipe brightness change failed: %s", esp_err_to_name(result));
    } else {
        const DeviceSettings settings = DeviceConfig::instance().snapshot();
        ESP_LOGI(kTag, "SWIPE %s axis=%c dx=%d dy=%d segments=%d -> brightness %u%%",
                 movedDown ? "DOWN" : "UP", useXAxis ? 'X' : 'Y',
                 deltaX, deltaY, segments, settings.brightness);
    }
    return true;
}

void handleDoubleTap() {
    const esp_err_t result = DeviceConfig::instance().cycleSpeedDisplayMode();
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Double-tap view change failed: %s", esp_err_to_name(result));
        return;
    }

    const DeviceSettings settings = DeviceConfig::instance().snapshot();
    const char *view = "V1";
    switch (settings.speedDisplayMode) {
        case SpeedDisplayMode::CurrentPrimary: view = "V1"; break;
        case SpeedDisplayMode::LimitPrimary: view = "V2"; break;
        case SpeedDisplayMode::NoNavigation: view = "V3"; break;
    }
    ESP_LOGI(kTag, "DOUBLE-TAP -> %s", view);
}

void touchTask(void *) {
    ESP_LOGI(kTag, "Touch active: vertical swipe adjusts brightness; double-tap cycles V1/V2/V3");
    ESP_LOGI(kTag, "XPT2046 SPI3: SCK=25 MOSI=32 MISO=39 CS=33 IRQ=36");

    for (;;) {
        uint16_t startX = 0, startY = 0;
        if (!readTouchSample(&startX, &startY)) {
            vTaskDelay(kPollDelay);
            continue;
        }

        // Capture both raw axes so straight vertical gestures also work on
        // panels whose X/Y axes are rotated relative to the display.
        uint16_t endX = startX;
        uint16_t endY = startY;
        captureGesture(startX, startY, endX, endY);
        if (handleVerticalSwipe(startX, startY, endX, endY)) {
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        // A short tap is eligible for the existing double-tap view switch.
        const TickType_t secondTapDeadline = xTaskGetTickCount() + kDoubleTapWindow;
        if (!waitForPressUntil(secondTapDeadline)) continue;
        vTaskDelay(kDebounceDelay);
        waitForRelease();
        handleDoubleTap();
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

}  // namespace

void startTouchController() {
    ESP_LOGI(kTag, "Starting XPT2046 touch controller on SPI3");

    spi_bus_config_t bus{};
    bus.sclk_io_num = kTouchSck;
    bus.mosi_io_num = kTouchMosi;
    bus.miso_io_num = kTouchMiso;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 16;

    esp_err_t err = spi_bus_initialize(kTouchHost, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "Touch SPI bus init failed: %s", esp_err_to_name(err));
        return;
    }

    spi_device_interface_config_t config{};
    config.clock_speed_hz = 2000000;
    config.mode = 0;
    config.spics_io_num = kTouchCs;
    config.queue_size = 1;
    err = spi_bus_add_device(kTouchHost, &config, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Touch SPI device add failed: %s", esp_err_to_name(err));
        return;
    }

    gpio_config_t irq{};
    irq.pin_bit_mask = 1ULL << static_cast<unsigned>(kTouchIrq);
    irq.mode = GPIO_MODE_INPUT;
    irq.pull_up_en = GPIO_PULLUP_DISABLE;
    irq.pull_down_en = GPIO_PULLDOWN_DISABLE;
    irq.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&irq) != ESP_OK) {
        ESP_LOGE(kTag, "Touch IRQ setup failed");
        return;
    }

    const BaseType_t task = xTaskCreatePinnedToCore(touchTask, "touch_ctrl", 3072,
                                                     nullptr, 2, nullptr, 1);
    if (task != pdPASS) {
        ESP_LOGE(kTag, "Touch controller task creation failed");
    }
}

}  // namespace waze_hud
