#pragma once

// Whole-display rotation in 90-degree steps from the accelerometer, for a
// board that is turned like a tablet rather than tilted like the cube. The
// gravity vector's direction in the panel plane is quantised into four
// sectors; a sector change has to clear a hysteresis band past the
// 45-degree boundary and hold for a few samples before the display turns.

namespace monitor {

struct OrientationConfig {
    int   offset        = 0;      // display rotation (0..3) shown while sector 0 is "down"
    int   sign          = 1;      // +1 or -1: which way the rotation follows the sector
    float minInPlane    = 0.5f;   // |g| in the panel plane below this = lying flat: hold
    float hysteresisDeg = 12.0f;  // a new sector must be this far past the boundary
    int   stableFrames  = 15;     // and stay there this many samples (~0.5 s at 30 fps)
};

class Orientation {
public:
    explicit Orientation(const OrientationConfig& cfg = {}) : cfg_(cfg) {}

    // New mapping (calibration): the rotation is recomputed for the current
    // sector at once
    void setConfig(const OrientationConfig& cfg);
    const OrientationConfig& config() const { return cfg_; }

    // One accelerometer sample (g). Returns true when rotation() changed.
    bool update(float ax, float ay, float az);

    int  rotation() const { return rotation_; }   // 0..3, -1 until the first usable sample
    int  sector() const { return sector_; }       // quadrant of atan2(ay, ax): 0 +x, 1 +y, 2 -x, 3 -y
    bool valid() const { return sector_ >= 0; }

    // The offset that would make the current sector show `rotation` with
    // `sign`: what "calibrate rot=N" needs. Equals `rotation` before the
    // first sample.
    int offsetFor(int rotation, int sign) const;

private:
    int rotationFor(int sector) const;

    OrientationConfig cfg_;
    int sector_          = -1;
    int rotation_        = -1;
    int candidate_       = -1;
    int candidateFrames_ = 0;
};

}  // namespace monitor
