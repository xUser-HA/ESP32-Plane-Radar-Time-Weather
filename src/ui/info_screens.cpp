#include "ui/info_screens.h"

#include <Arduino.h>
#include <time.h>
#include <cstdio>
#include <cmath>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/weather_client.h"

namespace ui::info {

constexpr int cx = config::kDisplayWidth / 2;
constexpr int cy = config::kDisplayHeight / 2;

/*
 * ============================================================
 * COLORS
 * ============================================================
 */

constexpr uint16_t C_BLACK      = 0x0000;
constexpr uint16_t C_WHITE      = 0xFFFF;
constexpr uint16_t C_SILVER     = 0xC618;
constexpr uint16_t C_BLUE       = 0x04FF;
constexpr uint16_t C_CYAN       = 0x07FF;
constexpr uint16_t C_RED        = 0xF800;
constexpr uint16_t C_ORANGE     = 0xFD20;
constexpr uint16_t C_YELLOW     = 0xFFE0;
constexpr uint16_t C_GREEN      = 0x07E0;
constexpr uint16_t C_SKY       = 0x5DFF;
constexpr uint16_t C_CLOUD      = 0xBDF7;
constexpr uint16_t C_DARKCLOUD  = 0x7BEF;
constexpr uint16_t C_DARKBLUE   = 0x0190;

/*
 * ============================================================
 * CLOCK
 * ============================================================
 */

constexpr int CLOCK_RADIUS       = 101;
constexpr int MINUTE_MARK_OUTER = 97;
constexpr int HOUR_MARK_OUTER   = 96;
constexpr int HOUR_NUMBER_RADIUS = 81;

constexpr int HOUR_HAND_LENGTH   = 52;
constexpr int MINUTE_HAND_LENGTH = 74;

/*
 * ============================================================
 * BASIC DRAWING
 * ============================================================
 */

void clockPoint(
    float angle,
    float radius,
    int& x,
    int& y
) {
    x = cx + static_cast<int>(cosf(angle) * radius);
    y = cy + static_cast<int>(sinf(angle) * radius);
}


/*
 * ============================================================
 * WIND
 * ============================================================
 */

const char* windDir(float deg) {
    static const char* dirs[] = {
        "С", "СВ", "В", "ЮВ",
        "Ю", "ЮЗ", "З", "СЗ"
    };

    int i = static_cast<int>((deg + 22.5f) / 45.0f) % 8;
    return dirs[i];
}


/*
 * ============================================================
 * CLOCK FACE
 * ============================================================
 */

void drawClockFace() {

    /*
     * Outer circle.
     */

    tft.drawCircle(
        cx,
        cy,
        CLOCK_RADIUS,
        C_BLUE
    );

    tft.drawCircle(
        cx,
        cy,
        CLOCK_RADIUS - 2,
        C_SILVER
    );


    /*
     * 60 minute marks.
     */

    for (int minute = 0; minute < 60; ++minute) {

        const float angle =
            static_cast<float>(minute) *
            2.0f *
            PI /
            60.0f -
            PI / 2.0f;

        const bool hourMark =
            (minute % 5 == 0);

        const float innerRadius =
            hourMark ? 88.0f : 94.0f;

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
            MINUTE_MARK_OUTER,
            x2,
            y2
        );

        tft.drawLine(
            x1,
            y1,
            x2,
            y2,
            hourMark ? C_WHITE : C_DARKBLUE
        );
    }


    /*
     * Large 12 / 3 / 6 / 9.
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
        C_WHITE,
        C_BLACK
    );

    tft.setTextDatum(
        textdatum_t::middle_center
    );

    const int majorHours[] = {
        12, 3, 6, 9
    };

    for (int hour : majorHours) {

        const float angle =
            static_cast<float>(hour) *
            2.0f *
            PI /
            12.0f -
            PI / 2.0f;

        int x;
        int y;

        clockPoint(
            angle,
            HOUR_NUMBER_RADIUS,
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
     * Small hour markers.
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
            static_cast<float>(hour) *
            2.0f *
            PI /
            12.0f -
            PI / 2.0f;

        int x;
        int y;

        clockPoint(
            angle,
            HOUR_MARK_OUTER,
            x,
            y
        );

        tft.fillCircle(
            x,
            y,
            2,
            C_SILVER
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
 *
 * NO SECOND HAND.
 * ============================================================
 */

void drawClockHands(
    const struct tm& tm_now
) {

    /*
     * Minute hand.
     */

    const float minuteValue =
        static_cast<float>(
            tm_now.tm_min
        );

    const float minuteAngle =
        minuteValue *
        2.0f *
        PI /
        60.0f -
        PI / 2.0f;


    /*
     * Hour hand.
     */

    const float hourValue =
        static_cast<float>(
            tm_now.tm_hour % 12
        ) +
        minuteValue / 60.0f;

    const float hourAngle =
        hourValue *
        2.0f *
        PI /
        12.0f -
        PI / 2.0f;


    /*
     * Hour.
     */

    drawHand(
        hourAngle,
        HOUR_HAND_LENGTH,
        C_WHITE
    );


    /*
     * Minute.
     */

    drawHand(
        minuteAngle,
        MINUTE_HAND_LENGTH,
        C_CYAN
    );


    /*
     * Center.
     */

    tft.fillCircle(
        cx,
        cy,
        6,
        C_ORANGE
    );

    tft.fillCircle(
        cx,
        cy,
        2,
        C_WHITE
    );
}


/*
 * ============================================================
 * DATE
 *
 * Example:
 * 15 Sen
 * ============================================================
 */

const char* monthEn(int month) {

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


void drawClockDate(
    const struct tm& tm_now
) {
    char datebuf[24];

    snprintf(
        datebuf,
        sizeof(datebuf),
        "%02d %s",
        tm_now.tm_mday,
        monthEn(tm_now.tm_mon)
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
        C_YELLOW,
        C_BLACK
    );

    tft.setTextDatum(
        textdatum_t::middle_center
    );

    /*
     * No frame.
     */

    tft.drawString(
        datebuf,
        cx,
        cy + 24
    );
}


/*
 * ============================================================
 * SUN
 * ============================================================
 */

void drawSun(
    int x,
    int y,
    int radius
) {
    tft.fillCircle(
        x,
        y,
        radius,
        C_YELLOW
    );

    for (int i = 0; i < 8; ++i) {

        const float angle =
            static_cast<float>(i) *
            PI /
            4.0f;

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
            C_ORANGE
        );
    }
}


/*
 * ============================================================
 * CLOUD
 * ============================================================
 */

void drawCloud(
    int x,
    int y,
    bool dark
) {
    const uint16_t color =
        dark
            ? C_DARKCLOUD
            : C_CLOUD;

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
 * PARTLY CLOUDY
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
 * RAIN
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
            C_SKY
        );
    }
}


/*
 * ============================================================
 * SNOW
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
            C_WHITE
        );

        tft.drawLine(
            sx,
            sy - 4,
            sx,
            sy + 4,
            C_WHITE
        );

        tft.drawLine(
            sx - 3,
            sy - 3,
            sx + 3,
            sy + 3,
            C_WHITE
        );

        tft.drawLine(
            sx + 3,
            sy - 3,
            sx - 3,
            sy + 3,
            C_WHITE
        );
    }
}


/*
 * ============================================================
 * STORM
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

    tft.fillTriangle(
        x + 2,
        y + 21,
        x - 7,
        y + 38,
        x + 2,
        y + 34,
        C_YELLOW
    );

    tft.fillTriangle(
        x + 2,
        y + 34,
        x + 10,
        y + 24,
        x + 2,
        y + 28,
        C_YELLOW
    );
}


/*
 * ============================================================
 * WEATHER ICON
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
                C_SILVER
            );
        }

        return;
    }


    /*
     * Rain.
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
        C_BLACK
    );

    const auto& w =
        services::weather::current();


    /*
     * Header.
     */

    if (displayFontIsSmooth()) {
        displayFontSetSmoothSize(
            tft,
            0.62f
        );
    }

    tft.setTextColor(
        C_WHITE,
        C_BLACK
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
     * Weather icon.
     */

    drawWeatherIcon(
        w.weather_code,
        58,
        75
    );


    /*
     * Temperature.
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
        C_ORANGE,
        C_BLACK
    );

    tft.drawString(
        buf,
        145,
        70
    );


    /*
     * Description.
     */

    if (displayFontIsSmooth()) {
        displayFontSetSmoothSize(
            tft,
            0.55f
        );
    }

    tft.setTextColor(
        C_WHITE,
        C_BLACK
    );

    tft.drawString(
        services::weather::descriptionRu(
            w.weather_code
        ),
        145,
        98
    );


    /*
     * Feels like.
     */

    snprintf(
        buf,
        sizeof(buf),
        "Feels %.1f C",
        w.apparent_c
    );

    tft.setTextColor(
        C_CYAN,
        C_BLACK
    );

    tft.drawString(
        buf,
        cx,
        130
    );


    /*
     * Humidity.
     */

    snprintf(
        buf,
        sizeof(buf),
        "Humidity %.0f%%",
        w.humidity_pct
    );

    tft.setTextColor(
        C_SKY,
        C_BLACK
    );

    tft.drawString(
        buf,
        cx,
        154
    );


    /*
     * Wind.
     */

    snprintf(
        buf,
        sizeof(buf),
        "Wind %.1f m/s %s",
        w.wind_ms,
        windDir(w.wind_deg)
    );

    tft.setTextColor(
        C_GREEN,
        C_BLACK
    );

    tft.drawString(
        buf,
        cx,
        178
    );


    /*
     * Pressure.
     */

    snprintf(
        buf,
        sizeof(buf),
        "Pressure %.0f hPa",
        w.pressure_hpa
    );

    tft.setTextColor(
        C_SILVER,
        C_BLACK
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

    tft.fillScreen(
        C_BLACK
    );

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
            C_WHITE,
            C_BLACK
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
     * Clock face.
     */

    drawClockFace();


    /*
     * Date:
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
     * Hour + minute only.
     *
     * NO SECOND HAND.
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


/*
 * ============================================================
 * END NAMESPACE
 * ============================================================
 */

} // namespace ui::info
