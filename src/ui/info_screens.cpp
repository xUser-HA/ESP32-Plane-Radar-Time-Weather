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

constexpr int cx = config::kDisplayWidth / 2;
constexpr int cy = config::kDisplayHeight / 2;

// Clock geometry for 240x240 round display.
constexpr int kClockRadius = 101;
constexpr int kMarkerRadius = 92;
constexpr int kHandRadius = 82;

// RGB565 colours.
constexpr uint16_t kBlack  = 0x0000;
constexpr uint16_t kWhite  = 0xFFFF;
constexpr uint16_t kSilver = 0xC618;
constexpr uint16_t kBlue   = 0x04FF;
constexpr uint16_t kCyan   = 0x07FF;
constexpr uint16_t kRed    = 0xF800;
constexpr uint16_t kDarkBlue = 0x0190;


/*
 * Set general text style.
 */
void style(
    float size,
    uint16_t fg,
    uint16_t bg = kBlack
) {
    tft.fillScreen(bg);

    tft.setTextColor(fg, bg);

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


/*
 * Draw centred text.
 */
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
 * Convert wind direction to Russian abbreviation.
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
 * Convert clock position to screen coordinates.
 *
 * 12 o'clock = -PI/2
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
 * Draw the analogue clock face.
 */
void drawClockFace() {

    /*
     * Outer rings.
     */
    tft.drawCircle(
        cx,
        cy,
        kClockRadius,
        kDarkBlue
    );

    tft.drawCircle(
        cx,
        cy,
        kClockRadius - 2,
        kSilver
    );


    /*
     * 60 minute marks.
     */
    for (int minute = 0; minute < 60; ++minute) {

        const float angle =
            (static_cast<float>(minute) *
             2.0f *
             PI /
             60.0f) -
            PI / 2.0f;

        const bool hourMark =
            (minute % 5 == 0);

        const float r1 =
            hourMark
                ? 82.0f
                : 88.0f;

        const float r2 =
            96.0f;

        int x1;
        int y1;
        int x2;
        int y2;

        clockPoint(
            angle,
            r1,
            x1,
            y1
        );

        clockPoint(
            angle,
            r2,
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
     * Four main hour markers.
     */
    for (int hour : {12, 3, 6, 9}) {

        float angle =
            (
                static_cast<float>(
                    hour
                ) *
                2.0f *
                PI /
                12.0f
            ) -
            PI / 2.0f;

        int x;
        int y;

        clockPoint(
            angle,
            kMarkerRadius,
            x,
            y
        );

        /*
         * Small filled circle behind the number
         * gives the dial an aviation/radar look.
         */
        tft.fillCircle(
            x,
            y,
            2,
            kCyan
        );
    }


    /*
     * Other hour markers.
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
            kMarkerRadius,
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


    /*
     * Hour numbers.
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

    for (int hour : {12, 3, 6, 9}) {

        char buf[4];

        snprintf(
            buf,
            sizeof(buf),
            "%d",
            hour
        );

        float angle =
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
            76.0f,
            x,
            y
        );

        tft.drawString(
            buf,
            x,
            y
        );
    }
}


/*
 * Draw one clock hand.
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
 * Draw clock hands.
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
     * Minutes including seconds for smooth movement.
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
        55.0f,
        kWhite
    );


    /*
     * Minute hand.
     */
    drawHand(
        minuteAngle,
        78.0f,
        kWhite
    );


    /*
     * Second hand.
     */
    drawHand(
        secondAngle,
        88.0f,
        kRed
    );


    /*
     * Centre pin.
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
 * Draw date in the centre of the clock.
 *
 * Example:
 * 15.09
 */
void drawClockDate(
    const struct tm& tm_now
) {

    char datebuf[16];

    snprintf(
        datebuf,
        sizeof(datebuf),
        "%02d.%02d",
        tm_now.tm_mday,
        tm_now.tm_mon + 1
    );


    /*
     * Small dark panel behind the date.
     */
    tft.fillRect(
        cx - 24,
        cy + 13,
        48,
        18,
        kBlack
    );


    tft.drawRect(
        cx - 24,
        cy + 13,
        48,
        18,
        kDarkBlue
    );


    if (displayFontIsSmooth()) {
        displayFontSetSmoothSize(
            tft,
            0.48f
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
        datebuf,
        cx,
        cy + 22
    );
}


/*
 * Draw clock header/footer.
 */
void drawClockLabels() {

    if (displayFontIsSmooth()) {
        displayFontSetSmoothSize(
            tft,
            0.48f
        );
    }

    tft.setTextColor(
        kCyan,
        kBlack
    );

    tft.setTextDatum(
        textdatum_t::middle_center
    );

    tft.drawString(
        "ALMATY",
        cx,
        20
    );


    tft.setTextColor(
        kSilver,
        kBlack
    );

    tft.drawString(
        "UTC+5",
        cx,
        220
    );
}

}  // namespace


void drawWeather() {

    style(
        0.72f,
        config::kTextOnBlack
    );

    const auto& w =
        services::weather::current();

    line(
        "ПОГОДА",
        25
    );


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
        windDir(w.wind_deg)
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


void drawClock() {

    /*
     * Start with a completely clean round clock.
     */
    tft.fillScreen(
        kBlack
    );


    /*
     * Get current local time.
     *
     * NTP is configured by main.cpp.
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
                0.7f
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
     * Draw dial.
     */
    drawClockFace();


    /*
     * Header/footer.
     */
    drawClockLabels();


    /*
     * Date in centre.
     */
    drawClockDate(
        tm_now
    );


    /*
     * Hands must be drawn last so they appear
     * above the dial and date.
     */
    drawClockHands(
        tm_now
    );
}


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
