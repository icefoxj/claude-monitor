#pragma once

#include <M5Unified.h>

#include <cstdint>

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

    M5Canvas& c_;
};

}  // namespace monitor
