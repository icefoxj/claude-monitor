#pragma once

#include <M5Unified.h>

#include <cstdint>
#include <string>
#include <vector>

#include "monitor/usage.h"

// The usage page, what a tap on the Tab5's usage tile opens: every limit
// in full (a tick where an even pace would be, the reset as a clock time,
// where it ends up at the reset), the week as a chart (the percentage so
// far, the even-pace line, the projection to the reset), Claude Code's
// share of the weekly limit per project, and the plan with its extra
// usage. Lays itself out from the canvas size: two columns in landscape,
// one block under the other in portrait. Whatever the host did not send
// (an older daemon, no transcripts) is left out or says so.

namespace monitor {

// What the week's chart shows, worked out from the usage line (both themes
// draw it): the weekly limit the history belongs to, its window, the
// points and where the pace leads
struct UsageChart {
    struct Point { int64_t t; float v; };
    const UsageLimit* week = nullptr;   // the weekly limit; nullptr when none is known
    int64_t start = 0, end = 0;         // the window, Unix seconds
    int64_t now = 0;                    // the moment drawn as now
    std::vector<Point> points;          // 0 at the start, the readings, the current percentage now
    UsagePace pace;                     // of `week`
    float yMax = 100.0f;                // top of the scale, a multiple of 25
    bool noHistory = false;             // fewer than two readings: the line is the average so far
};

// False when there is nothing to draw a week from
bool usageChart(const Usage& usage, int32_t elapsedSec, UsageChart& out);

// The chart's line at moment `t`, straight between the points
float usageChartValue(const UsageChart& chart, int64_t t);

// The plan and extra-usage lines ("Max 5x, active since 16 Feb 2026",
// "$12.40 of $50.00 this month"); false when the host sent none. `tone`:
// -1 off, 0 plain, 1 needs a look (status not active), 2 limit reached
bool usagePlanText(const Usage& usage, std::string& out, int& tone);
bool usageExtraText(const Usage& usage, std::string& out, int& tone);

// The theme switch in the page's top-right corner: the icon, and the
// corner a tap on it counts in (larger than the icon: a fingertip covers
// more than its 56 px)
struct UsageSwitch {
    int x = 0, y = 0, size = 0;                  // the icon
    int hitX = 0, hitY = 0, hitW = 0, hitH = 0;  // where a tap switches
    bool contains(int px, int py) const { return px >= hitX && px < hitX + hitW && py >= hitY && py < hitY + hitH; }
};
UsageSwitch usageThemeSwitch(int pageWidth);

// The icon in the current theme's look: a square cut along its diagonal
// into the two looks, the classic's grey with a green dot and the anime's
// orange with a hexagon. Only drawn when the build has both themes
void drawThemeSwitch(LovyanGFX& g, const UsageSwitch& s);

class UsagePage {
public:
    // `canvas` must already be created, as large as the display; it is
    // pushed at (0, 0)
    explicit UsagePage(M5Canvas& canvas) : c_(canvas) {}

    // Where its theme switch is, for the board's touch handling
    UsageSwitch themeSwitch() const { return usageThemeSwitch(c_.width()); }

    // Redraws the whole page and pushes it. `elapsedSec`: how long ago the
    // usage line arrived, for the countdowns and the clock.
    void draw(const Usage& usage, int32_t elapsedSec);

private:
    struct Box { int x, y, w, h; };

    void drawHeader(const Usage& usage, int32_t elapsedSec, const Box& b);
    void drawChart(const Usage& usage, int32_t elapsedSec, const Box& b);
    void drawProjects(const Usage& usage, const Box& b);
    void drawPlan(const Usage& usage, const Box& b);
    void dashedLine(int x0, int y0, int x1, int y1, uint16_t color);

    // Sends the canvas to the display in bands (see kPushBand)
    void push();

    M5Canvas& c_;
};

}  // namespace monitor
