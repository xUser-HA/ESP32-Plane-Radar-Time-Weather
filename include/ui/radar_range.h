#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::radar {

/**
 * Fixed radar range.
 *
 * The radar always displays a 25 km ring.
 *
 * The outer calculation radius is larger than the displayed
 * 25 km ring so that aircraft slightly beyond the ring can
 * still be received and drawn.
 */
struct RangePreset {
  /** Distance shown on ring 3, stored in kilometres. */
  float ring3_km;

  /** Outer radius used for aircraft calculations, in kilometres. */
  float outer_km;
};

constexpr float kRing3ToOuterKm = 4.0f / 3.0f;

/**
 * Only one radar range is available:
 *
 * 25 km displayed ring
 */
constexpr RangePreset kRangePresets[] = {
    {25.0f, 25.0f * kRing3ToOuterKm},
};

constexpr size_t kRangePresetCount =
    sizeof(kRangePresets) / sizeof(kRangePresets[0]);

/**
 * Load saved range and distance units from flash.
 * Call once after boot.
 */
void rangeInit();

/**
 * Cycle radar range.
 *
 * Kept for compatibility with the rest of the project.
 * With only one preset it always remains at 25 km.
 */
void rangeNext();

/** Return the currently selected range. */
const RangePreset& rangeCurrent();

/** Return the current range index. */
uint8_t rangeIndex();

/**
 * ADS-B fetch radius in kilometres.
 *
 * Slightly larger than the visible radar radius so that
 * aircraft near the edge can still be displayed.
 */
float fetchRadiusKm();

bool useMiles();

bool showRunways();

/**
 * WiFi portal checkbox:
 * "T" = enabled, otherwise disabled.
 */
void saveMilesFromPortal(const char* checkbox_value);

void saveRunwaysFromPortal(const char* checkbox_value);

/**
 * Format a radar range label.
 *
 * Examples:
 *   "25km"
 *   "16mi"
 */
void formatRing3Label(
    char* buf,
    size_t len,
    float ring3_km,
    bool use_miles
);

void formatCurrentRing3Label(
    char* buf,
    size_t len
);

/**
 * Reset distance units to kilometres and restore runway
 * display after WiFi credential wipe.
 */
void unitsReset();

}  // namespace ui::radar
