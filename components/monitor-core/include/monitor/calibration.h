#pragma once

#include <cstdint>

// Orientation calibration kept in NVS (namespace "monitor"), shared by the
// boards. What the fields mean is the board's business: on the AtomS3R
// `rotation` is the display rotation and `sign` / `offsetDeg` the
// continuous tilt mapping; on the Tab5 `rotation` is the display rotation
// for gravity sector 0 and `sign` the direction, `offsetDeg` unused.

namespace monitor {

struct Calibration {
    uint8_t rotation  = 0;
    float   sign      = -1.0f;
    float   offsetDeg = 0.0f;
};

// Initialises NVS (a version mismatch just wipes it). False = NVS unusable.
bool initCalibrationStorage();

// True when NVS held at least one field (the others keep their values)
bool loadCalibration(Calibration& c);

// Stores the three fields; `reset` erases the namespace instead
bool saveCalibration(const Calibration& c, bool reset);

}  // namespace monitor
