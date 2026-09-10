#pragma once

#include <M5Unified.h>

#include <cstddef>

// The "version" reply: what hardware this is and what firmware runs on it.
// Board-independent apart from the board name the firmware was built for,
// which the caller passes in; the model comes from M5Unified's detection,
// everything else from ESP-IDF.

namespace monitor {

// Human name of the board M5Unified detected ("M5Stack-AtomS3R", ...). No
// spaces, so it fits a key=value line.
const char* boardModelName(m5::board_t board);

// Writes the VERSION line (without a newline) into buf and returns its
// length like snprintf. `board` is the name of the firmware build
// ("atoms3r"); the line also carries the detected model, chip and revision,
// core count, flash size, app version (git describe at build time), ESP-IDF
// and M5Unified versions, protocol version, project name, build time (ISO
// 8601, local to the build machine), the first 8 hex digits of the ELF
// SHA-256, uptime in seconds and the reason for the last reset.
int formatVersionLine(char* buf, size_t size, const char* board);

}  // namespace monitor
