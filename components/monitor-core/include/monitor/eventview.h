#pragma once

#include <M5Unified.h>

#include <cstdint>

#include "monitor/eventlog.h"

// The hook inspector: draws an EventLog into a canvas as a page of text,
// the last event with every one of its fields, then a one-line history.
// Board-independent: it lays itself out from the canvas size and the
// fonts' metrics. Meant for a panel with room next to the icon.

namespace monitor {

class EventView {
public:
    // `canvas` must already be created; it is pushed at (pushX, pushY)
    EventView(M5Canvas& canvas, int pushX, int pushY);

    // Redraws the whole page and pushes it. `statusLine` goes in the
    // header ("processing  sessions=wp  link=ok"); `nowMs` is the board's
    // clock, for the "12s ago" of each event.
    void draw(const EventLog& log, const char* statusLine, int64_t nowMs);

private:
    // Draws `text` wrapped inside `width` pixels from (x, y), at most
    // `maxLines` lines; returns the number of lines drawn
    int drawWrapped(int x, int y, int width, const char* text, uint16_t color, int maxLines);

    void ageText(int64_t receivedMs, int64_t nowMs, char* out, size_t size) const;

    M5Canvas& canvas_;
    int pushX_, pushY_;
};

}  // namespace monitor
