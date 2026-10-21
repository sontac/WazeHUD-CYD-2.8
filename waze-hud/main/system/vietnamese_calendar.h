#pragma once

#include <cstddef>
#include <cstdint>

namespace waze_hud {

struct VietnameseCalendarInfo {
    int weekday;       // 0 = Sunday, 1 = Monday, ... 6 = Saturday
    int solarDay;
    int solarMonth;
    int solarYear;
    int lunarDay;
    int lunarMonth;
    int lunarYear;
    bool lunarLeap;
    char canChiYear[24];
};

// localUnixSeconds is an epoch timestamp already adjusted to Vietnam local time (UTC+7).
bool getVietnameseCalendarInfo(int64_t localUnixSeconds, VietnameseCalendarInfo *output);

}  // namespace waze_hud
