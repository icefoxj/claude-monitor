// What changed between two frames of an off-screen image, so a board can
// send the display only that. The Tab5 draws its tiles' texts and frames
// into a display-sized canvas and pushes the parts that differ from the
// previous one: erasing a row and drawing it again on the panel made it
// flash once a second, worst with the LCARS elbows.
#pragma once

#include <cstdint>
#include <vector>

namespace monitor {

struct DiffRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// Where `cur` and `prev`, two 16-bit images of width x height pixels, differ
// inside `region` (clipped to the image): one rectangle per run of
// consecutive rows that differ, cut every `maxRows` rows, from the run's
// leftmost to its rightmost differing pixel. Nothing when they are equal.
std::vector<DiffRect> changedRects(const uint16_t* cur, const uint16_t* prev, int width, int height,
                                   const DiffRect& region, int maxRows = 64);

}  // namespace monitor
