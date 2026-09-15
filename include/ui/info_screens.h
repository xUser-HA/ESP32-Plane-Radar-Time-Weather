#pragma once

namespace ui::info {

enum class Screen {
  Radar = 0,
  Weather,
  Clock,
};

void drawWeather();
void drawClock();
void draw(Screen screen);

}  // namespace ui::info
