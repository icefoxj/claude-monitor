#pragma once

#include <M5Unified.h>

#include <cstddef>
#include <cstdint>
#include <string>

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
// line arrived, for the countdowns. `detailed` (the usage page): a tick on
// each bar where an even pace would be, the reset as a clock time when the
// host sent its clock, and where the usage ends up at the reset.
void drawUsageRows(M5Canvas& canvas, int x, int y, int w, int h,
                   const Usage& usage, int32_t elapsedSec, uint16_t bg, bool detailed = false);

// "12 min", "3 h 05 min", "2 d 4 h"
void usageDurationText(int32_t sec, char* out, size_t size);

// "resets in 1 h 32 min", "resetting now", or empty when the reset is unknown
void usageResetText(const UsageLimit& limit, int32_t elapsedSec, char* out, size_t size);

// The texts under a bar on the usage page
struct UsageDetailText {
    char full[80]  = "";   // "resets Wed 07:00, in 20 h 33 min" ("resets in ..." without the host's clock)
    char brief[40] = "";   // "resets Wed 07:00", for when the full one does not fit; empty: none
    char pace[48]  = "";   // "on pace for 69%", "runs out Sat 15:20", "limit reached"; may be empty
    int  tone      = 0;    // of the pace: 0 plain, 1 runs out before the reset, 2 limit reached
};
void usageDetailText(const UsageLimit& limit, const Usage& usage, int32_t elapsedSec, UsageDetailText& out);

// Green under 50 %, amber under 80 %, red above, or worse when the account says so
uint16_t usageLevelColor(float percent, const std::string& severity);

// Rows drawUsageRows draws for `usage`
int usageRowCount(const Usage& usage);

// Height `rows` rows take at the largest size (the screensaver's card)
int usageRowsHeight(int rows);

// "updated just now", "updated 3 min ago", "updated 2 h ago"
void usageAgeText(int32_t ageSec, char* out, size_t size);

}  // namespace monitor
