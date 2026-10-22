#pragma once

#include <M5Unified.h>

#include <cstddef>
#include <cstdint>

#include "monitor/usage.h"

// The screensaver: a card with the account's plan usage (one bar per limit
// with its percentage and the time left until it resets) that drifts
// slowly over the whole display and bounces off its edges, so no part of
// the panel shows the same thing for long. Board-independent: it sizes
// itself from the display and the DejaVu fonts. The card is a PSRAM sprite
// with a black border wider than one step, so each push wipes the trail of
// the previous one and nothing flickers.

namespace monitor {

class UsageSaver {
public:
    explicit UsageSaver(M5GFX& display);

    // New numbers from the host (redrawn at once while the saver is on);
    // `nowMs` is the board's clock, for the countdowns and the age
    void setUsage(const Usage& usage, int64_t nowMs);

    // Clears the display and starts the drift from a place that changes
    // every time
    void enter(int64_t nowMs);

    // The board takes the display back
    void leave();

    bool active() const { return active_; }

    // Moves the card by the frames that went by (30 per second) and redraws
    // its text once a second
    void tick(int frames, int64_t nowMs);

    // The percentages for STATUS ("25,4,0"), or "-" without data
    void summary(char* out, size_t size) const;

private:
    void ensureCanvas();
    void render(int64_t nowMs);
    void push();

    M5GFX&   display_;
    M5Canvas card_;
    Usage    usage_;
    bool     have_       = false;
    bool     active_     = false;
    int64_t  receivedMs_ = 0;
    int64_t  renderedMs_ = 0;
    int      rows_       = -1;          // limits the canvas was made for
    float    x_ = 0.0f, y_ = 0.0f;
    float    vx_ = 0.7f, vy_ = 0.5f;    // px per frame
    int      px_ = -1, py_ = -1;        // where it was last pushed
};

}  // namespace monitor
