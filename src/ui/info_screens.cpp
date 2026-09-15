#include "ui/info_screens.h"

#include <Arduino.h>
#include <time.h>
#include <cstdio>
#include <cmath>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/radar_location.h"
#include "services/weather_client.h"

namespace ui::info {

namespace {

/*
 * ============================================================
 * DISPLAY
 * ============================================================
 */

constexpr int cx = config::kDisplayWidth / 2;
constexpr int cy = config::kDisplayHeight / 2;


/*
 * ============================================================
 * CLOCK
 * ============================================================
 */

constexpr int kClockRadius = 101;

constexpr int kMinuteMarkOuter = 97;
constexpr int kHourMarkOuter = 96;
constexpr int kHourNumberRadius = 81;

constexpr int kHourHandLength = 52;
constexpr int kMinuteHandLength = 74;


/*
 * ============================================================
 * COLORS
 * ============================================================
 */

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kSilver = 0xC618;

constexpr uint16_t kBlue = 0x04FF;
constexpr uint16_t kCyan = 0x07FF;

constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kOrange = 0xFD20;
constexpr uint16_t kYellow = 0xFFE0;

constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kSky = 0x5DFF;

constexpr uint16_t kCloud = 0xBDF7;
constexpr uint16_t kDarkCloud = 0x7BEF;

constexpr uint16_t kSnow = 0xFFFF;
constexpr uint16_t kDarkBlue = 0x0190;


/*
 * ============================================================
 * TEXT / GENERAL
 * ============================================================
 */

void style(
    float size,
    uint16_t fg,
    uint16_t bg = kBlack
) {
    tft.fillScreen(bg);

    tft.setTextColor(
        fg,
        bg
    );

    tft.setTextDatum(
        textdatum_t::middle_center
    );

    if (displayFontIsSmooth()) {
        displayFontSetSmoothSize(
            tft,
            size
        );
    } else {
        tft.setTextSize(1);
    }
}


void line(
    const char* s,
    int y
) {
    tft.drawString(
        s,
        cx,
        y
    );
}


/*
 * ============================================================
 * WIND DIRECTION
 * ============================================================
 */

const char* windDir(
    float deg
) {
    static const char* dirs[] = {
        "С",
        "СВ",
        "В",
        "ЮВ",
        "Ю",
        "ЮЗ",
        "З",
        "СЗ"
    };

    int i =
        static_cast<int>(
            (deg + 22.5f) / 45.0f
        ) % 8;

    return dirs[i];
}


/*
 * ============================================================
 * CLOCK GEOMETRY
 * ============================================================
 */

void clockPoint(
    float angle,
    float radius,
    int& x,
    int& y
) {
    x =
        cx +
        static_cast<int>(
            cosf(angle) * radius
        );

    y =
        cy +
        static_cast<int>(
            sinf(angle) * radius
        );
}


/*
 * ============================================================
 * CLOCK FACE
 * ============================================================
 */

void drawClockFace() {

    /*
     * Outer bezel.
     */

    tft.drawCircle(
        cx,
        cy,
        kClockRadius,
        kBlue
    );


    /*
     * Inner highlight.
     */

    tft.drawCircle(
        cx,
        cy,
        kClockRadius - 2,
        kSilver
    );


    /*
     * ========================================================
     * 60 MINUTE MARKS
     * ========================================================
     */

    for (int minute = 0; minute < 60; ++minute) {

        const float angle =
            (
                static_cast<float>(minute) *
                2.0f *
                PI /
                60.0f
            ) -
            PI / 2.0f;


        const bool hourMark =
            (minute % 5 == 0);


        const float innerRadius =
            hourMark
                ? 88.0f
                : 94.0f;


        int x1;
        int y1;
        int x2;
        int y2;


        clockPoint(
            angle,
            innerRadius,
            x1,
            y1
        );


        clockPoint(
            angle,
            kMinuteMarkOuter,
            x2,
            y2
        );


        tft.drawLine(
            x1,
            y1,
            x2,
            y2,
            hourMark
                ? kWhite
                : kDarkBlue
        );
    }


    /*
     * ========================================================
     * LARGE 12 / 3 / 6 / 9
     * ========================================================
     */

    if (displayFontIsSmooth()) {

        displayFontSetSmoothSize(
            tft,
            1.05f
        );

    } else {

        tft.setTextSize(2);
    }


    tft.setTextColor(
        kWhite,
        kBlack
    );


    tft.setTextDatum(
        textdatum_t::middle_center
    );


    const int majorHours[] = {
        12,
        3,
        6,
        9
    };


    for (int hour : majorHours) {

        const float angle =
            (
                static_cast<float>(hour) *
                2.0f *
                PI /
                12.0f
            ) -
            PI / 2.0f;


        int x;
        int y;


        clockPoint(
            angle,
            kHourNumberRadius,
            x,
            y
        );


        char buf[4];


        snprintf(
            buf,
            sizeof(buf),
            "%d",
            hour
        );


        tft.drawString(
            buf,
            x,
            y
        );
    }


    /*
     * ========================================================
     * OTHER HOUR MARKERS
     * ========================================================
     */

    for (int hour = 1; hour <= 12; ++hour) {

        if (
            hour == 12 ||
            hour == 3 ||
            hour == 6 ||
            hour == 9
        ) {
            continue;
        }


        const float angle =
            (
                static_cast<float>(hour) *
                2.0f *
                PI /
                12.0f
            ) -
            PI / 2.0f;


        int x;
        int y;


        clockPoint(
            angle,
            kHourMarkOuter,
            x,
            y
        );


        tft.fillCircle(
            x,
            y,
            2,
            kSilver
        );
    }
}


/*
 * ============================================================
 * CLOCK HAND
 * ============================================================
 */

void drawHand(
    float angle,
    float radius,
    uint16_t color
) {
    int x;
    int y;


    clockPoint(
        angle,
        radius,
        x,
        y
    );


    tft.drawLine(
        cx,
        cy,
        x,
        y,
        color
    );
}


/*
 * ============================================================
 * CLOCK HANDS
 * ============================================================
 *
 * IMPORTANT:
 * There is NO SECOND HAND.
 *
 * The clock contains only:
 *
 *   - hour hand
 *   - minute hand
 *   - center dot
 *
 * This prevents the second-by-second visual movement that
 * previously made the display flash.
 * ============================================================
 */

void drawClockHands(
    const struct tm& tm_now
) {

    /*
     * ========================================================
     * MINUTE HAND
     * ========================================================
     */

    const float minuteValue =
        static_cast<float>(
            tm_now.tm_min
        );


    const float minuteAngle =
        (
            minuteValue *
            2.0f *
            PI /
            60.0f
        ) -
        PI / 2.0f;


    /*
     * ========================================================
     * HOUR HAND
     * ========================================================
     *
     * The hour hand moves gradually according to the minute.
     */

    const float hourValue =
        static_cast<float>(
            tm_now.tm_hour % 12
        ) +
        minuteValue / 60.0f;


    const float hourAngle =
        (
            hourValue *
            2.0f *
            PI /
            12.0f
        ) -
        PI / 2.0f;


    /*
     * Hour hand.
     */

    drawHand(
        hourAngle,
        kHourHandLength,
        kWhite
    );


    /*
     * Minute hand.
     */

    drawHand(
        minuteAngle,
        kMinuteHandLength,
        kCyan
    );


    /*
     * Center.
     */

    tft.fillCircle(
        cx,
        cy,
        6,
        kOrange
    );


    tft.fillCircle(
        cx,
        cy,
        2,
        kWhite
    );
}


/*
 * ============================================================
 * DATE
 * ============================================================
 *
 * User requested English "Sen".
 *
 * Example:
 *
 *   15 Sen
 *
 * No frame.
 * ============================================================
 */

const char* monthEn(
    int month
) {
    static const char* months[] = {
        "Jan",
        "Feb",
        "Mar",
        "Apr",
        "May",
        "Jun",
        "Jul",
        "Aug",
        "Sen",
        "Oct",
        "Nov",
        "Dec"
    };


    if (
        month < 0 ||
        month > 11
    ) {
        return "";
    }


    return months[month];
}


/*
 * ============================================================
 * CLOCK DATE
 * ============================================================
 */

void drawClockDate(
    const struct tm& tm_now
) {
    char datebuf[24];


    snprintf(
        datebuf,
        sizeof(datebuf),
        "%02d %s",
        tm_now.tm_mday,
        monthEn(
            tm_now.tm_mon
        )
    );


    if (displayFontIsSmooth()) {

        displayFontSetSmoothSize(
            tft,
            0.82f
        );

    } else {

        tft.setTextSize(1);
    }


    tft.setTextColor(
        kYellow,
        kBlack
    );


    tft.setTextDatum(
        textdatum_t::middle_center
    );


    /*
     * No rectangle / frame.
     */

    tft.drawString(
        datebuf,
        cx,
        cy + 24
    );
}


/*
 * ============================================================
 * WEATHER ICON: SUN
 * ============================================================
 */

void drawSun(
    int x,
    int y,
    int radius
) {
    /*
     * Sun disc.
     */

    tft.fillCircle(
        x,
        y,
        radius,
        kYellow
    );


    /*
     * Rays.
     */

    for (int i = 0; i < 8; ++i) {

        const float angle =
            static_cast<float>(i) *
            PI / 4.0f;


        const int x1 =
            x +
            static_cast<int>(
                cosf(angle) *
                (radius + 5)
            );


        const int y1 =
            y +
            static_cast<int>(
                sinf(angle) *
                (radius + 5)
            );


        const int x2 =
            x +
            static_cast<int>(
                cosf(angle) *
                (radius + 11)
            );


        const int y2 =
            y +
            static_cast<int>(
                sinf(angle) *
                (radius + 11)
            );


        tft.drawLine(
            x1,
            y1,
            x2,
            y2,
            kOrange
        );
    }
}


/*
 * ============================================================
 * WEATHER ICON: CLOUD
 * ============================================================
 */

void drawCloud(
    int x,
    int y,
    bool dark
) {
    const uint16_t color =
        dark
            ? kDarkCloud
            : kCloud;


    tft.fillCircle(
        x - 16,
        y + 5,
        12,
        color
    );


    tft.fillCircle(
        x,
        y - 2,
        16,
        color
    );


    tft.fillCircle(
        x + 17,
        y + 6,
        11,
        color
    );


    tft.fillRect(
        x - 27,
        y + 5,
        54,
        14,
        color
    );
}


/*
 * ============================================================
 * WEATHER ICON: RAIN
 * ============================================================
 */

void drawRain(
    int x,
    int y
) {
    drawCloud(
        x,
        y,
        true
    );


    for (int i = -1; i <= 1; ++i) {

        const int rx =
            x + i * 14;


        tft.drawLine(
            rx,
            y + 24,
            rx - 4,
            y + 33,
            kSky
        );
    }
}


/*
 * ============================================================
 * WEATHER ICON: SNOW
 * ============================================================
 */

void drawSnow(
    int x,
    int y
) {
    drawCloud(
        x,
        y,
        false
    );


    for (int i = -1; i <= 1; ++i) {

        const int sx =
            x + i * 14;


        const int sy =
            y + 29;


        tft.drawLine(
            sx - 4,
            sy,
            sx + 4,
            sy,
            kSnow
        );


        tft.drawLine(
            sx,
            sy - 4,
            sx,
            sy + 4,
            kSnow
        );


        tft.drawLine(
            sx - 3,
            sy - 3,
            sx + 3,
            sy + 3,
            kSnow
        );


        tft.drawLine(
            sx + 3,
            sy - 3,
            sx - 3,
            sy + 3,
            kSnow
        );
    }
}


/*
 * ============================================================
 * WEATHER ICON: THUNDERSTORM
 * ============================================================
 */

void drawStorm(
    int x,
    int y
) {
    drawCloud(
        x,
        y,
        true
    );


    /*
     * Lightning bolt.
     */

    tft.fillTriangle(
        x + 2,
        y + 21,
        x - 7,
        y + 38,
        x + 2,
        y + 34,
        kYellow
    );


    tft.fillTriangle(
        x + 2,
        y + 34,
        x + 10,
        y + 24,
        x + 2,
        y + 28,
        kYellow
    );
}


/*
 * ============================================================
 * WEATHER ICON: PARTLY CLOUDY
 * ============================================================
 */

void drawPartlyCloudy(
    int x,
    int y
) {
    drawSun(
        x - 15,
        y - 9,
        13
    );


    drawCloud(
        x + 8,
        y + 7,
        false
    );
}


/*
 * ============================================================
 * WEATHER ICON SELECTOR
 * ============================================================
 *
 * WMO weather codes from Open-Meteo.
 * ============================================================
 */

void drawWeatherIcon(
    int code,
    int x,
    int y
) {

    /*
     * Clear.
     */

    if (code == 0) {

        drawSun(
            x,
            y,
            19
        );

        return;
    }


    /*
     * Mainly clear / partly cloudy.
     */

    if (
        code == 1 ||
        code == 2
    ) {

        drawPartlyCloudy(
            x,
            y
        );

        return;
    }


    /*
     * Overcast.
     */

    if (code == 3) {

        drawCloud(
            x,
            y,
            false
        );

        return;
    }


    /*
     * Fog.
     */

    if (
        code == 45 ||
        code == 48
    ) {

        drawCloud(
            x,
            y,
            true
        );


        for (int i = -1; i <= 1; ++i) {

            tft.drawLine(
                x - 25,
                y + 27 + i * 6,
                x + 25,
                y + 27 + i * 6,
                kSilver
            );
        }

        return;
    }


    /*
     * Drizzle / freezing rain / rain.
     */

    if (
        (code >= 51 && code <= 67) ||
        (code >= 80 && code <= 82)
    ) {

        drawRain(
            x,
            y
        );

        return;
    }


    /*
     * Snow.
     */

    if (
        (code >= 71 && code <= 77) ||
        code == 85 ||
        code == 86
    ) {

        drawSnow(
            x,
            y
        );

        return;
    }


    /*
     * Thunderstorm.
     */

    if (
        code == 95 ||
        code == 96 ||
        code == 99
    ) {

        drawStorm(
            x,
            y
        );

        return;
    }


    /*
     * Unknown.
     */

    drawCloud(
        x,
        y,
        false
    );
}


/*
 * ============================================================
 * WEATHER SCREEN
 * ============================================================
 */

void drawWeather() {

    tft.fillScreen(
        kBlack
    );


    const auto& w =
        services::weather::current();


    /*
     * Title.
     */

    if (displayFontIsSmooth()) {

        displayFontSetSmoothSize(
            tft,
            0.62f
        );
    }


    tft.setTextColor(
        kWhite,
        kBlack
    );


    tft.setTextDatum(
        textdatum_t::middle_center
    );


    tft.drawString(
        "WEATHER",
        cx,
        18
    );


    /*
     * No data.
     */

    if (!w.valid) {

        if (displayFontIsSmooth()) {

            displayFontSetSmoothSize(
                tft,
                0.65f
            );
        }


        tft.drawString(
            "No data",
            cx,
            100
        );


        tft.drawString(
            "Waiting...",
            cx,
            130
        );


        return;
    }


    /*
     * ========================================================
     * WEATHER ICON
     * ========================================================
     */

    drawWeatherIcon(
        w.weather_code,
        58,
        75
    );


    /*
     * ========================================================
     * TEMPERATURE
     * ========================================================
     */

    char buf[64];


    snprintf(
        buf,
        sizeof(buf),
        "%.1f C",
        w.temperature_c
    );


    if (displayFontIsSmooth()) {

        displayFontSetSmoothSize(
            tft,
            1.25f
        );
    }


    tft.setTextColor(
        kOrange,
        kBlack
    );


    tft.drawString(
        buf,
        145,
        70
    );


    /*
     * ========================================================
     * DESCRIPTION
     * ========================================================
     */

    if (displayFontIsSmooth()) {

        displayFontSetSmoothSize(
            tft,
            0.55f
        );
    }


    tft.setTextColor(
        kWhite,
        kBlack
    );


    tft.drawString(
        services::weather::descriptionRu(
            w.weather_code
        ),
        145,
        98
    );


    /*
     * ========================================================
     * FEELS LIKE
     * ========================================================
     */

    snprintf(
        buf,
        sizeof(buf),
        "Feels %.1f C",
        w.apparent_c
    );


    tft.setTextColor(
        kCyan,
        kBlack
    );


    tft.drawString(
        buf,
        cx,
        130
    );


    /*
     * ========================================================
     * HUMIDITY
     * ========================================================
     */

    snprintf(
        buf,
        sizeof(buf),
        "Humidity %.0f%%",
        w.humidity_pct
    );


    tft.setTextColor(
        kSky,
        kBlack
    );


    tft.drawString(
        buf,
        cx,
        154
    );


    /*
     * ========================================================
     * WIND
     * ========================================================
     */

    snprintf(
        buf,
        sizeof(buf),
        "Wind %.1f m/s %s",
        w.wind_ms,
        windDir(
            w.wind_deg
        )
    );


    tft.setTextColor(
        kGreen,
        kBlack
    );


    tft.drawString(
        buf,
        cx,
        178
    );


    /*
     * ========================================================
     * PRESSURE
     * ========================================================
     */

    snprintf(
        buf,
        sizeof(buf),
        "Pressure %.0f hPa",
        w.pressure_hpa
    );


    tft.setTextColor(
        kSilver,
        kBlack
    );


    tft.drawString(
        buf,
        cx,
        202
    );
}


/*
 * ============================================================
 * CLOCK SCREEN
 * ============================================================
 */

void drawClock() {

    /*
     * Clear display.
     */

    tft.fillScreen(
        kBlack
    );


    /*
     * Get local NTP time.
     */

    struct tm tm_now;


    if (
        !getLocalTime(
            &tm_now,
            50
        )
    ) {

        if (displayFontIsSmooth()) {

            displayFontSetSmoothSize(
                tft,
                0.70f
            );
        }


        tft.setTextColor(
            kWhite,
            kBlack
        );


        tft.setTextDatum(
            textdatum_t::middle_center
        );


        tft.drawString(
            "SYNC",
            cx,
            cy - 10
        );


        tft.drawString(
            "NTP...",
            cx,
            cy + 20
        );


        return;
    }


    /*
     * ========================================================
     * CLOCK FACE
     * ========================================================
     */

    drawClockFace();


    /*
     * ========================================================
     * DATE
     * ========================================================
     *
     * Example:
     *
     * 15 Sen
     *
     * No frame.
     * No Almaty.
     * No UTC+5.
     */

    drawClockDate(
        tm_now
    );


    /*
     * ========================================================
     * HANDS
     * ========================================================
     *
     * ONLY hour + minute.
     *
     * NO second hand.
     */

    drawClockHands(
        tm_now
    );
}


/*
 * ============================================================
 * SCREEN DISPATCH
 * ============================================================
 */

void draw(
    Screen screen
) {

    if (
        screen ==
        Screen::Weather
    ) {

        drawWeather();

    } else if (
        screen ==
        Screen::Clock
    ) {

        drawClock();
    }
}


}  // namespace ui::info
