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
 * GENERAL
 * ============================================================
 */

constexpr int cx = config::kDisplayWidth / 2;
constexpr int cy = config::kDisplayHeight / 2;


/*
 * ============================================================
 * CLOCK SETTINGS
 * ============================================================
 *
 * 240x240 display:
 *
 * Clock radius = 88 px.
 *
 * This leaves a visible margin from the display edge.
 */

constexpr int kClockRadius = 88;

constexpr int kMinuteMarkOuter = 84;
constexpr int kHourMarkOuter = 84;
constexpr int kHourNumberRadius = 69;


/*
 * Clock hand lengths.
 */

constexpr int kHourHandLength = 45;
constexpr int kMinuteHandLength = 64;
constexpr int kSecondHandLength = 75;


/*
 * ============================================================
 * COLORS
 * ============================================================
 */

constexpr uint16_t kBlack    = 0x0000;
constexpr uint16_t kWhite    = 0xFFFF;
constexpr uint16_t kSilver   = 0xC618;
constexpr uint16_t kCyan     = 0x07FF;
constexpr uint16_t kRed      = 0xF800;
constexpr uint16_t kDarkBlue = 0x0190;


/*
 * ============================================================
 * TEXT HELPERS
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
 * CLOCK COORDINATES
 * ============================================================
 *
 * 12 o'clock = -PI/2.
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
     * Outer circle.
     */
    tft.drawCircle(
        cx,
        cy,
        kClockRadius,
        kDarkBlue
    );


    /*
     * Inner circle.
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
                ? 75.0f
                : 80.0f;


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
            0.85f
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
     * OTHER HOUR MARKS
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
 */

void drawClockHands(
    const struct tm& tm_now
) {

    /*
     * Seconds.
     */

    const float secondAngle =
        (
            static_cast<float>(
                tm_now.tm_sec
            ) *
            2.0f *
            PI /
            60.0f
        ) -
        PI / 2.0f;


    /*
     * Minutes including seconds.
     */

    const float minuteValue =
        static_cast<float>(
            tm_now.tm_min
        ) +
        static_cast<float>(
            tm_now.tm_sec
        ) / 60.0f;


    const float minuteAngle =
        (
            minuteValue *
            2.0f *
            PI /
            60.0f
        ) -
        PI / 2.0f;


    /*
     * Hours including minutes.
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
        kWhite
    );


    /*
     * Second hand.
     */

    drawHand(
        secondAngle,
        kSecondHandLength,
        kRed
    );


    /*
     * Center pin.
     */

    tft.fillCircle(
        cx,
        cy,
        5,
        kCyan
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
 * RUSSIAN MONTH
 * ============================================================
 *
 * Example:
 *
 * 15 сен
 */

const char* monthRu(
    int month
) {
    static const char* months[] = {
        "янв",
        "фев",
        "мар",
        "апр",
        "май",
        "июн",
        "июл",
        "авг",
        "сен",
        "окт",
        "ноя",
        "дек"
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
 * DATE
 * ============================================================
 *
 * Format:
 *
 * 15 сен
 *
 * No frame.
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
        monthRu(
            tm_now.tm_mon
        )
    );


    if (displayFontIsSmooth()) {

        displayFontSetSmoothSize(
            tft,
            0.70f
        );

    } else {

        tft.setTextSize(1);
    }


    tft.setTextColor(
        kWhite,
        kBlack
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
        cy + 20
    );
}


/*
 * ============================================================
 * END OF PRIVATE HELPERS
 * ============================================================
 *
 * IMPORTANT:
 *
 * Everything above is private to this source file.
 *
 * drawWeather(), drawClock() and draw() below belong directly
 * to namespace ui::info and are visible to the rest of the
 * project.
 */

}  // namespace


/*
 * ============================================================
 * WEATHER SCREEN
 * ============================================================
 */

void drawWeather() {

    style(
        0.72f,
        config::kTextOnBlack
    );


    const auto& w =
        services::weather::current();


    /*
     * Title.
     */

    line(
        "ПОГОДА",
        25
    );


    /*
     * No data.
     */

    if (!w.valid) {

        line(
            "Нет данных",
            82
        );

        line(
            "Ожидание данных...",
            112
        );

        line(
            "Проверьте соединение",
            142
        );

        return;
    }


    char buf[64];


    /*
     * Temperature.
     */

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


    line(
        buf,
        72
    );


    /*
     * Weather description.
     */

    if (displayFontIsSmooth()) {

        displayFontSetSmoothSize(
            tft,
            0.62f
        );
    }


    line(
        services::weather::descriptionRu(
            w.weather_code
        ),
        105
    );


    /*
     * Feels like.
     */

    snprintf(
        buf,
        sizeof(buf),
        "Ощущается %.1f C",
        w.apparent_c
    );


    line(
        buf,
        128
    );


    /*
     * Humidity.
     */

    snprintf(
        buf,
        sizeof(buf),
        "Влажность %.0f%%",
        w.humidity_pct
    );


    line(
        buf,
        150
    );


    /*
     * Wind.
     */

    snprintf(
        buf,
        sizeof(buf),
        "Ветер %.1f м/с %s",
        w.wind_ms,
        windDir(
            w.wind_deg
        )
    );


    line(
        buf,
        172
    );


    /*
     * Pressure.
     */

    snprintf(
        buf,
        sizeof(buf),
        "Давление %.0f hPa",
        w.pressure_hpa
    );


    line(
        buf,
        194
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
            "СИНХРОНИЗАЦИЯ",
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
     * 15 сен
     *
     * No frame.
     */

    drawClockDate(
        tm_now
    );


    /*
     * ========================================================
     * HANDS
     * ========================================================
     *
     * Drawn last.
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
