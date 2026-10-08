#pragma once

#include "sdkconfig.h"
#include <cstdint>

namespace waze_hud {

struct Rect {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
};

namespace layout {
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
constexpr bool IsLargeDisplay = true;
constexpr const char *DeviceName = "ESP32-S3 3.5-inch HUD";
constexpr int UiYScaleNumerator = 5;
constexpr int UiYScaleDenominator = 4;
constexpr int Width = 320;
constexpr int Height = 213;
constexpr int PhysicalWidth = 480;
constexpr int PhysicalHeight = 320;
// 173 logical rows scale to exactly 260 physical rows. This keeps the
// main/street boundary four-pixel aligned after landscape pixels are rotated
// into the ST77922 native 320x480 address space.
constexpr int MainHeight = 173;
constexpr int GuidanceHeight = 0;
constexpr int StreetHeight = Height - MainHeight;
#elif CONFIG_WAZE_HUD_DISPLAY_CYD_28
constexpr bool IsLargeDisplay = false;
constexpr const char *DeviceName = "ESP32-2432S028 2.8-inch HUD";
// Use a native 320x240 canvas. Only layout positions are spread vertically;
// bitmap assets and fonts remain square, unscaled pixels.
constexpr int UiYScaleNumerator = 7;
constexpr int UiYScaleDenominator = 5;
constexpr int Width = 320;
constexpr int Height = 240;
constexpr int PhysicalWidth = 320;
constexpr int PhysicalHeight = 240;
constexpr int MainHeight = 146;
constexpr int GuidanceHeight = 50;
constexpr int StreetHeight = Height - MainHeight - GuidanceHeight;
#else
constexpr bool IsLargeDisplay = false;
constexpr const char *DeviceName = "LILYGO T-Display-S3";
constexpr int UiYScaleNumerator = 1;
constexpr int UiYScaleDenominator = 1;
constexpr int Width = 320;
constexpr int Height = 170;
constexpr int PhysicalWidth = 320;
constexpr int PhysicalHeight = 170;
constexpr int MainHeight = 140;
constexpr int GuidanceHeight = 0;
constexpr int StreetHeight = Height - MainHeight;
#endif

constexpr Rect Maneuver{0, 0, 80, MainHeight};
constexpr Rect Speed{80, 0, 80, MainHeight};
constexpr Rect SpeedCluster{80, 0, 160, MainHeight};
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
// Landscape layout keeps the 3.5-inch variant unchanged.
constexpr Rect Limits{165, 0, 59, MainHeight};
constexpr Rect Alerts{224, 0, 96, MainHeight};
#else
// CYD 2.8: four equal 80px columns across the 320px framebuffer.
constexpr Rect Limits{160, 0, 80, MainHeight};
constexpr Rect Alerts{240, 0, 80, MainHeight};
#endif
constexpr Rect Guidance{0, MainHeight, Width, GuidanceHeight};
constexpr Rect Street{0, MainHeight + GuidanceHeight, Width, StreetHeight};
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
// V2 renderer regions; only used by the third display mode.
// Keep V2's original CYD geometry independent from the V1 geometry above.
// V2 was designed as: 30px header + 120px main + 65px guidance + 25px street = 240px.
// Do not derive these from V1 MainHeight/GuidanceHeight, because V1 1.1.0
// intentionally uses a different vertical split.
constexpr int V2MainHeight = 120;
constexpr int V2GuidanceHeight = 65;
constexpr int V2StreetHeight = 25;
constexpr int V1NextStreetHeight = 30;
// V1: reserve the top 30px of all three main columns for the next-street header.
// The V1 content regions are clipped to start at Y=30 while their existing
// drawing coordinates are preserved via a renderer translation.
constexpr Rect V1NextStreet{0, 0, 240, V1NextStreetHeight};
constexpr Rect V1Maneuver{0, V1NextStreetHeight, 80, MainHeight - V1NextStreetHeight};
constexpr Rect V1Speed{80, V1NextStreetHeight, 80, MainHeight - V1NextStreetHeight};
constexpr Rect V1Limits{160, V1NextStreetHeight, 80, MainHeight - V1NextStreetHeight};
constexpr Rect V1SpeedCluster{80, V1NextStreetHeight, 160, MainHeight - V1NextStreetHeight};
constexpr Rect V2NextStreet{0, 0, Width, 30};
constexpr Rect V2Maneuver{0, 30, 120, V2MainHeight};
constexpr Rect V2SpeedCluster{120, 30, 200, V2MainHeight};
constexpr Rect V2Alerts{240, 150, 80, 90};
constexpr Rect V2Guidance{0, 150, 240, V2GuidanceHeight};
constexpr Rect V2Street{0, 215, 240, V2StreetHeight};
#endif
constexpr Rect Full{0, 0, Width, Height};

constexpr int scaleCoordinate(int value, int logicalExtent, int physicalExtent) {
    return (value * physicalExtent + logicalExtent / 2) / logicalExtent;
}

constexpr Rect physicalRect(const Rect &logical) {
    const int x0 = scaleCoordinate(logical.x, Width, PhysicalWidth);
    const int y0 = scaleCoordinate(logical.y, Height, PhysicalHeight);
    const int x1 = scaleCoordinate(logical.x + logical.width, Width, PhysicalWidth);
    const int y1 = scaleCoordinate(logical.y + logical.height, Height, PhysicalHeight);
    return {static_cast<int16_t>(x0), static_cast<int16_t>(y0),
            static_cast<int16_t>(x1 - x0), static_cast<int16_t>(y1 - y0)};
}

constexpr int regionPixels(const Rect &logical) {
    const Rect physical = physicalRect(logical);
    return physical.width * physical.height;
}

constexpr int maxInt(int left, int right) { return left > right ? left : right; }
constexpr int BaseMaxRegionPixels = maxInt(
    maxInt(maxInt(regionPixels(Maneuver), regionPixels(Speed)), regionPixels(SpeedCluster)),
    maxInt(maxInt(regionPixels(Limits), regionPixels(Alerts)),
           maxInt(regionPixels(Guidance), regionPixels(Street))));
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
constexpr int V2MaxRegionPixels = maxInt(
    maxInt(maxInt(maxInt(regionPixels(V1NextStreet), regionPixels(V1Maneuver)), regionPixels(V1Speed)), regionPixels(V1Limits)),
    maxInt(regionPixels(V2SpeedCluster),
           maxInt(regionPixels(V2Guidance), maxInt(regionPixels(V2Street), regionPixels(V2Alerts)))));
constexpr int MaxRegionPixels = maxInt(BaseMaxRegionPixels, V2MaxRegionPixels);
#else
constexpr int MaxRegionPixels = BaseMaxRegionPixels;
#endif

static_assert(physicalRect(Full).x == 0 && physicalRect(Full).y == 0,
              "Display viewport must start at the framebuffer origin");
static_assert(physicalRect(Full).width == PhysicalWidth &&
              physicalRect(Full).height == PhysicalHeight,
              "Display viewport must cover the complete framebuffer");
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
static_assert(physicalRect(V1NextStreet).x == 0 &&
              physicalRect(V1NextStreet).y == 0 &&
              physicalRect(V1NextStreet).width == 240 &&
              physicalRect(V1NextStreet).height == V1NextStreetHeight,
              "V1 next-street header geometry is invalid");
static_assert(physicalRect(V1Maneuver).x == 0 &&
              physicalRect(V1Maneuver).y == V1NextStreetHeight &&
              physicalRect(V1Maneuver).width == 80 &&
              physicalRect(V1Maneuver).height == MainHeight - V1NextStreetHeight,
              "V1 maneuver region geometry is invalid");
static_assert(physicalRect(V1Speed).x == 80 &&
              physicalRect(V1Speed).y == V1NextStreetHeight &&
              physicalRect(V1Speed).width == 80 &&
              physicalRect(V1Speed).height == MainHeight - V1NextStreetHeight,
              "V1 speed region geometry is invalid");
static_assert(physicalRect(V1Limits).x == 160 &&
              physicalRect(V1Limits).y == V1NextStreetHeight &&
              physicalRect(V1Limits).width == 80 &&
              physicalRect(V1Limits).height == MainHeight - V1NextStreetHeight,
              "V1 limits region geometry is invalid");
static_assert(physicalRect(V1SpeedCluster).x == 80 &&
              physicalRect(V1SpeedCluster).y == V1NextStreetHeight &&
              physicalRect(V1SpeedCluster).width == 160 &&
              physicalRect(V1SpeedCluster).height == MainHeight - V1NextStreetHeight,
              "V1 speed-cluster region geometry is invalid");
static_assert(physicalRect(V2NextStreet).y == 0 &&
              physicalRect(V2NextStreet).height == 30,
              "V2 header must occupy the top 30px");
static_assert(physicalRect(V2Maneuver).y == 30 &&
              physicalRect(V2Maneuver).height == V2MainHeight &&
              physicalRect(V2SpeedCluster).y == 30 &&
              physicalRect(V2SpeedCluster).height == V2MainHeight,
              "V2 main row geometry is invalid");
static_assert(physicalRect(V2Guidance).y == 150 &&
              physicalRect(V2Guidance).height == V2GuidanceHeight &&
              physicalRect(V2Alerts).y == 150 &&
              physicalRect(V2Alerts).height == 90,
              "V2 lower guidance/alert row geometry is invalid");
static_assert(physicalRect(V2Street).y == 215 &&
              physicalRect(V2Street).height == V2StreetHeight &&
              physicalRect(V2Street).y + physicalRect(V2Street).height == PhysicalHeight,
              "V2 street row must end exactly at the bottom of the framebuffer");
#endif
static_assert(physicalRect(Maneuver).width + physicalRect(Speed).width +
              physicalRect(Limits).width + physicalRect(Alerts).width == PhysicalWidth,
              "HUD columns must cover the framebuffer without gaps");
static_assert(physicalRect(Maneuver).height + physicalRect(Guidance).height +
              physicalRect(Street).height == PhysicalHeight,
              "HUD rows must cover the framebuffer without gaps");
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
static_assert(physicalRect(Maneuver).x % 4 == 0 &&
              (physicalRect(Maneuver).x + physicalRect(Maneuver).width) % 4 == 0 &&
              physicalRect(Speed).x % 4 == 0 &&
              (physicalRect(Speed).x + physicalRect(Speed).width) % 4 == 0 &&
              physicalRect(Limits).x % 4 == 0 &&
              (physicalRect(Limits).x + physicalRect(Limits).width) % 4 == 0 &&
              physicalRect(Alerts).x % 4 == 0 &&
              (physicalRect(Alerts).x + physicalRect(Alerts).width) % 4 == 0,
              "Landscape dirty-region X coordinates must be four-pixel aligned");
static_assert(physicalRect(Maneuver).y % 4 == 0 &&
              (physicalRect(Maneuver).y + physicalRect(Maneuver).height) % 4 == 0 &&
              physicalRect(Street).y % 4 == 0 &&
              (physicalRect(Street).y + physicalRect(Street).height) % 4 == 0,
              "Rotated ST77922 native X boundaries must be four-pixel aligned");
#endif
}  // namespace layout

}  // namespace waze_hud
