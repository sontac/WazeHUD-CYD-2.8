#pragma once

#include "config/device_config.h"
#include "display/bitmap_font.h"
#include "display/layout.h"
#include "state/hud_state.h"
#include "system/system_status.h"
#include "esp_err.h"
#include <cstdint>

namespace waze_hud {

class HudRenderer {
public:
    esp_err_t init();
    void render(const HudState &state, const DeviceSettings &settings,
                const SystemStatusSnapshot &systemStatus);
    bool animationActive() const { return marqueeActive_ || nextStreetMarqueeActive_ || clockActive_ || overspeedBorderActive_; }

private:
    void renderRegion(const Rect &region, const HudState &state, const DeviceSettings &settings,
                      const SystemStatusSnapshot &systemStatus);
    void renderSystemStatus(Canvas &canvas, const Rect &region,
                            const SystemStatusSnapshot &systemStatus,
                            const DeviceSettings &settings);
    void renderMainIndicators(Canvas &canvas, const Rect &region,
                              const SystemStatusSnapshot &systemStatus);
    void renderStatus(Canvas &canvas, const Rect &region, const HudState &state, const DeviceSettings &settings);
    void renderManeuver(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderSpeed(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderLimitPrimary(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderLimits(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderAlerts(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderGuidance(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderStreet(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderV1NextStreet(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderV2NextStreet(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderV2Maneuver(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderV2SpeedCluster(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderV2Alerts(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderV2Guidance(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderV2Street(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderOverspeedBorder(const HudState &state, const DeviceSettings &settings, bool forceRedraw);
    void drawBorderEdge(const Rect &edge, uint16_t color);

    uint16_t *buffer_{nullptr};
    HudState previous_{};
    DeviceSettings previousSettings_{};
    SystemStatusSnapshot previousSystemStatus_{};
    int64_t renderedClockMinute_{INT64_MIN};
    int8_t renderedClockPhase_{-1};
    uint64_t marqueeEpochMs_{0};
    int marqueeOffset_{0};
    int marqueeRenderedOffset_{-1};
    int marqueeTextWidth_{0};
    int marqueeAvailableWidth_{0};
    bool marqueeActive_{false};
    uint64_t nextStreetMarqueeEpochMs_{0};
    int nextStreetMarqueeOffset_{0};
    int nextStreetMarqueeRenderedOffset_{-1};
    int nextStreetMarqueeTextWidth_{0};
    int nextStreetMarqueeAvailableWidth_{0};
    bool nextStreetMarqueeActive_{false};
    bool clockActive_{false};
    bool overspeedBorderActive_{false};
    uint8_t previousBorderMask_{0};
    bool firstFrame_{true};
};

}  // namespace waze_hud
