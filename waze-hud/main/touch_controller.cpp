#include "touch_controller.h"

#include "config/device_config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>

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

// Two distinct taps must occur within this interval.
constexpr TickType_t kDoubleTapWindow = pdMS_TO_TICKS(350);
constexpr TickType_t kDebounceDelay = pdMS_TO_TICKS(35);
constexpr TickType_t kPollDelay = pdMS_TO_TICKS(20);
constexpr int kMinPressure = 40;

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

bool readTouchSample() {
    if (gpio_get_level(kTouchIrq) != 0) return false;
    const uint16_t x = read12(0xD0);
    const uint16_t y = read12(0x90);
    const uint16_t z1 = read12(0xB0);
    (void)read12(0xC0);  // z2 is not needed; IRQ + z1 are sufficient here.
    return z1 > kMinPressure && x > 20 && x < 4070 && y > 20 && y < 4070;
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

bool waitForPressUntil(TickType_t deadline) {
    while (xTaskGetTickCount() < deadline) {
        if (readTouchSample()) return true;
        vTaskDelay(kPollDelay);
    }
    return false;
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
    ESP_LOGI(kTag, "Touch controller active: double-tap cycles V1 -> V2 -> V3 -> V1");
    ESP_LOGI(kTag, "XPT2046 SPI3: SCK=25 MOSI=32 MISO=39 CS=33 IRQ=36");

    for (;;) {
        if (!readTouchSample()) {
            vTaskDelay(kPollDelay);
            continue;
        }

        // First tap: wait until the finger/stylus is released before looking
        // for the second tap. Holding the screen therefore cannot generate
        // repeated view changes.
        vTaskDelay(kDebounceDelay);
        waitForRelease();

        const TickType_t secondTapDeadline = xTaskGetTickCount() + kDoubleTapWindow;
        if (!waitForPressUntil(secondTapDeadline)) {
            continue;  // Single tap: intentionally ignored.
        }

        vTaskDelay(kDebounceDelay);
        waitForRelease();
        handleDoubleTap();

        // Small post-release guard prevents one physical double tap from
        // being interpreted as another tap because of contact bounce.
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
