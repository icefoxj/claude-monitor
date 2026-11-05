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

class UsagePage {
public:
    // `canvas` must already be created, as large as the display; it is
    // pushed at (0, 0)
    explicit UsagePage(M5Canvas& canvas) : c_(canvas) {}

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
