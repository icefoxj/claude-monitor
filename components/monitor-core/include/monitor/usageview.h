#pragma once

#include <M5Unified.h>

#include <cstddef>
#include <cstdint>

#include "monitor/usage.h"

// Draws the account's plan usage: one row per limit with its label and
// percentage, a bar (green under 50 %, amber under 80 %, red above, or
// worse when the account says so) and the time left until it resets.
// Shared by the screensaver's card and the Tab5's usage tile, which come
// in very different sizes: the rows take the largest of four sizes that
// lets every one of them fit the box.

namespace monitor {

// Draws the rows of `usage` into the box (x, y, w, h) of `canvas`, centred
// vertically, over the colour `bg` (already filled by the caller): one per
// limit, then, when the account sent it, one for the week by app (a bar in
// one colour per app and a legend). `elapsedSec` is how long ago the usage
// line arrived, for the countdowns.
void drawUsageRows(M5Canvas& canvas, int x, int y, int w, int h,
                   const Usage& usage, int32_t elapsedSec, uint16_t bg);

// Rows drawUsageRows draws for `usage`
int usageRowCount(const Usage& usage);

// Height `rows` rows take at the largest size (the screensaver's card)
int usageRowsHeight(int rows);

// "updated just now", "updated 3 min ago", "updated 2 h ago"
void usageAgeText(int32_t ageSec, char* out, size_t size);

}  // namespace monitor
