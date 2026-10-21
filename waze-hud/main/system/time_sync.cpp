#include "system/time_sync.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "state/hud_state_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

namespace waze_hud {
namespace {
constexpr char kTag[] = "TIME_SYNC";
#if CONFIG_WAZE_HUD_WIFI_NTP
EventGroupHandle_t wifiEvents = nullptr;
constexpr EventBits_t kRetryBit = BIT1;
bool wifiInitialized = false;
bool sntpInitialized = false;

void onTimeSynchronized(struct timeval *) {
    HudStateStore::instance().refresh();
}

void wifiEventHandler(void *, esp_event_base_t base, int32_t id, void *) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifiEvents) xEventGroupSetBits(wifiEvents, kRetryBit);
        ESP_LOGW(kTag, "Wi-Fi disconnected; system clock continues while powered");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(kTag, "Wi-Fi connected; enabling NTP synchronization");
        if (sntpInitialized) {
            esp_sntp_restart();
        } else {
            esp_sntp_set_time_sync_notification_cb(&onTimeSynchronized);
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, const_cast<char *>("pool.ntp.org"));
            esp_sntp_setservername(1, const_cast<char *>("time.google.com"));
            esp_sntp_init();
            sntpInitialized = true;
        }
    }
}

void wifiReconnectTask(void *) {
    for (;;) {
        const EventBits_t bits = xEventGroupWaitBits(wifiEvents, kRetryBit,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
        if (bits & kRetryBit) {
            const esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_CONN)
                ESP_LOGW(kTag, "Wi-Fi reconnect request failed: %s", esp_err_to_name(err));
        }
    }
}
#endif
}  // namespace

esp_err_t startTimeSync() {
    // Local time rendering uses epoch seconds plus UTC+7; no large timezone library is needed.
#if CONFIG_WAZE_HUD_WIFI_NTP
    if (std::strlen(CONFIG_WAZE_HUD_WIFI_SSID) == 0) {
        ESP_LOGW(kTag, "Wi-Fi SSID is empty; calendar will use navigation time or a valid system clock");
        return ESP_OK;
    }
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    if (wifiInitialized) return ESP_OK;
    if (!esp_netif_create_default_wifi_sta()) return ESP_FAIL;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), kTag, "Wi-Fi init failed");
    wifiEvents = xEventGroupCreate();
    if (!wifiEvents) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifiEventHandler, nullptr), kTag, "Wi-Fi event registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
        &wifiEventHandler, nullptr), kTag, "IP event registration failed");
    wifi_config_t wifiConfig{};
    std::snprintf(reinterpret_cast<char *>(wifiConfig.sta.ssid), sizeof(wifiConfig.sta.ssid), "%s", CONFIG_WAZE_HUD_WIFI_SSID);
    std::snprintf(reinterpret_cast<char *>(wifiConfig.sta.password), sizeof(wifiConfig.sta.password), "%s", CONFIG_WAZE_HUD_WIFI_PASSWORD);
    wifiConfig.sta.threshold.authmode = std::strlen(CONFIG_WAZE_HUD_WIFI_PASSWORD) == 0
        ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "Wi-Fi STA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig), kTag, "Wi-Fi credentials rejected");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "Wi-Fi start failed");
    wifiInitialized = true;
    if (xTaskCreate(wifiReconnectTask, "wifi_reconnect", 3072, nullptr, 3, nullptr) != pdPASS)
        ESP_LOGW(kTag, "Could not create Wi-Fi reconnect task");
    return esp_wifi_connect();
#else
    ESP_LOGI(kTag, "Wi-Fi/NTP disabled; calendar uses navigation time or a valid system clock");
    return ESP_OK;
#endif
}
}  // namespace waze_hud
