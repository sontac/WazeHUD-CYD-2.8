#include "display/hud_renderer.h"

#include "display/colors.h"
#include "display/display_driver.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace waze_hud {
namespace {
constexpr char kTag[] = "DISPLAY";

constexpr int mainY(int value) {
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    return value;
#else
    return value * layout::UiYScaleNumerator / layout::UiYScaleDenominator;
#endif
}

constexpr int screenY(int value) {
    return value * layout::UiYScaleNumerator / layout::UiYScaleDenominator;
}

uint16_t foreground(const DeviceSettings &settings) {
    return settings.theme == UiTheme::Night ? colors::rgb565(220, 105, 80) : colors::Foreground;
}

template <typename Left, typename Right>
bool sameText(const Left &left, const Right &right) { return std::strcmp(left.data(), right.data()) == 0; }

bool maneuverChanged(const HudState &a, const HudState &b) {
    return a.maneuver != b.maneuver || a.secondManeuver != b.secondManeuver ||
           a.maneuverDistanceM != b.maneuverDistanceM ||
           a.roundaboutExit != b.roundaboutExit;
}

bool isRoundaboutManeuver(Maneuver maneuver) {
    return maneuver == Maneuver::Roundabout || maneuver == Maneuver::RoundaboutLeft ||
           maneuver == Maneuver::RoundaboutRight ||
           maneuver == Maneuver::RoundaboutStraight ||
           maneuver == Maneuver::RoundaboutUTurn;
}

bool alertsChanged(const HudState &a, const HudState &b) {
    if (!(a.nearestAlert == b.nearestAlert) || a.upcomingAlertCount != b.upcomingAlertCount ||
        a.noPassingZone != b.noPassingZone || a.noPassingRemainingM != b.noPassingRemainingM) return true;
    for (uint8_t i = 0; i < a.upcomingAlertCount; ++i)
        if (!(a.upcomingAlerts[i] == b.upcomingAlerts[i])) return true;
    return false;
}

bool guidanceChanged(const HudState &a, const HudState &b) {
    if (!sameText(a.eta, b.eta) ||
        a.remainingMeters != b.remainingMeters ||
        a.remainingMinutes != b.remainingMinutes ||
        a.laneCount != b.laneCount || alertsChanged(a, b))
        return true;
    for (uint8_t index = 0; index < a.laneCount; ++index)
        if (!(a.lanes[index] == b.lanes[index])) return true;
    return false;
}

bool hasSettingsChanged(const DeviceSettings &a, const DeviceSettings &b) {
    return a.brightness != b.brightness || a.theme != b.theme || a.showStreet != b.showStreet ||
           a.speedDisplayMode != b.speedDisplayMode ||
           a.mirrorHud != b.mirrorHud || a.rotateDisplay != b.rotateDisplay ||
           a.overspeedOffsetKmh != b.overspeedOffsetKmh ||
           a.speedOffsetKmh != b.speedOffsetKmh ||
           a.speedOffsetPercent != b.speedOffsetPercent ||
           a.offsetX != b.offsetX || a.offsetY != b.offsetY || a.revision != b.revision
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
           || a.autoBrightness != b.autoBrightness ||
           a.invertColor != b.invertColor ||
           a.colorBgr != b.colorBgr ||
           a.backlightPin != b.backlightPin ||
           a.overspeedBorder != b.overspeedBorder
#endif
           ;
}

bool firmwareOverspeed(const HudState &state, const DeviceSettings &settings) {
    if (state.speedLimitKmh <= 0) return false;
    const int threshold = std::max(0, state.speedLimitKmh +
                                     static_cast<int>(settings.overspeedOffsetKmh));
    return adjustedSpeed(state.speedKmh, settings) > threshold;
}

uint16_t alertDistanceColor(int distanceM, uint16_t normalColor) {
    // Alert distance labels are always white for consistent readability
    // across V1, V2 and V3, regardless of warning distance or text size.
    (void)distanceM;
    (void)normalColor;
    return colors::White;
}

uint16_t bleSignalColor(const SystemStatusSnapshot &status) {
    if (!status.bleConnected) return colors::Muted;
    if (status.bleRssiDbm >= -60) return colors::Green;
    if (status.bleRssiDbm >= -75) return colors::Blue;
    if (status.bleRssiDbm >= -85) return colors::Amber;
    return colors::Red;
}

int64_t localClockMillis(const HudState &state) {
    if (state.clockUnixSeconds <= 0) return INT64_MIN;
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    const uint64_t elapsedMs = nowMs >= state.clockSyncMonotonicMs
        ? nowMs - state.clockSyncMonotonicMs : 0U;
    return state.clockUnixSeconds * 1000LL + static_cast<int64_t>(elapsedMs) +
           static_cast<int64_t>(state.timezoneOffsetMinutes) * 60000LL;
}

const char *displayStreet(const HudState &state) {
    // Hiển thị tên đường
    if (state.currentStreet[0] != 0) return state.currentStreet.data();
    return "Cầu đường chưa đặt tên";
}

bool sameRegion(const Rect &left, const Rect &right) {
    return left.x == right.x && left.y == right.y && left.width == right.width &&
           left.height == right.height;
}

void arrowHead(Canvas &canvas, int x, int y, int dx, int dy, uint16_t color, int thickness) {
    if (std::abs(dx) >= std::abs(dy)) {
        const int sign = dx >= 0 ? 1 : -1;
        canvas.line(x, y, x - sign * 9, y - 7, color, thickness);
        canvas.line(x, y, x - sign * 9, y + 7, color, thickness);
    } else {
        const int sign = dy >= 0 ? 1 : -1;
        canvas.line(x, y, x - 7, y - sign * 9, color, thickness);
        canvas.line(x, y, x + 7, y - sign * 9, color, thickness);
    }
}

const assets::AlphaMask *laneArrowHeadForBit(int bit) {
    switch (bit) {
        case 0: return &assets::kLaneArrowHeadUp;
        case 1: return &assets::kLaneArrowHeadUpLeft;
        case 2: return &assets::kLaneArrowHeadLeft;
        case 3: return &assets::kLaneArrowHeadDownLeft;
        case 4: return &assets::kLaneArrowHeadUpRight;
        case 5: return &assets::kLaneArrowHeadRight;
        case 6: return &assets::kLaneArrowHeadDownRight;
        case 7: return &assets::kLaneArrowHeadDown;
        default: return nullptr;
    }
}

void laneArrowHead(Canvas &canvas, int bit, int x, int y, uint16_t color) {
    const assets::AlphaMask *head = laneArrowHeadForBit(bit);
    if (!head) return;
    canvas.alphaMask(x - static_cast<int>(head->width) / 2,
                     y - static_cast<int>(head->height) / 2, *head, color);
}

void drawGuidanceLane(Canvas &canvas, int x, int spacing, const LaneState &lane,
                      uint16_t foregroundColor) {
    constexpr int baseline = 40;
    constexpr int junction = 29;
    const bool selectedLane = lane.selectedMask != 0;
    const uint16_t stemColor = selectedLane ? foregroundColor : colors::Muted;
    const int stemThickness = selectedLane ? 3 : 2;
    canvas.line(x, baseline, x, junction, stemColor, stemThickness);
    if (selectedLane)
        canvas.fillRect(x - spacing / 2 + 2, 47, std::max(2, spacing - 4), 2,
                        colors::Green);

    // v12.9: use one consistent geometry for every lane direction.  The
    // branch endpoint is kept far enough from the lane center that the 12x12
    // arrow-head masks never sit on top of the main vertical stem.
    const int branch = std::max(8, std::min(10, spacing / 2 - 2));
    constexpr int elbowY = 23;
    constexpr int upTipY = 14;
    constexpr int sideTipY = 23;
    constexpr int downTipY = 34;

    for (int bit = 0; bit < 8; ++bit) {
        const uint8_t flag = static_cast<uint8_t>(1U << bit);
        if ((lane.directionMask & flag) == 0) continue;

        const bool selectedDirection = (lane.selectedMask & flag) != 0;
        const uint16_t color = selectedDirection ? foregroundColor : colors::Muted;
        const int thickness = selectedDirection ? 3 : 2;

        switch (bit) {
            case 0: // straight
                canvas.line(x, junction, x, upTipY, color, thickness);
                laneArrowHead(canvas, bit, x, upTipY, color);
                break;

            case 1: // up-left: clean 90-degree elbow + diagonal head
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x - branch, upTipY, color, thickness);
                laneArrowHead(canvas, bit, x - branch, upTipY, color);
                break;

            case 2: // left: true horizontal branch
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x - branch, sideTipY, color, thickness);
                laneArrowHead(canvas, bit, x - branch, sideTipY, color);
                break;

            case 3: // down-left
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x - branch, downTipY, color, thickness);
                laneArrowHead(canvas, bit, x - branch, downTipY, color);
                break;

            case 4: // up-right
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x + branch, upTipY, color, thickness);
                laneArrowHead(canvas, bit, x + branch, upTipY, color);
                break;

            case 5: // right: true horizontal branch
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x + branch, sideTipY, color, thickness);
                laneArrowHead(canvas, bit, x + branch, sideTipY, color);
                break;

            case 6: // down-right
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x + branch, downTipY, color, thickness);
                laneArrowHead(canvas, bit, x + branch, downTipY, color);
                break;

            case 7: { // U-turn: wider elbow, then downward arrow
                // Keep the U-turn head outside the central stem.  The old
                // 6-7 px elbow put the 12x12 down-arrow mask over the stem.
                const int uTurnX = x - branch;
                constexpr int uTurnY = 20;
                constexpr int uTurnTipY = 31;
                canvas.line(x, junction, uTurnX, uTurnY, color, thickness);
                canvas.line(uTurnX, uTurnY, uTurnX, uTurnTipY, color, thickness);
                laneArrowHead(canvas, bit, uTurnX, uTurnTipY, color);
                break;
            }
        }
    }
}

const assets::AlphaMask *maneuverAsset(Maneuver maneuver) {
    switch (maneuver) {
        case Maneuver::Continue: return &assets::kManeuverContinue;
        case Maneuver::Left: return &assets::kManeuverLeft;
        case Maneuver::Right: return &assets::kManeuverRight;
        case Maneuver::UTurn: return &assets::kManeuverUTurn;
        case Maneuver::Roundabout: return &assets::kManeuverRoundabout;
        case Maneuver::RoundaboutLeft: return &assets::kManeuverRoundaboutLeft;
        case Maneuver::RoundaboutRight: return &assets::kManeuverRoundaboutRight;
        case Maneuver::RoundaboutStraight: return &assets::kManeuverRoundaboutStraight;
        case Maneuver::RoundaboutUTurn: return &assets::kManeuverRoundaboutUTurn;
        case Maneuver::KeepLeft: return &assets::kManeuverExitLeft;
        case Maneuver::KeepRight: return &assets::kManeuverExitRight;
        case Maneuver::ExitLeft: return &assets::kManeuverExitLeft;
        case Maneuver::ExitRight: return &assets::kManeuverExitRight;
        case Maneuver::Arrive: return &assets::kManeuverArrive;
        default: return nullptr;
    }
}

void drawRoundaboutExit(Canvas &canvas, int exit, uint16_t color) {
    if (exit <= 0) return;
    char number[12];
    std::snprintf(number,sizeof(number),"%d",exit);
    constexpr int centerX = 42;
    constexpr int width = 36;
    canvas.fontText(centerX-width/2,mainY(65)-assets::kNumberMedium.lineHeight/2,
                    number,assets::kNumberMedium,color,width,true);
}

enum class SpeedSignContext { Current, AlertLarge, AlertSmall };

const assets::ColorBitmap *speedLimitAsset(int value, SpeedSignContext context) {
    for (std::size_t index = 0; index < assets::kSpeedLimitAssetCount; ++index) {
        const assets::SpeedLimitAssetSet &entry = assets::kSpeedLimitAssets[index];
        if (entry.value != value) continue;
        switch (context) {
            case SpeedSignContext::Current: return entry.current;
            case SpeedSignContext::AlertLarge: return entry.alertLarge;
            case SpeedSignContext::AlertSmall: return entry.alertSmall;
        }
    }
    return nullptr;
}

void drawManeuverIcon(Canvas &canvas, Maneuver maneuver, int exit, uint16_t color) {
    constexpr int cx = 40;
    const int top = mainY(38);
    const int bottom = mainY(92);
    const int thick = 5;
    if (maneuver == Maneuver::None) return;
    if (const assets::AlphaMask *asset = maneuverAsset(maneuver)) {
        canvas.alphaMask(10, mainY(34), *asset, color);
        if (isRoundaboutManeuver(maneuver) && exit > 0) {
            drawRoundaboutExit(canvas,exit,color);
        }
        return;
    }
    if (maneuver == Maneuver::Arrive) {
        canvas.line(cx - 18, top + 7, cx - 18, bottom - 3, color, 3);
        canvas.fillRect(cx - 15, top + 8, 28, 18, color);
        canvas.fillRect(cx - 10, top + 13, 5, 5, colors::Background);
        canvas.fillRect(cx, top + 13, 5, 5, colors::Background);
        return;
    }
    if (isRoundaboutManeuver(maneuver)) {
        canvas.circle(cx, mainY(65), 20, color, 4);
        canvas.line(cx, bottom, cx, mainY(83), color, thick);
        if (maneuver == Maneuver::RoundaboutStraight) {
            canvas.line(cx, mainY(45), cx, top, color, thick);
            arrowHead(canvas, cx, top, 0, -1, color, 3);
            drawRoundaboutExit(canvas,exit,color);
            return;
        }
        if (maneuver == Maneuver::RoundaboutUTurn) {
            const int endX = cx - 27;
            canvas.line(cx - 19, mainY(65), endX, mainY(65), color, thick);
            canvas.line(endX, mainY(65), endX, mainY(78), color, thick);
            arrowHead(canvas, endX, mainY(78), 0, 1, color, 3);
            drawRoundaboutExit(canvas,exit,color);
            return;
        }
        const bool left = maneuver == Maneuver::RoundaboutLeft;
        const int endX = left ? cx - 27 : cx + 27;
        canvas.line(left ? cx - 19 : cx + 19, mainY(65), endX, mainY(65), color, thick);
        arrowHead(canvas, endX, mainY(65), left ? -1 : 1, 0, color, 3);
        drawRoundaboutExit(canvas,exit,color);
        return;
    }
    if (maneuver == Maneuver::UTurn || maneuver == Maneuver::UTurnRightReserved) {
        const bool right = maneuver == Maneuver::UTurnRightReserved;
        const int side = right ? 1 : -1;
        canvas.line(cx, bottom, cx, mainY(55), color, thick);
        canvas.line(cx, mainY(55), cx + side * 16, mainY(43), color, thick);
        canvas.line(cx + side * 16, mainY(43), cx + side * 27, mainY(55), color, thick);
        canvas.line(cx + side * 27, mainY(55), cx + side * 27, mainY(68), color, thick);
        arrowHead(canvas, cx + side * 27, mainY(68), 0, 1, color, 3);
        return;
    }

    int endX = cx, endY = top;
    switch (maneuver) {
        case Maneuver::Left: endX = 15; endY = mainY(53); break;
        case Maneuver::Right: endX = 69; endY = mainY(53); break;
        case Maneuver::SlightLeft: case Maneuver::KeepLeft: endX = 21; endY = mainY(42); break;
        case Maneuver::SlightRight: case Maneuver::KeepRight: endX = 63; endY = mainY(42); break;
        case Maneuver::SharpLeft: case Maneuver::ExitLeft: endX = 14; endY = mainY(73); break;
        case Maneuver::SharpRight: case Maneuver::ExitRight: endX = 70; endY = mainY(73); break;
        default: break;
    }
    canvas.line(cx, bottom, cx, mainY(69), color, thick);
    canvas.line(cx, mainY(69), endX, endY, color, thick);
    arrowHead(canvas, endX, endY, endX - cx, endY - mainY(69), color, 3);
}

const assets::ColorBitmap *alertAsset(AlertKind kind, bool dominant) {
    const uint8_t code = static_cast<uint8_t>(kind);
    for (std::size_t index = 0; index < assets::kAlertAssetCount; ++index) {
        const assets::AlertAssetSet &entry = assets::kAlertAssets[index];
        if (entry.code == code) return dominant ? entry.large : entry.small;
    }
    return nullptr;
}

uint16_t trafficSeverityColor(uint8_t severity) {
    switch (severity) {
        case 1: return colors::Green;
        case 2: return colors::Amber;
        case 3: return colors::rgb565(255, 112, 24);
        case 4: case 5: return colors::Red;
        default: return colors::Muted;
    }
}

const assets::ColorBitmap *trafficJamAsset(uint8_t severity, bool dominant) {
    switch (severity) {
        case 1: return dominant ? &assets::kAlertTrafficJam1Large
                                : &assets::kAlertTrafficJam1Small;
        case 2: return dominant ? &assets::kAlertTrafficJam2Large
                                : &assets::kAlertTrafficJam2Small;
        case 4: case 5: return dominant ? &assets::kAlertTrafficJam4Large
                                        : &assets::kAlertTrafficJam4Small;
        default: return alertAsset(AlertKind::TrafficJam, dominant);
    }
}

const char *trafficSeverityLabel(uint8_t severity) {
    switch (severity) {
        case 1: return "NHẸ";
        case 2: return "VỪA";
        case 3: return "NẶNG";
        case 4: return "ĐỨNG IM";
        case 5: return "ĐƯỜNG ĐÓNG";
        default: return "KẸT XE";
    }
}

void drawTrafficSeverityTicks(Canvas &canvas, int centerX, int y,
                              uint8_t severity, uint16_t color) {
    if (severity == 0) return;
    constexpr int tickWidth = 4;
    constexpr int gap = 2;
    constexpr int totalWidth = 5 * tickWidth + 4 * gap;
    const int startX = centerX - totalWidth / 2;
    for (int tick = 0; tick < 5; ++tick)
        canvas.fillRect(startX + tick * (tickWidth + gap), y, tickWidth, 3,
                        tick < severity ? color : colors::Muted);
}

void drawAlertIcon(Canvas &canvas, int cx, int cy, int radius, const AlertState &alert, bool dominant) {
    if (alert.kind == AlertKind::None) return;
    if (alert.kind == AlertKind::SpeedDrop) {
        const assets::ColorBitmap *sign = speedLimitAsset(
            alert.valueKmh, dominant ? SpeedSignContext::AlertLarge : SpeedSignContext::AlertSmall);
        if (sign && sign->pixels && sign->alpha) {
            canvas.colorBitmap(cx - sign->width / 2, cy - sign->height / 2, *sign);
            return;
        }
        const int thick = dominant ? 3 : 2;
        char value[5]{};
        canvas.fillCircle(cx,cy,radius,colors::White);
        canvas.circle(cx,cy,radius,colors::Red,thick);
        std::snprintf(value,sizeof(value),"%d",alert.valueKmh);
        if (dominant)
            canvas.fontText(cx-radius,cy-assets::kNumberMedium.lineHeight/2,value,
                            assets::kNumberMedium,colors::Black,2*radius,true);
        else
            canvas.fontText(cx-radius,cy-assets::kNumberSmall.lineHeight/2,value,
                            assets::kNumberSmall,colors::Black,2*radius,true);
        return;
    }

    const bool trafficJam = alert.kind == AlertKind::TrafficJam;
    const uint16_t severityColor = trafficSeverityColor(alert.trafficSeverity);
    if (trafficJam && alert.trafficSeverity > 0)
        canvas.circle(cx, cy, radius + (dominant ? 3 : 2), severityColor,
                      dominant ? 2 : 1);
    const assets::ColorBitmap *bitmap = trafficJam
        ? trafficJamAsset(alert.trafficSeverity, dominant) : alertAsset(alert.kind, dominant);
    if (!bitmap) bitmap = alertAsset(AlertKind::Hazard, dominant);
    if (bitmap && bitmap->pixels && bitmap->alpha) {
        canvas.colorBitmap(cx - bitmap->width / 2, cy - bitmap->height / 2, *bitmap);
        if (trafficJam && !dominant)
            drawTrafficSeverityTicks(canvas, cx, cy + radius + 3,
                                     alert.trafficSeverity, severityColor);
        return;
    }

    // Last-resort primitive only when the generated hazard asset is absent.
    canvas.triangle(cx,cy-radius,cx-radius,cy+radius,cx+radius,cy+radius,colors::Amber);
    canvas.fontText(cx-radius,cy-assets::kTextMedium.lineHeight/2,"!",assets::kTextMedium,
                    colors::Amber,2*radius,true);
}

void formatDistance(int meters, char *output, size_t capacity) {
    if (meters < 0) { output[0] = 0; return; }
    if (meters < 1000) std::snprintf(output, capacity, "%d M", meters);
    else if (meters < 10000) std::snprintf(output, capacity, "%.1f KM", meters / 1000.0);
    else std::snprintf(output, capacity, "%d KM", (meters + 500) / 1000);
}

#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
const assets::ColorBitmap *speedLimitCurrentRingAsset(int value) {
    switch (value) {
        case 10: return &assets::kSpeedLimit10CurrentRing;
        case 20: return &assets::kSpeedLimit20CurrentRing;
        case 30: return &assets::kSpeedLimit30CurrentRing;
        case 40: return &assets::kSpeedLimit40CurrentRing;
        case 50: return &assets::kSpeedLimit50CurrentRing;
        case 60: return &assets::kSpeedLimit60CurrentRing;
        case 70: return &assets::kSpeedLimit70CurrentRing;
        case 80: return &assets::kSpeedLimit80CurrentRing;
        case 90: return &assets::kSpeedLimit90CurrentRing;
        case 100: return &assets::kSpeedLimit100CurrentRing;
        case 110: return &assets::kSpeedLimit110CurrentRing;
        case 120: return &assets::kSpeedLimit120CurrentRing;
        default: return nullptr;
    }
}
#endif
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
// UI NEW-07: keep distance rendering independent from punctuation glyphs.
// The compact number bitmaps intentionally contain only digits; drawing the
// decimal comma as a primitive avoids the fontGlyph() fallback to '?'.
void drawDistanceCyd(Canvas &canvas, int centerX, int baselineY, int meters,
                     const assets::BitmapFont &numberFont, const assets::BitmapFont &unitFont,
                     uint16_t color, int maxWidth) {
    if (meters < 0) return;

    char integerPart[12];
    char decimalPart[4] = {};
    const char *unit = meters < 1000 ? "m" : "km";
    bool hasDecimal = false;
    if (meters < 1000) {
        std::snprintf(integerPart, sizeof(integerPart), "%d", meters);
    } else if (meters < 10000) {
        const int whole = meters / 1000;
        const int tenth = (meters % 1000 + 50) / 100;
        if (tenth >= 10) {
            std::snprintf(integerPart, sizeof(integerPart), "%d", whole + 1);
        } else {
            std::snprintf(integerPart, sizeof(integerPart), "%d", whole);
            std::snprintf(decimalPart, sizeof(decimalPart), "%d", tenth);
            hasDecimal = true;
        }
    } else {
        std::snprintf(integerPart, sizeof(integerPart), "%d", (meters + 500) / 1000);
    }

    const int integerWidth = canvas.fontTextWidth(integerPart, numberFont);
    const int decimalWidth = hasDecimal ? canvas.fontTextWidth(decimalPart, numberFont) : 0;
    const int unitWidth = canvas.fontTextWidth(unit, unitFont);
    const int commaWidth = hasDecimal ? 5 : 0;
    const int gap = 4;
    int totalWidth = integerWidth + commaWidth + decimalWidth + gap + unitWidth;
    if (totalWidth > maxWidth) {
        // The caller can use a smaller native font for narrow alert panels.
        totalWidth = std::min(totalWidth, maxWidth);
    }
    const int startX = centerX - totalWidth / 2;
    const int textY = baselineY - numberFont.lineHeight;
    int x = startX;
    canvas.fontText(x, textY, integerPart, numberFont, color, integerWidth, false);
    x += integerWidth;
    if (hasDecimal) {
        // Crisp comma: a 3x2 dot plus a 1px tail, drawn directly in pixels.
        const int commaY = baselineY - 5;
        canvas.fillRect(x + 1, commaY, 3, 2, color);
        canvas.fillRect(x, commaY + 2, 2, 2, color);
        x += commaWidth;
        canvas.fontText(x, textY, decimalPart, numberFont, color, decimalWidth, false);
        x += decimalWidth;
    }
    x += gap;
    canvas.fontText(x, baselineY - unitFont.lineHeight + 1, unit, unitFont, color, unitWidth, false);
}
#endif
void drawGuidanceLaneV2(Canvas &canvas, int x, int spacing, const LaneState &lane,
                      uint16_t foregroundColor) {
    constexpr int laneYOffset = 7;
    const int baseline = 40 + laneYOffset;
    const int junction = 29 + laneYOffset;
    const bool selectedLane = lane.selectedMask != 0;
    const uint16_t stemColor = selectedLane ? foregroundColor : colors::Muted;
    const int stemThickness = selectedLane ? 3 : 2;
    canvas.line(x, baseline, x, junction, stemColor, stemThickness);
    if (selectedLane)
        canvas.fillRect(x - spacing / 2 + 2, 47 + laneYOffset, std::max(2, spacing - 4), 2,
                        colors::Green);

    // v12.9: use one consistent geometry for every lane direction.  The
    // branch endpoint is kept far enough from the lane center that the 12x12
    // arrow-head masks never sit on top of the main vertical stem.
    const int branch = std::max(8, std::min(10, spacing / 2 - 2));
    const int elbowY = 23 + laneYOffset;
    const int upTipY = 14 + laneYOffset;
    const int sideTipY = 23 + laneYOffset;
    const int downTipY = 34 + laneYOffset;

    for (int bit = 0; bit < 8; ++bit) {
        const uint8_t flag = static_cast<uint8_t>(1U << bit);
        if ((lane.directionMask & flag) == 0) continue;

        const bool selectedDirection = (lane.selectedMask & flag) != 0;
        const uint16_t color = selectedDirection ? foregroundColor : colors::Muted;
        const int thickness = selectedDirection ? 3 : 2;

        switch (bit) {
            case 0: // straight
                canvas.line(x, junction, x, upTipY, color, thickness);
                laneArrowHead(canvas, bit, x, upTipY, color);
                break;

            case 1: // up-left: clean 90-degree elbow + diagonal head
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x - branch, upTipY, color, thickness);
                laneArrowHead(canvas, bit, x - branch, upTipY, color);
                break;

            case 2: // left: true horizontal branch
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x - branch, sideTipY, color, thickness);
                laneArrowHead(canvas, bit, x - branch, sideTipY, color);
                break;

            case 3: // down-left
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x - branch, downTipY, color, thickness);
                laneArrowHead(canvas, bit, x - branch, downTipY, color);
                break;

            case 4: // up-right
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x + branch, upTipY, color, thickness);
                laneArrowHead(canvas, bit, x + branch, upTipY, color);
                break;

            case 5: // right: true horizontal branch
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x + branch, sideTipY, color, thickness);
                laneArrowHead(canvas, bit, x + branch, sideTipY, color);
                break;

            case 6: // down-right
                canvas.line(x, junction, x, elbowY, color, thickness);
                canvas.line(x, elbowY, x + branch, downTipY, color, thickness);
                laneArrowHead(canvas, bit, x + branch, downTipY, color);
                break;

            case 7: { // U-turn: wider elbow, then downward arrow
                // Keep the U-turn head outside the central stem.  The old
                // 6-7 px elbow put the 12x12 down-arrow mask over the stem.
                const int uTurnX = x - branch;
                const int uTurnY = 20 + laneYOffset;
                const int uTurnTipY = 31 + laneYOffset;
                canvas.line(x, junction, uTurnX, uTurnY, color, thickness);
                canvas.line(uTurnX, uTurnY, uTurnX, uTurnTipY, color, thickness);
                laneArrowHead(canvas, bit, uTurnX, uTurnTipY, color);
                break;
            }
        }
    }
}

}  // namespace

esp_err_t HudRenderer::init() {
    buffer_ = static_cast<uint16_t *>(heap_caps_malloc(layout::MaxRegionPixels * sizeof(uint16_t),
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!buffer_) {
        ESP_LOGE(kTag, "Unable to allocate %d-byte dirty-region buffer",
                 layout::MaxRegionPixels * static_cast<int>(sizeof(uint16_t)));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void HudRenderer::render(const HudState &state, const DeviceSettings &settings,
                         const SystemStatusSnapshot &systemStatus) {
    const int64_t currentClockMillis = localClockMillis(state);
    const int64_t currentClockSecond = currentClockMillis == INT64_MIN
        ? INT64_MIN : currentClockMillis / 1000LL;
    const int64_t currentClockMinute = currentClockSecond == INT64_MIN
        ? INT64_MIN : currentClockSecond / 60;
    const int8_t currentClockPhase = currentClockMillis == INT64_MIN
        ? -1 : static_cast<int8_t>((currentClockMillis % 1000LL) < 500LL);
    clockActive_ = state.connected && state.hasProducerState && currentClockSecond != INT64_MIN;
    const bool streetChanged = firstFrame_ || !sameText(state.currentStreet, previous_.currentStreet);
    const int availableStreetWidth = currentClockMinute != INT64_MIN ? 248 : 310;
    Canvas metrics(buffer_, layout::Street.width, layout::Street.height);
    const int streetWidth = settings.showStreet
        ? metrics.fontTextWidth(displayStreet(state), assets::kTextMedium) : 0;
    const bool shouldMarquee = state.connected && state.hasProducerState && settings.showStreet &&
                               streetWidth > availableStreetWidth;
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    if (!shouldMarquee) {
        marqueeActive_ = false;
        marqueeOffset_ = 0;
    } else {
        if (!marqueeActive_ || streetChanged || streetWidth != marqueeTextWidth_ ||
            availableStreetWidth != marqueeAvailableWidth_) {
            marqueeEpochMs_ = nowMs;
            marqueeOffset_ = 0;
            marqueeRenderedOffset_ = -1;
        }
        marqueeActive_ = true;
        marqueeTextWidth_ = streetWidth;
        marqueeAvailableWidth_ = availableStreetWidth;
        constexpr uint64_t kStartHoldMs = 1200;
        constexpr uint64_t kEndHoldMs = 900;
        constexpr uint64_t kMsPerPixel = 45;
        const int overflow = streetWidth - availableStreetWidth;
        const uint64_t scrollMs = static_cast<uint64_t>(overflow) * kMsPerPixel;
        const uint64_t cycleMs = kStartHoldMs + scrollMs + kEndHoldMs;
        const uint64_t elapsed = cycleMs > 0 ? (nowMs - marqueeEpochMs_) % cycleMs : 0;
        if (elapsed < kStartHoldMs) marqueeOffset_ = 0;
        else if (elapsed < kStartHoldMs + scrollMs)
            marqueeOffset_ = std::min(overflow, static_cast<int>((elapsed - kStartHoldMs) / kMsPerPixel));
        else marqueeOffset_ = overflow;
    }
    const bool nextStreetChanged = firstFrame_ || !sameText(state.nextStreet, previous_.nextStreet);
    Canvas nextStreetMetrics(buffer_, layout::V1NextStreet.width, layout::V1NextStreet.height);
    const int nextStreetAvailableWidth = layout::V1NextStreet.width - 8;
    const int nextStreetWidth = nextStreetMetrics.fontTextWidth(state.nextStreet.data(), assets::kTextMedium);
    const bool shouldNextStreetMarquee = state.connected && state.hasProducerState &&
                                         nextStreetWidth > nextStreetAvailableWidth;
    if (!shouldNextStreetMarquee) {
        nextStreetMarqueeActive_ = false;
        nextStreetMarqueeOffset_ = 0;
    } else {
        if (!nextStreetMarqueeActive_ || nextStreetChanged ||
            nextStreetWidth != nextStreetMarqueeTextWidth_ ||
            nextStreetAvailableWidth != nextStreetMarqueeAvailableWidth_) {
            nextStreetMarqueeEpochMs_ = nowMs;
            nextStreetMarqueeOffset_ = 0;
            nextStreetMarqueeRenderedOffset_ = -1;
        }
        nextStreetMarqueeActive_ = true;
        nextStreetMarqueeTextWidth_ = nextStreetWidth;
        nextStreetMarqueeAvailableWidth_ = nextStreetAvailableWidth;
        constexpr uint64_t kStartHoldMs = 1200;
        constexpr uint64_t kEndHoldMs = 900;
        constexpr uint64_t kMsPerPixel = 45;
        const int overflow = nextStreetWidth - nextStreetAvailableWidth;
        const uint64_t scrollMs = static_cast<uint64_t>(overflow) * kMsPerPixel;
        const uint64_t cycleMs = kStartHoldMs + scrollMs + kEndHoldMs;
        const uint64_t elapsed = cycleMs > 0 ? (nowMs - nextStreetMarqueeEpochMs_) % cycleMs : 0;
        if (elapsed < kStartHoldMs) nextStreetMarqueeOffset_ = 0;
        else if (elapsed < kStartHoldMs + scrollMs)
            nextStreetMarqueeOffset_ = std::min(overflow,
                static_cast<int>((elapsed - kStartHoldMs) / kMsPerPixel));
        else nextStreetMarqueeOffset_ = overflow;
    }
    const bool marqueeFrameChanged = marqueeActive_ && marqueeOffset_ != marqueeRenderedOffset_;
    const bool statusChanged = firstFrame_ || state.connected != previous_.connected ||
                               state.hasProducerState != previous_.hasProducerState ||
                               state.navigationActive != previous_.navigationActive;
    const bool configChanged = firstFrame_ || hasSettingsChanged(settings, previousSettings_);
    if (configChanged) {
        const esp_err_t brightnessResult = DisplayDriver::instance().setBrightness(settings.brightness);
        if (brightnessResult != ESP_OK)
            ESP_LOGE(kTag, "Brightness update failed: %s", esp_err_to_name(brightnessResult));
        const esp_err_t orientationResult = DisplayDriver::instance().setOrientation(
            settings.mirrorHud, settings.rotateDisplay);
        if (orientationResult != ESP_OK)
            ESP_LOGE(kTag, "HUD orientation update failed: %s", esp_err_to_name(orientationResult));
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
        const esp_err_t invertResult = DisplayDriver::instance().setInvertColor(settings.invertColor);
        if (invertResult != ESP_OK)
            ESP_LOGE(kTag, "LCD invert update failed: %s", esp_err_to_name(invertResult));
        const esp_err_t colorResult = DisplayDriver::instance().setColorBgr(settings.colorBgr);
        if (colorResult != ESP_OK)
            ESP_LOGE(kTag, "LCD color-order update failed: %s", esp_err_to_name(colorResult));
        const esp_err_t backlightResult = DisplayDriver::instance().setBacklightPin(settings.backlightPin);
        if (backlightResult != ESP_OK)
            ESP_LOGE(kTag, "Backlight pin update failed: %s", esp_err_to_name(backlightResult));
#endif
    }
    const bool systemStatusChanged = firstFrame_ || systemStatus != previousSystemStatus_;
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    if (settings.speedDisplayMode == SpeedDisplayMode::NoNavigation) {
        // V2 is deliberately isolated to the third 1.1.0 display mode.
        // Render all V2 regions on each frame: this is robust for the first
        // integration pass and avoids changing the proven V1 dirty-region logic.
        if (systemStatus.visible) {
            renderRegion(layout::V2NextStreet,state,settings,systemStatus);
            renderRegion(layout::V2Maneuver,state,settings,systemStatus);
            renderRegion(layout::V2SpeedCluster,state,settings,systemStatus);
            renderRegion(layout::V2Guidance,state,settings,systemStatus);
            renderRegion(layout::V2Street,state,settings,systemStatus);
            renderRegion(layout::V2Alerts,state,settings,systemStatus);
        } else {
            renderRegion(layout::V2NextStreet,state,settings,systemStatus);
            renderRegion(layout::V2Maneuver,state,settings,systemStatus);
            renderRegion(layout::V2SpeedCluster,state,settings,systemStatus);
            renderRegion(layout::V2Guidance,state,settings,systemStatus);
            renderRegion(layout::V2Street,state,settings,systemStatus);
            renderRegion(layout::V2Alerts,state,settings,systemStatus);
        }
        previous_ = state;
        previousSettings_ = settings;
        previousSystemStatus_ = systemStatus;
        firstFrame_ = false;
        renderedClockMinute_ = currentClockMinute;
        renderedClockPhase_ = currentClockPhase;
        renderOverspeedBorder(state, settings, true);
        return;
    }
#endif
    const bool limitPrimary = settings.speedDisplayMode == SpeedDisplayMode::LimitPrimary;
    auto renderSpeedArea = [&]() {
        if (limitPrimary) {
            renderRegion(layout::V1SpeedCluster,state,settings,systemStatus);
        } else {
            renderRegion(layout::V1Speed,state,settings,systemStatus);
            renderRegion(layout::V1Limits,state,settings,systemStatus);
        }
    };
    if (systemStatus.visible) {
        if (systemStatusChanged || configChanged) {
            renderRegion(layout::Maneuver,state,settings,systemStatus);
            renderSpeedArea();
            renderRegion(layout::Alerts,state,settings,systemStatus);
            renderRegion(layout::Guidance,state,settings,systemStatus);
            renderRegion(layout::Street,state,settings,systemStatus);
        }
        // Keep the border as the final overlay so normal UI rendering cannot overwrite it.
        renderOverspeedBorder(state, settings, systemStatusChanged || configChanged);
        previous_ = state;
        previousSettings_ = settings;
        previousSystemStatus_ = systemStatus;
        firstFrame_ = false;
        return;
    }
    const bool systemStatusClosed = previousSystemStatus_.visible;
    bool streetRendered = false;
    bool v1HeaderNeedsRender = false;
    if (systemStatusClosed || statusChanged || configChanged || !state.connected || !state.hasProducerState) {
        renderRegion(layout::V1Maneuver,state,settings,systemStatus);
        if (limitPrimary) renderRegion(layout::V1SpeedCluster,state,settings,systemStatus);
        else {
            renderRegion(layout::V1Speed,state,settings,systemStatus);
            renderRegion(layout::V1Limits,state,settings,systemStatus);
        }
        renderRegion(layout::Alerts,state,settings,systemStatus);
        renderRegion(layout::Guidance,state,settings,systemStatus);
        renderRegion(layout::Street,state,settings,systemStatus);
        streetRendered = true;
        v1HeaderNeedsRender = state.connected && state.hasProducerState;
    } else {
        const bool maneuverWasChanged = maneuverChanged(state, previous_);
        if (maneuverWasChanged) {
            renderRegion(layout::V1Maneuver,state,settings,systemStatus);
            v1HeaderNeedsRender = true;
        }
        const bool speedChanged = state.speedKmh != previous_.speedKmh ||
                                  state.speedLimitKmh != previous_.speedLimitKmh;
        const bool limitChanged = state.speedLimitKmh != previous_.speedLimitKmh ||
                                  state.hasMinimumSpeed != previous_.hasMinimumSpeed ||
                                  state.minimumSpeedKmh != previous_.minimumSpeedKmh;
        if (limitPrimary) {
            if (speedChanged || limitChanged) {
                renderRegion(layout::V1SpeedCluster,state,settings,systemStatus);
                v1HeaderNeedsRender = true;
            }
        } else {
            if (speedChanged) {
                renderRegion(layout::V1Speed,state,settings,systemStatus);
                v1HeaderNeedsRender = true;
            }
            if (limitChanged) {
                renderRegion(layout::V1Limits,state,settings,systemStatus);
                v1HeaderNeedsRender = true;
            }
        }
        const bool changedAlerts = alertsChanged(state, previous_);
        if (changedAlerts) renderRegion(layout::Alerts,state,settings,systemStatus);
        if (guidanceChanged(state, previous_))
            renderRegion(layout::Guidance,state,settings,systemStatus);
        if (streetChanged ||
            settings.showStreet != previousSettings_.showStreet ||
            currentClockMinute != renderedClockMinute_ ||
            currentClockPhase != renderedClockPhase_ || marqueeFrameChanged) {
            renderRegion(layout::Street,state,settings,systemStatus);
            streetRendered = true;
        }
        if (systemStatusChanged) {
            renderSpeedArea();
            renderRegion(layout::Alerts,state,settings,systemStatus);
            v1HeaderNeedsRender = true;
        }
    }
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    const bool nextStreetFrameChanged =
        nextStreetMarqueeOffset_ != nextStreetMarqueeRenderedOffset_;
    if (state.connected && state.hasProducerState &&
        (v1HeaderNeedsRender || nextStreetChanged || nextStreetFrameChanged)) {
        renderRegion(layout::V1NextStreet, state, settings, systemStatus);
        nextStreetMarqueeRenderedOffset_ = nextStreetMarqueeOffset_;
    }
#endif
    previous_ = state;
    previousSettings_ = settings;
    previousSystemStatus_ = systemStatus;
    renderedClockMinute_ = currentClockMinute;
    renderedClockPhase_ = currentClockPhase;
    if (streetRendered) marqueeRenderedOffset_ = marqueeOffset_;
    // Border is drawn last, outside the normal UI regions.
    renderOverspeedBorder(state, settings, true);
    firstFrame_ = false;
}

namespace {
constexpr int16_t kBorderThickness = 3;
constexpr Rect kEdgeA{0, 0, layout::Width, kBorderThickness};
constexpr Rect kEdgeD{0, static_cast<int16_t>(layout::Height - kBorderThickness),
                      layout::Width, kBorderThickness};
constexpr Rect kEdgeC{0, kBorderThickness, kBorderThickness,
                      static_cast<int16_t>(layout::Height - 2 * kBorderThickness)};
constexpr Rect kEdgeB{static_cast<int16_t>(layout::Width - kBorderThickness), kBorderThickness,
                      kBorderThickness, static_cast<int16_t>(layout::Height - 2 * kBorderThickness)};
constexpr uint8_t kMaskEdgeA = 1U << 0;
constexpr uint8_t kMaskEdgeB = 1U << 1;
constexpr uint8_t kMaskEdgeD = 1U << 2;
constexpr uint8_t kMaskEdgeC = 1U << 3;
}

void HudRenderer::drawBorderEdge(const Rect &edge, uint16_t color) {
    const Rect physical = layout::physicalRect(edge);
    const int pixelCount = physical.width * physical.height;
    std::fill(buffer_, buffer_ + pixelCount, color);
    DisplayDriver::instance().drawRegion(physical, buffer_);
}

void HudRenderer::renderOverspeedBorder(const HudState &state, const DeviceSettings &settings, bool forceRedraw) {
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    const bool enabled = settings.overspeedBorder;
#else
    const bool enabled = false;
#endif
    const bool isOverspeed = enabled &&
        state.connected && state.hasProducerState &&
        (state.overSpeed || firmwareOverspeed(state, settings));

    if (!isOverspeed) {
        if (previousBorderMask_ != 0) {
            if (previousBorderMask_ & kMaskEdgeA) drawBorderEdge(kEdgeA, 0x0000);
            if (previousBorderMask_ & kMaskEdgeB) drawBorderEdge(kEdgeB, 0x0000);
            if (previousBorderMask_ & kMaskEdgeD) drawBorderEdge(kEdgeD, 0x0000);
            if (previousBorderMask_ & kMaskEdgeC) drawBorderEdge(kEdgeC, 0x0000);
            previousBorderMask_ = 0;
        }
        overspeedBorderActive_ = false;
        return;
    }

    overspeedBorderActive_ = true;
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    const uint64_t phase = nowMs % 500ULL;
    uint8_t targetMask = 0;
    if (phase < 200ULL) targetMask = kMaskEdgeC | kMaskEdgeA | kMaskEdgeB;
    else if (phase >= 250ULL && phase < 450ULL) targetMask = kMaskEdgeB | kMaskEdgeD | kMaskEdgeC;

    auto updateEdge = [this, targetMask, forceRedraw](uint8_t bit, const Rect &edge) {
        const bool shouldBeOn = (targetMask & bit) != 0;
        const bool wasOn = (previousBorderMask_ & bit) != 0;
        if (shouldBeOn != wasOn || (shouldBeOn && forceRedraw))
            drawBorderEdge(edge, shouldBeOn ? colors::Red : 0x0000);
    };
    updateEdge(kMaskEdgeA, kEdgeA);
    updateEdge(kMaskEdgeB, kEdgeB);
    updateEdge(kMaskEdgeD, kEdgeD);
    updateEdge(kMaskEdgeC, kEdgeC);
    previousBorderMask_ = targetMask;
}

void HudRenderer::renderV1NextStreet(Canvas &canvas, const HudState &state,
                                  const DeviceSettings &settings) {
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    // V1 option 2: use only the clear space above the current-speed and
    // speed-limit columns. The maneuver column is deliberately untouched.
    // This keeps all existing V1 numbers/icons at their original coordinates.
    canvas.clear(colors::Panel);
    if (state.nextStreet[0] == 0) return;

    const uint16_t fg = foreground(settings);
    constexpr int kPadding = 4;
    const int availableWidth = layout::V1NextStreet.width - kPadding * 2;
    const int textY = std::max(0, (layout::V1NextStreet.height - assets::kTextMedium.lineHeight) / 2 - 1);
    if (nextStreetMarqueeActive_) {
        canvas.fontText(kPadding - nextStreetMarqueeOffset_, textY,
                        state.nextStreet.data(), assets::kTextMedium, fg, -1, false);
    } else {
        canvas.fontText(kPadding, textY, state.nextStreet.data(), assets::kTextMedium,
                        fg, availableWidth, false);
    }
#else
    (void)canvas; (void)state; (void)settings;
#endif
}

void HudRenderer::renderV2NextStreet(Canvas &canvas, const HudState &state,
                                  const DeviceSettings &settings) {
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    canvas.clear(colors::Panel);
    const int textY = std::max(0, (layout::V2NextStreet.height - assets::kTextMedium.lineHeight) / 2 - 1);
    // UI NEW-10: next street stays left-aligned; the live clock occupies the
    // right side of the same top row. Keep a fixed right margin for a clean,
    // stable header and leave enough room for long street names.
    if (state.nextStreet[0] != 0) {
        canvas.fontText(4, textY, state.nextStreet.data(), assets::kTextMedium,
                        foreground(settings), 244, false);
    }
    const int64_t millis = localClockMillis(state);
    if (millis != INT64_MIN) {
        const int64_t minute = millis / 60000LL;
        const int normalizedMinute = static_cast<int>((minute % 1440 + 1440) % 1440);
        char clock[6];
        std::snprintf(clock, sizeof(clock), "%02d:%02d",
                      normalizedMinute / 60, normalizedMinute % 60);
        canvas.fontText(258, textY, clock, assets::kTextMedium, colors::White, 58, true);
    }
#else
    canvas.clear(colors::Panel);
#endif
}

void HudRenderer::renderV2Maneuver(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const uint16_t fg = foreground(settings);
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    // UI NEW-07: move the maneuver icon upward and give the distance a larger
    // native bitmap font. No bitmap scaling is used, so the digits stay crisp.
    canvas.setTranslation(settings.offsetX + 20, settings.offsetY - 24);
    drawManeuverIcon(canvas, state.maneuver, state.roundaboutExit, fg);
    canvas.setTranslation(settings.offsetX, settings.offsetY);
    drawDistanceCyd(canvas, 58, 116, state.maneuverDistanceM,
                    assets::kNumberDistance, assets::kTextMedium, fg, 116);
#else
    drawManeuverIcon(canvas,state.maneuver,state.roundaboutExit,fg);
    char distance[16]; formatDistance(state.maneuverDistanceM,distance,sizeof(distance));
    canvas.fontText(2,mainY(101),distance,assets::kTextMedium,fg,76,true);
#endif
}

void HudRenderer::renderV2SpeedCluster(Canvas &canvas, const HudState &state,
                                     const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    const uint16_t speedColor = firmwareOverspeed(state, settings)
        ? colors::Red : foreground(settings);

    char speed[5];
    std::snprintf(speed, sizeof(speed), "%d", std::clamp(adjustedSpeed(state.speedKmh, settings), 0, 999));
    // UI NEW-07: use the native speed font. Scaling the old bitmap made the
    // strokes uneven and visibly soft on the 320x240 panel.
    // UI NEW-10: center the speed number and unit on the same 108px column,
    // then lower the number slightly so its visual center matches the km/h label.
    // V2 tuning: keep the two-digit readout slightly higher, while giving
    // 3-digit speeds the same native large font instead of shrinking them.
    const int speedValue = adjustedSpeed(state.speedKmh, settings);
    const bool threeDigitSpeed = speedValue >= 100;
    const assets::BitmapFont &speedFont = assets::kNumberSpeed;
    const int speedX = 4; // shift the speed readout slightly right
    // Keep the 3-digit speed on the same baseline as 2-digit speeds.
    // The previous special Y calculation lifted 100+ speeds too far upward.
    const int speedY = 21;
    canvas.fontText(speedX, speedY, speed, speedFont, speedColor, 108, true);
    canvas.fontText(0, 78, "km/h", assets::kTextMedium, colors::Muted, 108, true);

    // UI NEW-08: render a native 84x84 speed-limit bitmap 1:1.
    // No runtime scaling and no fillCircle/font composition: the ring and
    // number retain the offline high-resolution raster edges.
    // V2 tuning: move the limit circle a few pixels left to increase the
    // separation from the speed readout without changing its vertical position.
    constexpr int signX = 150;
    // V3: lift the limit sign to align its visual center with the large speed
    // number. Keep this independent from the V2 (LimitPrimary) sign position.
    constexpr int signY = 52;
    if (state.speedLimitKmh > 0) {
        const assets::ColorBitmap *sign = speedLimitCurrentRingAsset(state.speedLimitKmh);
        if (sign && sign->pixels && sign->alpha) {
            // UI NEW-09: native 84x84 ring-only bitmap. The speed-limit
            // digits are rendered separately with the native number font.
            canvas.colorBitmap(signX - sign->width / 2, signY - sign->height / 2, *sign);
            // UI NEW-12: keep the proven 84px outer ring. The inner white face
            // is drawn with pixel-center aware anti-aliasing so it is symmetric
            // with the even-sized 84x84 ring bitmap (no +0.5px down/right bias).
            canvas.fillCircleAntiAliased(signX, signY, 33, colors::White);
            char limit[5];
            std::snprintf(limit, sizeof(limit), "%d", std::clamp(state.speedLimitKmh, 0, 999));
            canvas.fontText(signX - 42, signY - assets::kNumberLarge.lineHeight / 2,
                            limit, assets::kNumberLarge, colors::Black, 84, true);
        }
    } else if (assets::kNoSpeedCurrent.pixels && assets::kNoSpeedCurrent.alpha) {
        canvas.colorBitmap(signX - assets::kNoSpeedCurrent.width / 2,
                           signY - assets::kNoSpeedCurrent.height / 2,
                           assets::kNoSpeedCurrent);
    }
#else
    constexpr int signX = 60;
    constexpr int signY = 64;
    constexpr int outerRadius = 54;
    constexpr int innerRadius = 43;

    if (state.speedLimitKmh > 0) {
        canvas.fillCircle(signX, signY, outerRadius, colors::Red);
        canvas.fillCircle(signX, signY, innerRadius, colors::White);
        char limit[5];
        std::snprintf(limit, sizeof(limit), "%d", state.speedLimitKmh);
        canvas.fontText(signX - innerRadius,
                        signY - assets::kNumberLarge.lineHeight / 2,
                        limit, assets::kNumberLarge, colors::Black,
                        innerRadius * 2, true);
    } else if (assets::kNoSpeedCurrent.pixels && assets::kNoSpeedCurrent.alpha) {
        canvas.colorBitmap(signX - assets::kNoSpeedCurrent.width / 2,
                           signY - assets::kNoSpeedCurrent.height / 2,
                           assets::kNoSpeedCurrent);
    }

    char speed[5];
    std::snprintf(speed, sizeof(speed), "%d", std::clamp(adjustedSpeed(state.speedKmh, settings), 0, 999));
    const uint16_t speedColor = firmwareOverspeed(state, settings)
        ? colors::Red : foreground(settings);
    canvas.fontText(96, 101, speed, assets::kNumberMedium,
                    speedColor, 42, true);
#endif
}

void HudRenderer::renderV2Alerts(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    canvas.clear(colors::Background);
    // UI NEW-04: restore the full-width top separator over the alert column.

    // UI NEW-04: one warning only. The lower-right 80x90 panel is deliberately
    // simple: one large icon plus one large distance value. This keeps the
    // information readable at driving distance instead of packing two tiny
    // warnings into the same narrow column.
    AlertState warning{};
    if (state.noPassingZone) {
        warning.kind = AlertKind::NoPassing;
        warning.distanceM = state.noPassingRemainingM;
    } else if (state.nearestAlert.kind != AlertKind::None) {
        warning = state.nearestAlert;
    } else {
        for (uint8_t index = 0; index < state.upcomingAlertCount; ++index) {
            if (state.upcomingAlerts[index].kind != AlertKind::NoPassing) {
                warning = state.upcomingAlerts[index];
                break;
            }
        }
    }

    if (warning.kind != AlertKind::None) {
        // Use the large native alert asset where available. No bitmap scaling
        // is performed, so the generated icon remains crisp on the CYD.
        drawAlertIcon(canvas, 40, 31, 28, warning, true);
        const uint16_t distanceColor = alertDistanceColor(warning.distanceM, foreground(settings));
        drawDistanceCyd(canvas, 40, 84, warning.distanceM,
                        assets::kNumberMedium, assets::kTextSmall, distanceColor, 76);
        if (warning.kind == AlertKind::TrafficJam) {
            canvas.fontText(2, 80, trafficSeverityLabel(warning.trafficSeverity),
                            assets::kTextSmall, trafficSeverityColor(warning.trafficSeverity),
                            76, true);
        }
    }
#else
    canvas.clear(colors::Background);
    const bool activeZone = state.noPassingZone;
    AlertState primary = state.nearestAlert;
    if (activeZone) {
        primary.kind = AlertKind::NoPassing;
        primary.distanceM = state.noPassingRemainingM;
        primary.valueKmh = 0;
    }
    if (primary.kind != AlertKind::None) {
        drawAlertIcon(canvas,40,mainY(34),22,primary,true);
        char distance[16]; formatDistance(primary.distanceM,distance,sizeof(distance));
        canvas.fontText(2,mainY(60),distance,assets::kTextMedium,
                        alertDistanceColor(primary.distanceM, foreground(settings)),76,true);
        if (primary.kind == AlertKind::TrafficJam) {
            char trafficDetail[48];
            if (primary.trafficDelayMinutes >= 0)
                std::snprintf(trafficDetail, sizeof(trafficDetail), "%.20s +%d PH",
                              trafficSeverityLabel(primary.trafficSeverity),
                              primary.trafficDelayMinutes);
            else
                std::snprintf(trafficDetail, sizeof(trafficDetail), "%.20s",
                              trafficSeverityLabel(primary.trafficSeverity));
            canvas.fontText(1,mainY(78),trafficDetail,assets::kTextSmall,
                            trafficSeverityColor(primary.trafficSeverity),93,true);
        }
    }

    if (activeZone) {
        AlertState upcoming = state.nearestAlert;
        if (upcoming.kind == AlertKind::NoPassing) upcoming = {};
        for (uint8_t index = 0; upcoming.kind == AlertKind::None &&
                                index < state.upcomingAlertCount; ++index) {
            if (state.upcomingAlerts[index].kind != AlertKind::NoPassing)
                upcoming = state.upcomingAlerts[index];
        }
        if (upcoming.kind != AlertKind::None && !(upcoming == primary)) {
            drawAlertIcon(canvas,40,mainY(105),13,upcoming,false);
            char distance[12]; formatDistance(upcoming.distanceM,distance,sizeof(distance));
            canvas.fontText(2,mainY(121),distance,assets::kTextSmall,
                            alertDistanceColor(upcoming.distanceM, colors::Muted),76,true);
        }
    } else {
        const uint8_t count = std::min<uint8_t>(1,state.upcomingAlertCount);
        for (uint8_t index = 0; index < count; ++index) {
            drawAlertIcon(canvas,40,mainY(105),13,
                          state.upcomingAlerts[index],false);
            char distance[12];
            formatDistance(state.upcomingAlerts[index].distanceM,distance,sizeof(distance));
            canvas.fontText(2,mainY(121),distance,assets::kTextSmall,
                            alertDistanceColor(state.upcomingAlerts[index].distanceM,
                                               colors::Muted),76,true);
        }
    }
#endif
}

void HudRenderer::renderV2Guidance(Canvas &canvas, const HudState &state,
                                 const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const uint16_t fg = foreground(settings);
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    // UI NEW-01: the whole 240px lower-left area is dedicated to lane guidance.
    const uint8_t laneCount = std::min<uint8_t>(state.laneCount, 10);
    if (laneCount > 0) {
        const int available = layout::V2Guidance.width - 8;
        const int spacing = std::max(18, std::min(32, available / static_cast<int>(laneCount)));
        const int totalWidth = spacing * static_cast<int>(laneCount);
        const int firstX = (layout::V2Guidance.width - totalWidth) / 2 + spacing / 2;
        for (uint8_t index = 0; index < laneCount; ++index)
            drawGuidanceLaneV2(canvas, firstX + index * spacing, spacing,
                             state.lanes[index], fg);
    }
#else
    constexpr int etaWidth = 82;
    constexpr int laneLeft = etaWidth;
    constexpr int laneRight = layout::Width;

    canvas.fillRect(0, 0, layout::Width, 1, colors::Muted);
    canvas.fillRect(etaWidth - 1, 4, 1, layout::GuidanceHeight - 8, colors::Muted);

    if (state.eta[0] != 0) {
        canvas.fontText(0, 2, "ETA", assets::kTextSmall, colors::Muted, etaWidth - 2, true);
        char eta24[8] = {};
        const int64_t etaClockMillis = localClockMillis(state);
        bool formattedEta = false;
        if (etaClockMillis != INT64_MIN && state.remainingMinutes >= 0) {
            const int64_t nowMinutes = etaClockMillis / 60000LL;
            const int64_t arrivalMinutes = nowMinutes + static_cast<int64_t>(state.remainingMinutes);
            const int normalizedArrival = static_cast<int>((arrivalMinutes % 1440 + 1440) % 1440);
            std::snprintf(eta24, sizeof(eta24), "%02d:%02d",
                          normalizedArrival / 60, normalizedArrival % 60);
            formattedEta = true;
        }
        if (formattedEta)
            canvas.fontText(0, 14, eta24, assets::kTextMedium, fg, etaWidth - 2, true);
        else
            canvas.fontText(0, 14, state.eta.data(), assets::kTextMedium, fg, etaWidth - 2, true);
    }

    if (state.remainingMeters > 0) {
        char remainingDistance[16] = {};
        formatDistance(state.remainingMeters, remainingDistance, sizeof(remainingDistance));
        const char *separator = std::strchr(remainingDistance, ' ');
        if (separator) {
            char numberPart[12] = {};
            const size_t numberLength = static_cast<size_t>(separator - remainingDistance);
            const size_t copyLength = std::min(numberLength, sizeof(numberPart) - 1);
            std::memcpy(numberPart, remainingDistance, copyLength);
            numberPart[copyLength] = '\0';
            const char *unitPart = separator + 1;
            const int numberWidth = canvas.fontTextWidth(numberPart, assets::kTextMedium);
            const int unitWidth = canvas.fontTextWidth(unitPart, assets::kTextSmall);
            const int gap = 4;
            const int totalWidth = numberWidth + gap + unitWidth;
            const int startX = std::max(0, (etaWidth - totalWidth) / 2);
            canvas.fontText(startX, 32, numberPart, assets::kTextMedium, fg, numberWidth, false);
            canvas.fontText(startX + numberWidth + gap, 39, unitPart, assets::kTextSmall, fg,
                            unitWidth, false);
        } else {
            canvas.fontText(0, 32, remainingDistance, assets::kTextMedium, fg, etaWidth - 2, true);
        }
    }

    const uint8_t laneCount = std::min<uint8_t>(state.laneCount, 10);
    if (laneCount > 0) {
        const int available = laneRight - laneLeft - 6;
        const int spacing = std::min(32, available / static_cast<int>(laneCount));
        const int totalWidth = spacing * static_cast<int>(laneCount);
        const int firstX = laneLeft + (available - totalWidth) / 2 + spacing / 2 + 3;
        for (uint8_t index = 0; index < laneCount; ++index)
            drawGuidanceLaneV2(canvas, firstX + index * spacing, spacing,
                             state.lanes[index], fg);
    }
#endif
}

void HudRenderer::renderV2Street(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const int textY = std::max(0, (layout::V2StreetHeight - assets::kTextMedium.lineHeight) / 2);
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    // UI NEW-01: street name is a dedicated strip directly below the lanes.
    if (settings.showStreet) {
        const char *street = displayStreet(state);
        if (marqueeActive_)
            canvas.fontText(5 - marqueeOffset_, textY, street, assets::kTextMedium,
                            foreground(settings), -1, false);
        else
            canvas.fontText(5, textY, street, assets::kTextMedium,
                            foreground(settings), layout::V2Street.width - 10, true);
    }
#else
    const int64_t millis = localClockMillis(state);
    const int64_t second = millis == INT64_MIN ? INT64_MIN : millis / 1000LL;
    const bool haveClock = second != INT64_MIN;
    if (settings.showStreet) {
        const char *street = displayStreet(state);
        if (marqueeActive_)
            canvas.fontText(5-marqueeOffset_,textY,street,assets::kTextMedium,
                            foreground(settings),-1,false);
        else
            canvas.fontText(5,textY,street,assets::kTextMedium,foreground(settings),
                            haveClock ? 248 : 310,true);
    }
    if (haveClock) {
        canvas.fillRect(255,0,65,layout::Street.height,colors::Panel);
        const int64_t minute = second / 60;
        const int normalizedMinute = static_cast<int>((minute % 1440 + 1440) % 1440);
        const char separator = (millis % 1000LL) < 500LL ? ':' : ' ';
        const int hour24 = normalizedMinute / 60;
        char clock[8];
        std::snprintf(clock,sizeof(clock),"%02d%c%02d",
                      hour24,separator,normalizedMinute % 60);
        canvas.fontText(260,textY,clock,assets::kTextMedium,colors::White,55,true);
    }
#endif
}

void HudRenderer::renderRegion(const Rect &region, const HudState &state,
                               const DeviceSettings &settings,
                               const SystemStatusSnapshot &systemStatus) {
    const Rect physicalRegion = layout::physicalRect(region);
    Canvas canvas(buffer_, physicalRegion.width, physicalRegion.height,
                  region.width, region.height);
    // V1 main regions now start at Y=30 so the entire top strip can be
    // dedicated to the next-street header. Preserve every existing V1
    // drawing coordinate by translating the clipped region back by 30px.
    const bool isV1ClippedRegion = state.connected && state.hasProducerState &&
        (sameRegion(region, layout::V1Maneuver) ||
         sameRegion(region, layout::V1Speed) ||
         sameRegion(region, layout::V1SpeedCluster) ||
         sameRegion(region, layout::V1Limits));
    canvas.setTranslation(settings.offsetX,
                          settings.offsetY - (isV1ClippedRegion ? layout::V1NextStreetHeight : 0));
    if (systemStatus.visible) renderSystemStatus(canvas, region, systemStatus, settings);
    else if (!state.connected || !state.hasProducerState) renderStatus(canvas, region, state, settings);
    else if (sameRegion(region, layout::V1NextStreet)) renderV1NextStreet(canvas,state,settings);
    else if (sameRegion(region, layout::V1Maneuver)) renderManeuver(canvas,state,settings);
    else if (sameRegion(region, layout::V1SpeedCluster)) renderLimitPrimary(canvas,state,settings);
    else if (sameRegion(region, layout::V1Speed)) renderSpeed(canvas,state,settings);
    else if (sameRegion(region, layout::V1Limits)) renderLimits(canvas,state,settings);
    else if (sameRegion(region, layout::V2NextStreet)) renderV2NextStreet(canvas,state,settings);
    else if (sameRegion(region, layout::V2Maneuver)) renderV2Maneuver(canvas,state,settings);
    else if (sameRegion(region, layout::V2SpeedCluster)) renderV2SpeedCluster(canvas,state,settings);
    else if (sameRegion(region, layout::V2Alerts)) renderV2Alerts(canvas,state,settings);
    else if (sameRegion(region, layout::V2Guidance)) renderV2Guidance(canvas,state,settings);
    else if (sameRegion(region, layout::V2Street)) renderV2Street(canvas,state,settings);
    else if (sameRegion(region, layout::Maneuver)) renderManeuver(canvas,state,settings);
    else if (sameRegion(region, layout::SpeedCluster)) renderLimitPrimary(canvas,state,settings);
    else if (sameRegion(region, layout::Speed)) renderSpeed(canvas,state,settings);
    else if (sameRegion(region, layout::Limits)) renderLimits(canvas,state,settings);
    else if (sameRegion(region, layout::Alerts)) renderAlerts(canvas,state,settings);
    else if (sameRegion(region, layout::Guidance)) renderGuidance(canvas,state,settings);
    else renderStreet(canvas,state,settings);
    if (!systemStatus.visible && state.connected && state.hasProducerState) {
        renderMainIndicators(canvas, region, systemStatus);
    }
    const esp_err_t result = DisplayDriver::instance().drawRegion(physicalRegion, buffer_);
    if (result != ESP_OK) ESP_LOGE(kTag, "Dirty region (%d,%d %dx%d) failed: %s",
                                   region.x,region.y,region.width,region.height,esp_err_to_name(result));
}

void HudRenderer::renderMainIndicators(Canvas &canvas, const Rect &region,
                                       const SystemStatusSnapshot &systemStatus) {
    // Battery is centered across the upper HUD. It is omitted entirely when
    // GPIO4 does not contain a plausible single-cell LiPo voltage.
    if (systemStatus.batteryPresent &&
        (sameRegion(region, layout::Speed) || sameRegion(region, layout::Limits))) {
        constexpr int batteryX = 134;
        const int batteryY = mainY(5);
        constexpr int batteryWidth = 20;
        constexpr int batteryHeight = 11;
        const uint16_t batteryColor = systemStatus.batteryPercent <= 15 ? colors::Red
            : systemStatus.batteryPercent <= 35 ? colors::Amber : colors::Green;
        canvas.fillRect(batteryX - region.x, batteryY - region.y,
                        batteryWidth, batteryHeight, batteryColor);
        canvas.fillRect(batteryX + 2 - region.x, batteryY + 2 - region.y,
                        batteryWidth - 4, batteryHeight - 4, colors::Background);
        canvas.fillRect(batteryX + batteryWidth - region.x, batteryY + 3 - region.y,
                        3, batteryHeight - 6, batteryColor);
        const int fillWidth = (batteryWidth - 6) * systemStatus.batteryPercent / 100;
        canvas.fillRect(batteryX + 3 - region.x, batteryY + 3 - region.y,
                        fillWidth, batteryHeight - 6, batteryColor);
        char percent[8];
        std::snprintf(percent, sizeof(percent), "%u%%",
                      static_cast<unsigned>(systemStatus.batteryPercent));
        canvas.fontText(160 - region.x, mainY(0) - region.y, percent, assets::kTextSmall,
                        batteryColor, 38, false);
    }

}

void HudRenderer::renderSystemStatus(Canvas &canvas, const Rect &region,
                                     const SystemStatusSnapshot &systemStatus,
                                     const DeviceSettings &settings) {
    canvas.clear(colors::Background);
    const uint16_t fg = foreground(settings);
    canvas.fontText(0 - region.x, screenY(7) - region.y, "TRẠNG THÁI",
                    assets::kTextMedium, fg, layout::Width, true);

    // Battery body and terminal. The fill is proportional to the estimated
    // single-cell LiPo charge; an X marks an unavailable/non-battery reading.
    constexpr int batteryX = 25;
    const int batteryY = screenY(47);
    constexpr int batteryWidth = 52;
    constexpr int batteryHeight = 27;
    const uint16_t batteryColor = systemStatus.batteryPresent
        ? (systemStatus.batteryPercent <= 15 ? colors::Red
           : systemStatus.batteryPercent <= 35 ? colors::Amber : colors::Green)
        : colors::Muted;
    canvas.fillRect(batteryX - region.x, batteryY - region.y,
                    batteryWidth, batteryHeight, batteryColor);
    canvas.fillRect(batteryX + 3 - region.x, batteryY + 3 - region.y,
                    batteryWidth - 6, batteryHeight - 6, colors::Background);
    canvas.fillRect(batteryX + batteryWidth - region.x, batteryY + 8 - region.y,
                    5, batteryHeight - 16, batteryColor);
    if (systemStatus.batteryPresent) {
        const int fillWidth = (batteryWidth - 10) * systemStatus.batteryPercent / 100;
        canvas.fillRect(batteryX + 5 - region.x, batteryY + 5 - region.y,
                        fillWidth, batteryHeight - 10, batteryColor);
    } else {
        canvas.line(batteryX + 8 - region.x, batteryY + 6 - region.y,
                    batteryX + batteryWidth - 8 - region.x,
                    batteryY + batteryHeight - 6 - region.y, colors::Red, 3);
        canvas.line(batteryX + batteryWidth - 8 - region.x, batteryY + 6 - region.y,
                    batteryX + 8 - region.x,
                    batteryY + batteryHeight - 6 - region.y, colors::Red, 3);
    }
    char batteryText[24];
    if (systemStatus.batteryPresent)
        std::snprintf(batteryText, sizeof(batteryText), "PIN %u%%",
                      static_cast<unsigned>(systemStatus.batteryPercent));
    else
        std::snprintf(batteryText, sizeof(batteryText), "KHÔNG CÓ PIN");
    canvas.fontText(95 - region.x, screenY(49) - region.y, batteryText,
                    assets::kTextMedium,
                    batteryColor, 215, false);

    // Bluetooth rune plus four qualitative signal bars.
    constexpr int bluetoothX = 49;
    const int bluetoothTop = screenY(92);
    const int bluetoothBottom = screenY(132);
    const uint16_t bluetoothColor = bleSignalColor(systemStatus);
    canvas.line(bluetoothX - region.x, bluetoothTop - region.y,
                bluetoothX - region.x, bluetoothBottom - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX - region.x, bluetoothTop - region.y,
                bluetoothX + 13 - region.x, screenY(103) - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX + 13 - region.x, screenY(103) - region.y,
                bluetoothX - 10 - region.x, screenY(122) - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX - 10 - region.x, screenY(101) - region.y,
                bluetoothX + 13 - region.x, screenY(122) - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX + 13 - region.x, screenY(122) - region.y,
                bluetoothX - region.x, bluetoothBottom - region.y, bluetoothColor, 3);

    int signalBars = 0;
    if (systemStatus.bleConnected) {
        signalBars = systemStatus.bleRssiDbm >= -55 ? 4 :
                     systemStatus.bleRssiDbm >= -67 ? 3 :
                     systemStatus.bleRssiDbm >= -78 ? 2 :
                     systemStatus.bleRssiDbm >= -90 ? 1 : 0;
    }
    for (int bar = 0; bar < 4; ++bar) {
        const int height = 5 + bar * 5;
        canvas.fillRect(69 + bar * 6 - region.x, screenY(132) - height - region.y,
                        4, height, bar < signalBars ? bluetoothColor : colors::Muted);
    }
    char bleText[28];
    if (systemStatus.bleConnected)
        std::snprintf(bleText, sizeof(bleText), "BLE %d dBm",
                      static_cast<int>(systemStatus.bleRssiDbm));
    else
        std::snprintf(bleText, sizeof(bleText), "BLE CHƯA KẾT NỐI");
    canvas.fontText(100 - region.x, screenY(102) - region.y, bleText,
                    assets::kTextMedium, bluetoothColor, 210, false);
}

void HudRenderer::renderStatus(Canvas &canvas, const Rect &region, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Background);
    canvas.colorBitmap(16 - region.x, screenY(25) - region.y, assets::kBootIcon);
    constexpr int copyX = 120;
    constexpr int copyWidth = 190;
    canvas.fontText(copyX - region.x, screenY(34) - region.y, "WazeHUD", assets::kTextLarge,
                    colors::Foreground, copyWidth, true);
    const char *status = state.signalStale ? "Mất tín hiệu" : state.connected ? "Đã kết nối" : "Đang chờ thiết bị";
    const uint16_t statusColor = state.signalStale ? colors::Amber : state.connected ? colors::Green : colors::Muted;
    canvas.fontText(copyX - region.x, screenY(73) - region.y, status, assets::kTextMedium,
                    statusColor, copyWidth, true);
    const char *detail = state.signalStale ? "Đang đợi dữ liệu" : state.connected ? "Đang chờ WazeMod" : "Đang chờ kết nối";
    canvas.fontText(copyX - region.x, screenY(103) - region.y, detail, assets::kTextSmall,
                    foreground(settings), copyWidth, true);
}

void HudRenderer::renderManeuver(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const uint16_t fg = foreground(settings);
    drawManeuverIcon(canvas,state.maneuver,state.roundaboutExit,fg);
    char distance[16]; formatDistance(state.maneuverDistanceM,distance,sizeof(distance));
    canvas.fontText(2,mainY(101),distance,assets::kTextMedium,fg,76,true);
}

void HudRenderer::renderSpeed(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    // UI v10: keep the current-speed column on the same dark Panel background
    // as the speed-limit column. This removes the visible vertical shade split.
    canvas.clear(colors::Panel);
    const uint16_t color = firmwareOverspeed(state, settings) ? colors::Red : foreground(settings);
    char speed[5]; std::snprintf(speed,sizeof(speed),"%d",adjustedSpeed(state.speedKmh, settings));
    // Clean native bitmap font: centered in the 80px speed column.
    canvas.fontText(2,mainY(42),speed,assets::kNumberLarge,color,76,true);
    // Larger unit label, aligned with the maneuver distance row.
    canvas.fontText(2,mainY(101),"km/h",assets::kTextMedium,colors::Muted,76,true);
}

void HudRenderer::renderLimitPrimary(Canvas &canvas, const HudState &state,
                                     const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    // V2 (speed-limit-primary): move the speed-limit sign down into the
    // lower part of the 116px clipped main region. This leaves the top
    // 30px strip available for the future V2 next-street header while the
    // sign remains fully inside the main region.
    // V2 tuning 8: keep the sign at the same vertical position, but move it
    // 4px left to create a little more breathing room for 3-digit speeds.
    constexpr int signX = 56;
    constexpr int signY = 87;
    constexpr int outerRadius = 54;
    constexpr int innerRadius = 43;

    if (state.speedLimitKmh > 0) {
        canvas.fillCircle(signX, signY, outerRadius, colors::Red);
        canvas.fillCircle(signX, signY, innerRadius, colors::White);
        char limit[5];
        std::snprintf(limit, sizeof(limit), "%d", state.speedLimitKmh);
        canvas.fontText(signX - innerRadius,
                        signY - assets::kNumberLarge.lineHeight / 2,
                        limit, assets::kNumberLarge, colors::Black,
                        innerRadius * 2, true);
    } else if (assets::kNoSpeedCurrent.pixels && assets::kNoSpeedCurrent.alpha) {
        canvas.colorBitmap(signX - assets::kNoSpeedCurrent.width / 2,
                           signY - assets::kNoSpeedCurrent.height / 2,
                           assets::kNoSpeedCurrent);
    }

    char speed[5];
    std::snprintf(speed, sizeof(speed), "%d", adjustedSpeed(state.speedKmh, settings));
    const uint16_t speedColor = firmwareOverspeed(state, settings)
        ? colors::Red : foreground(settings);
    // V2 tuning 8: two-digit speeds stay large and are lifted 2px.
    // For 3-digit speeds use the 38px native distance font: it is visibly
    // larger than the old 19px medium font while still leaving room beside
    // the sign. Digits are placed with a slightly tighter 15px advance.
    const int speedValue = adjustedSpeed(state.speedKmh, settings);
    const bool threeDigitSpeed = speedValue >= 100;
    if (threeDigitSpeed) {
        const int y = signY + 54 - assets::kNumberDistance.lineHeight;
        // Spread the 3-digit readout horizontally so adjacent glyphs do not
        // visually crowd/merge, while keeping the same vertical position.
        constexpr int digitAdvance = 18;
        for (int i = 0; i < 3 && speed[i] != '\0'; ++i) {
            char digit[2] = {speed[i], '\0'};
            canvas.fontText(106 + i * digitAdvance, y, digit, assets::kNumberDistance,
                            speedColor, 20, false);
        }
    } else {
        // Lift 60/80 by 2px without changing their size.
        canvas.fontText(114, 98, speed, assets::kNumberLarge, speedColor, 50, true);
    }
}

void HudRenderer::renderLimits(Canvas &canvas, const HudState &state, const DeviceSettings &) {
    canvas.clear(colors::Panel);
    if (state.speedLimitKmh > 0) {
        // Keep the author's native 60px asset. No bitmap scaling means the
        // red ring and digits remain crisp on the CYD.
        const assets::ColorBitmap *sign = speedLimitAsset(state.speedLimitKmh, SpeedSignContext::Current);
        if (sign && sign->pixels && sign->alpha) {
            canvas.colorBitmap(40 - sign->width / 2, mainY(64) - sign->height / 2, *sign);
        } else {
            canvas.fillCircle(40,mainY(64),30,colors::White);
            canvas.circle(40,mainY(64),30,colors::Red,6);
            char value[5]; std::snprintf(value,sizeof(value),"%d",state.speedLimitKmh);
            canvas.fontText(10,mainY(64)-assets::kNumberMedium.lineHeight/2,value,
                            assets::kNumberMedium,colors::Black,60,true);
        }
    } else if (assets::kNoSpeedCurrent.pixels && assets::kNoSpeedCurrent.alpha) {
        canvas.colorBitmap(40 - assets::kNoSpeedCurrent.width / 2,
                           mainY(64) - assets::kNoSpeedCurrent.height / 2,
                           assets::kNoSpeedCurrent);
    }
    if (state.hasMinimumSpeed) {
        canvas.fillCircle(40,mainY(101),17,colors::Blue);
        char value[5]; std::snprintf(value,sizeof(value),"%d",state.minimumSpeedKmh);
        canvas.fontText(23,mainY(101)-assets::kNumberSmall.lineHeight/2,value,
                        assets::kNumberSmall,colors::White,34,true);
    }
}

void HudRenderer::renderAlerts(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Background);
    const bool activeZone = state.noPassingZone;
    AlertState primary = state.nearestAlert;
    if (activeZone) {
        primary.kind = AlertKind::NoPassing;
        primary.distanceM = state.noPassingRemainingM;
        primary.valueKmh = 0;
    }
    if (primary.kind != AlertKind::None) {
        drawAlertIcon(canvas,40,mainY(34),22,primary,true);
        char distance[16]; formatDistance(primary.distanceM,distance,sizeof(distance));
        canvas.fontText(2,mainY(60),distance,assets::kTextMedium,
                        alertDistanceColor(primary.distanceM, foreground(settings)),76,true);
        if (primary.kind == AlertKind::TrafficJam) {
            char trafficDetail[48];
            if (primary.trafficDelayMinutes >= 0)
                std::snprintf(trafficDetail, sizeof(trafficDetail), "%.20s +%d PH",
                              trafficSeverityLabel(primary.trafficSeverity),
                              primary.trafficDelayMinutes);
            else
                std::snprintf(trafficDetail, sizeof(trafficDetail), "%.20s",
                              trafficSeverityLabel(primary.trafficSeverity));
            canvas.fontText(1,mainY(78),trafficDetail,assets::kTextSmall,
                            trafficSeverityColor(primary.trafficSeverity),93,true);
        }
    }

    if (activeZone) {
        AlertState upcoming = state.nearestAlert;
        if (upcoming.kind == AlertKind::NoPassing) upcoming = {};
        for (uint8_t index = 0; upcoming.kind == AlertKind::None &&
                                index < state.upcomingAlertCount; ++index) {
            if (state.upcomingAlerts[index].kind != AlertKind::NoPassing)
                upcoming = state.upcomingAlerts[index];
        }
        if (upcoming.kind != AlertKind::None && !(upcoming == primary)) {
            drawAlertIcon(canvas,40,mainY(105),13,upcoming,false);
            char distance[12]; formatDistance(upcoming.distanceM,distance,sizeof(distance));
            canvas.fontText(2,mainY(121),distance,assets::kTextSmall,
                            alertDistanceColor(upcoming.distanceM, colors::Muted),76,true);
        }
    } else {
        const uint8_t count = std::min<uint8_t>(1,state.upcomingAlertCount);
        for (uint8_t index = 0; index < count; ++index) {
            drawAlertIcon(canvas,40,mainY(105),13,
                          state.upcomingAlerts[index],false);
            char distance[12];
            formatDistance(state.upcomingAlerts[index].distanceM,distance,sizeof(distance));
            canvas.fontText(2,mainY(121),distance,assets::kTextSmall,
                            alertDistanceColor(state.upcomingAlerts[index].distanceM,
                                               colors::Muted),76,true);
        }
    }

}

void HudRenderer::renderGuidance(Canvas &canvas, const HudState &state,
                                 const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const uint16_t fg = foreground(settings);
    constexpr int etaWidth = 82;
    constexpr int laneLeft = etaWidth;
    constexpr int laneRight = layout::Width;

    canvas.fillRect(0, 0, layout::Width, 1, colors::Muted);
    canvas.fillRect(etaWidth - 1, 4, 1, layout::GuidanceHeight - 8, colors::Muted);

    if (state.eta[0] != 0) {
        canvas.fontText(0, 2, "ETA", assets::kTextSmall, colors::Muted, etaWidth - 2, true);

        // UI v12.2: always show ETA in 24-hour format, independent of the
        // phone/WazeMod display setting. When remainingMinutes is available,
        // derive the arrival time from the HUD's synchronized local clock so
        // an incoming 12-hour string such as "08:42" is never ambiguous.
        char eta24[8] = {};
        const int64_t etaClockMillis = localClockMillis(state);
        bool formattedEta = false;
        if (etaClockMillis != INT64_MIN && state.remainingMinutes >= 0) {
            const int64_t nowMinutes = etaClockMillis / 60000LL;
            const int64_t arrivalMinutes = nowMinutes + static_cast<int64_t>(state.remainingMinutes);
            const int normalizedArrival = static_cast<int>((arrivalMinutes % 1440 + 1440) % 1440);
            std::snprintf(eta24, sizeof(eta24), "%02d:%02d",
                          normalizedArrival / 60, normalizedArrival % 60);
            formattedEta = true;
        }

        if (formattedEta)
            canvas.fontText(0, 14, eta24, assets::kTextMedium, fg, etaWidth - 2, true);
        else
            canvas.fontText(0, 14, state.eta.data(), assets::kTextMedium, fg, etaWidth - 2, true);
    }

    // UI v12.5: widen the ETA column so medium-size remaining distances such
    // as "121 KM" fit without clipping. The divider and lane area move right
    // together; the upper HUD remains unchanged.
    if (state.remainingMeters > 0) {
        // UI v12.6: keep the numeric distance prominent, but render only the
        // unit (M/KM) in the smaller font. This avoids widening the ETA column
        // just to fit the unit while keeping values such as "121 KM" legible.
        char remainingDistance[16] = {};
        formatDistance(state.remainingMeters, remainingDistance, sizeof(remainingDistance));

        const char *separator = std::strchr(remainingDistance, ' ');
        if (separator) {
            char numberPart[12] = {};
            const size_t numberLength = static_cast<size_t>(separator - remainingDistance);
            const size_t copyLength = std::min(numberLength, sizeof(numberPart) - 1);
            std::memcpy(numberPart, remainingDistance, copyLength);
            numberPart[copyLength] = '\0';

            const char *unitPart = separator + 1;
            const int numberWidth = canvas.fontTextWidth(numberPart, assets::kTextMedium);
            const int unitWidth = canvas.fontTextWidth(unitPart, assets::kTextSmall);
            const int gap = 4;
            const int totalWidth = numberWidth + gap + unitWidth;
            const int startX = std::max(0, (etaWidth - totalWidth) / 2);

            canvas.fontText(startX, 32, numberPart, assets::kTextMedium, fg,
                            numberWidth, false);
            canvas.fontText(startX + numberWidth + gap, 39, unitPart, assets::kTextSmall, fg,
                            unitWidth, false);
        } else {
            canvas.fontText(0, 32, remainingDistance, assets::kTextMedium, fg,
                            etaWidth - 2, true);
        }
    }

    const uint8_t laneCount = std::min<uint8_t>(state.laneCount, 10);
    if (laneCount > 0) {
        const int available = laneRight - laneLeft - 6;
        // UI v12.1: give each lane a wider cell. With 7 lanes this reaches
        // about 32 px/cell while still adapting down for 8-10 lanes.
        // This keeps the arrow heads and branches from crowding adjacent lanes.
        const int spacing = std::min(32, available / static_cast<int>(laneCount));
        const int totalWidth = spacing * static_cast<int>(laneCount);
        const int firstX = laneLeft + (available - totalWidth) / 2 + spacing / 2 + 3;
        for (uint8_t index = 0; index < laneCount; ++index)
            drawGuidanceLane(canvas, firstX + index * spacing, spacing,
                             state.lanes[index], fg);
    }

}

void HudRenderer::renderStreet(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const int textY = std::max(0, (layout::StreetHeight - assets::kTextMedium.lineHeight) / 2);
    const int64_t millis = localClockMillis(state);
    const int64_t second = millis == INT64_MIN ? INT64_MIN : millis / 1000LL;
    const bool haveClock = second != INT64_MIN;
    if (settings.showStreet) {
        const char *street = displayStreet(state);
        if (marqueeActive_)
            canvas.fontText(5-marqueeOffset_,textY,street,assets::kTextMedium,
                            foreground(settings),-1,false);
        else
            canvas.fontText(5,textY,street,assets::kTextMedium,foreground(settings),
                            haveClock ? 248 : 310,true);
    }
    if (haveClock) {
        // Clip marquee pixels before painting the independent clock column.
        canvas.fillRect(255,0,65,layout::Street.height,colors::Panel);
        const int64_t minute = second / 60;
        const int normalizedMinute = static_cast<int>((minute % 1440 + 1440) % 1440);
        const char separator = (millis % 1000LL) < 500LL ? ':' : ' ';
        // UI v12.2: always use 24-hour format. This clock is generated by
        // the HUD itself and therefore does not depend on the phone setting.
        const int hour24 = normalizedMinute / 60;
        char clock[8];
        std::snprintf(clock,sizeof(clock),"%02d%c%02d",
                      hour24,separator,normalizedMinute % 60);
        // UI v11.2: use full white for the main clock so it remains legible outdoors.
        canvas.fontText(260,textY,clock,assets::kTextMedium,colors::White,55,true);
    }
}

}  // namespace waze_hud
