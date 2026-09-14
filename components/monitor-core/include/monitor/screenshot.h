#pragma once

#include <M5Unified.h>

namespace monitor {

// Writes the whole display as text through `write`, which must block until
// its bytes are out: one "SCREENSHOT w=<w> h=<h> fmt=rgb888" line, then one
// "ROW <y> <base64>" line per row (w*3 bytes, R G B per pixel, left to
// right), then "END". For documentation and debugging: a 1280x720 panel is
// a few MB of text, so the caller's loop stops for some seconds.
void dumpDisplay(lgfx::LGFXBase& display, void (*write)(const char* data, int n));

}  // namespace monitor
