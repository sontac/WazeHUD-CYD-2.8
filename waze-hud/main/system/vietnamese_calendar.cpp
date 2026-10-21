#include "system/vietnamese_calendar.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace waze_hud {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTimeZone = 7.0;

int julianDay(int day, int month, int year) {
    const int a = (14 - month) / 12;
    const int y = year + 4800 - a;
    const int m = month + 12 * a - 3;
    int jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    if (jd < 2299161) jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
    return jd;
}

double newMoon(int k) {
    const double t = static_cast<double>(k) / 1236.85;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double dr = kPi / 180.0;
    double jd = 2415020.75933 + 29.53058868 * k + 0.0001178 * t2 - 0.000000155 * t3;
    jd += 0.00033 * std::sin((166.56 + 132.87 * t - 0.009173 * t2) * dr);
    const double m = 359.2242 + 29.10535608 * k - 0.0000333 * t2 - 0.00000347 * t3;
    const double mp = 306.0253 + 385.81691806 * k + 0.0107306 * t2 + 0.00001236 * t3;
    const double f = 21.2964 + 390.67050646 * k - 0.0016528 * t2 - 0.00000239 * t3;
    double c1 = (0.1734 - 0.000393 * t) * std::sin(m * dr) + 0.0021 * std::sin(2 * dr * m);
    c1 += -0.4068 * std::sin(mp * dr) + 0.0161 * std::sin(2 * dr * mp);
    c1 += -0.0004 * std::sin(3 * dr * mp) + 0.0104 * std::sin(2 * dr * f);
    c1 += -0.0051 * std::sin(dr * (m + mp)) - 0.0074 * std::sin(dr * (m - mp));
    c1 += 0.0004 * std::sin(dr * (2 * f + m)) - 0.0004 * std::sin(dr * (2 * f - m));
    c1 += -0.0006 * std::sin(dr * (2 * f + mp)) + 0.0010 * std::sin(dr * (2 * f - mp));
    c1 += 0.0005 * std::sin(dr * (2 * mp + m));
    double deltaT;
    if (t < -11) {
        const double t4 = t3 * t;
        deltaT = 0.001 + 0.000839 * t + 0.0002261 * t2 - 0.00000845 * t3 - 0.000000081 * t4;
    } else {
        deltaT = -0.000278 + 0.000265 * t + 0.000262 * t2;
    }
    return jd + c1 - deltaT;
}

int newMoonDay(int k) {
    return static_cast<int>(std::floor(newMoon(k) + 0.5 + kTimeZone / 24.0));
}

int sunLongitudeSector(int dayNumber) {
    const double t = (static_cast<double>(dayNumber) - 2451545.0) / 36525.0;
    const double t2 = t * t;
    const double dr = kPi / 180.0;
    const double m = 357.52910 + 35999.05030 * t - 0.0001559 * t2 - 0.00000048 * t2 * t;
    const double l0 = 280.46645 + 36000.76983 * t + 0.0003032 * t2;
    const double dl = (1.914600 - 0.004817 * t - 0.000014 * t2) * std::sin(dr * m)
                    + (0.019993 - 0.000101 * t) * std::sin(2 * dr * m)
                    + 0.000290 * std::sin(3 * dr * m);
    double longitude = (l0 + dl) * dr;
    longitude -= 2 * kPi * std::floor(longitude / (2 * kPi));
    return static_cast<int>(std::floor(longitude / kPi * 6.0));
}

int lunarMonth11(int year) {
    const int off = julianDay(31, 12, year) - 2415021;
    const int k = static_cast<int>(std::floor(off / 29.530588853));
    int nm = newMoonDay(k);
    if (sunLongitudeSector(nm) >= 9) nm = newMoonDay(k - 1);
    return nm;
}

int leapMonthOffset(int a11) {
    const int k = static_cast<int>(std::floor((a11 - 2415021.076998695) / 29.530588853 + 0.5));
    int last = 0;
    int i = 1;
    int arc = sunLongitudeSector(newMoonDay(k + i));
    do {
        last = arc;
        ++i;
        arc = sunLongitudeSector(newMoonDay(k + i));
    } while (arc != last && i < 14);
    return i - 1;
}

struct LunarDate { int day; int month; int year; bool leap; };

LunarDate solarToLunar(int day, int month, int year) {
    const int dayNumber = julianDay(day, month, year);
    const int k = static_cast<int>(std::floor((dayNumber - 2415021.076998695) / 29.530588853));
    int monthStart = newMoonDay(k + 1);
    if (monthStart > dayNumber) monthStart = newMoonDay(k);
    int a11 = lunarMonth11(year);
    int b11 = a11;
    int lunarYear;
    if (a11 >= monthStart) {
        lunarYear = year;
        a11 = lunarMonth11(year - 1);
    } else {
        lunarYear = year + 1;
        b11 = lunarMonth11(year + 1);
    }
    const int lunarDay = dayNumber - monthStart + 1;
    const int diff = static_cast<int>(std::floor((monthStart - a11) / 29.0));
    int lunarMonth = diff + 11;
    bool lunarLeap = false;
    if (b11 - a11 > 365) {
        const int leapDiff = leapMonthOffset(a11);
        if (diff >= leapDiff) {
            lunarMonth = diff + 10;
            if (diff == leapDiff) lunarLeap = true;
        }
    }
    if (lunarMonth > 12) lunarMonth -= 12;
    if (lunarMonth >= 11 && diff < 4) --lunarYear;
    return {lunarDay, lunarMonth, lunarYear, lunarLeap};
}

void makeCanChiYear(int year, char *out, size_t capacity) {
    static constexpr const char *stems[] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
    static constexpr const char *branches[] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};
    const int stem = ((year + 6) % 10 + 10) % 10;
    const int branch = ((year + 8) % 12 + 12) % 12;
    std::snprintf(out, capacity, "%s %s", stems[stem], branches[branch]);
}
}  // namespace

bool getVietnameseCalendarInfo(int64_t localUnixSeconds, VietnameseCalendarInfo *output) {
    if (!output || localUnixSeconds < 1577836800LL || localUnixSeconds > 4102444799LL) return false;
    const time_t localTime = static_cast<time_t>(localUnixSeconds);
    struct tm date{};
    if (!gmtime_r(&localTime, &date)) return false;
    const LunarDate lunar = solarToLunar(date.tm_mday, date.tm_mon + 1, date.tm_year + 1900);
    output->weekday = date.tm_wday;
    output->solarDay = date.tm_mday;
    output->solarMonth = date.tm_mon + 1;
    output->solarYear = date.tm_year + 1900;
    output->lunarDay = lunar.day;
    output->lunarMonth = lunar.month;
    output->lunarYear = lunar.year;
    output->lunarLeap = lunar.leap;
    makeCanChiYear(lunar.year, output->canChiYear, sizeof(output->canChiYear));
    return true;
}

}  // namespace waze_hud
