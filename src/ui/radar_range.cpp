#include "ui/radar_range.h"

#include "ui/radar_theme.h"

#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";

constexpr char kPrefsRangeKey[] = "rangeIdx";
constexpr char kPrefsMilesKey[] = "useMiles";
constexpr char kPrefsRunwaysKey[] = "showRwys";

/*
 * Radar range is fixed at 25 km.
 *
 * Because kRangePresets contains only one element,
 * the only valid index is 0.
 */
constexpr uint8_t kDefaultRangeIndex = 0;

constexpr float kKmPerMile = 1.609344f;

Preferences s_prefs;

uint8_t s_range_index = kDefaultRangeIndex;

bool s_use_miles = false;

bool s_show_runways = true;


/*
 * Save the current range index.
 *
 * Kept for compatibility with the original project.
 */
void saveRangeIndex() {

  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }

  /*
   * Always save zero because there is only one
   * available range: 25 km.
   */
  s_prefs.putUChar(
      kPrefsRangeKey,
      kDefaultRangeIndex
  );

  s_prefs.end();
}


/*
 * Save distance units.
 */
void saveUseMiles() {

  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }

  s_prefs.putBool(
      kPrefsMilesKey,
      s_use_miles
  );

  s_prefs.end();
}


/*
 * Save runway display setting.
 */
void saveShowRunways() {

  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }

  s_prefs.putBool(
      kPrefsRunwaysKey,
      s_show_runways
  );

  s_prefs.end();
}


/*
 * Convert WiFiManager checkbox value to bool.
 */
bool portalCheckboxChecked(const char* value) {

  if (value == nullptr || value[0] == '\0') {
    return false;
  }

  /*
   * WiFiManager checkbox can submit:
   * T / F
   */
  if (
      (value[0] == 'T' ||
       value[0] == 't' ||
       value[0] == 'F' ||
       value[0] == 'f') &&
      value[1] == '\0'
  ) {
    /*
     * Preserve the behaviour of the original project:
     * the checkbox value itself means that it was checked.
     */
    return true;
  }

  return strcmp(value, "on") == 0;
}

}  // namespace


/*
 * Initialize radar range and user preferences.
 */
void rangeInit() {

  if (!s_prefs.begin(kPrefsNamespace, true)) {

    /*
     * Even if Preferences cannot be opened,
     * force 25 km.
     */
    s_range_index = kDefaultRangeIndex;

    return;
  }

  /*
   * Read the old saved value.
   *
   * Older firmware could have saved:
   *   0 = 5 km
   *   1 = 10 km
   *   2 = 15 km
   *   3 = 25 km
   *
   * The new firmware has only one preset.
   * Therefore ANY old value is converted to 0.
   */
  const uint8_t saved =
      s_prefs.getUChar(
          kPrefsRangeKey,
          kDefaultRangeIndex
      );

  if (saved < kRangePresetCount) {
    s_range_index = saved;
  } else {
    s_range_index = kDefaultRangeIndex;
  }

  /*
   * Since kRangePresetCount == 1,
   * the only valid index is zero.
   */
  s_range_index = kDefaultRangeIndex;

  s_use_miles =
      s_prefs.getBool(
          kPrefsMilesKey,
          false
      );

  s_show_runways =
      s_prefs.getBool(
          kPrefsRunwaysKey,
          true
      );

  s_prefs.end();

  /*
   * Make sure the old saved range is overwritten.
   */
  saveRangeIndex();
}


/*
 * Cycle radar range.
 *
 * There is only one preset, so this function always
 * returns to index 0.
 */
void rangeNext() {

  s_range_index =
      static_cast<uint8_t>(
          (s_range_index + 1) %
          kRangePresetCount
      );

  /*
   * With one preset:
   *
   * (0 + 1) % 1 = 0
   *
   * Therefore the radar always remains at 25 km.
   */
  s_range_index = kDefaultRangeIndex;

  saveRangeIndex();
}


/*
 * Return current radar range.
 */
const RangePreset& rangeCurrent() {

  /*
   * Extra protection against corrupted RAM/state.
   */
  if (s_range_index >= kRangePresetCount) {
    s_range_index = kDefaultRangeIndex;
  }

  return kRangePresets[s_range_index];
}


/*
 * Return current range index.
 */
uint8_t rangeIndex() {

  return kDefaultRangeIndex;
}


/*
 * Calculate ADS-B fetch radius.
 *
 * The visible ring is 25 km.
 * The actual request radius is slightly larger.
 */
float fetchRadiusKm() {

  const float outer_km =
      rangeCurrent().outer_km;

  const float screen_r_px =
      static_cast<float>(
          kCenterX -
          kBeyondRingScreenMarginPx
      );

  return outer_km *
         (
             screen_r_px /
             static_cast<float>(
                 kGridOuterRadius
             )
         );
}


/*
 * Distance units.
 */
bool useMiles() {

  return s_use_miles;
}


/*
 * Runway overlay.
 */
bool showRunways() {

  return s_show_runways;
}


/*
 * Save miles setting from WiFi portal.
 */
void saveMilesFromPortal(
    const char* checkbox_value
) {

  s_use_miles =
      portalCheckboxChecked(
          checkbox_value
      );

  saveUseMiles();

  Serial.printf(
      "Distance units: %s\n",
      s_use_miles
          ? "miles"
          : "km"
  );
}


/*
 * Save runway setting from WiFi portal.
 */
void saveRunwaysFromPortal(
    const char* checkbox_value
) {

  s_show_runways =
      portalCheckboxChecked(
          checkbox_value
      );

  saveShowRunways();

  Serial.printf(
      "Runway overlay: %s\n",
      s_show_runways
          ? "on"
          : "off"
  );
}


/*
 * Format radar range label.
 */
void formatRing3Label(
    char* buf,
    size_t len,
    float ring3_km,
    bool use_miles
) {

  if (use_miles) {

    const int mi =
        static_cast<int>(
            lroundf(
                ring3_km /
                kKmPerMile
            )
        );

    snprintf(
        buf,
        len,
        "%dmi",
        mi
    );

  } else {

    const int km =
        static_cast<int>(
            lroundf(ring3_km)
        );

    snprintf(
        buf,
        len,
        "%dkm",
        km
    );
  }
}


/*
 * Format the currently selected range.
 */
void formatCurrentRing3Label(
    char* buf,
    size_t len
) {

  formatRing3Label(
      buf,
      len,
      rangeCurrent().ring3_km,
      s_use_miles
  );
}


/*
 * Reset distance settings.
 *
 * Used when WiFi credentials are wiped.
 */
void unitsReset() {

  s_use_miles = false;

  s_show_runways = true;

  /*
   * Force radar range back to 25 km.
   */
  s_range_index = kDefaultRangeIndex;

  if (
      s_prefs.begin(
          kPrefsNamespace,
          false
      )
  ) {

    s_prefs.remove(
        kPrefsMilesKey
    );

    s_prefs.remove(
        kPrefsRunwaysKey
    );

    s_prefs.putUChar(
        kPrefsRangeKey,
        kDefaultRangeIndex
    );

    s_prefs.end();
  }
}

}  // namespace ui::radar
