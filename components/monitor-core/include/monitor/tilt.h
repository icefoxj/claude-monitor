#pragma once

// Continuous auto-rotation from the accelerometer (gravity vector), not the
// gyro. The icon angle is kept so the static icons stay upright at any
// tilt of the device; the display itself never changes rotation.

namespace monitor {

struct TiltConfig {
    // Board calibration: atan2(ay, ax) is the direction the accelerometer
    // reports as "up" in the sensor frame; the sign and offset absorb how
    // the IMU is mounted relative to the panel. Check with the "status"
    // command, which reports the angle in use.
    float angleSign   = -1.0f;
    float angleOffset = 0.0f;                       // radians

    float flatThreshold = 0.80f;                    // |az| above this = lying flat: hold the angle
    float tiltThreshold = 0.40f;                    // in-plane |g| below this = too noisy: hold
    float smoothing     = 0.25f;                    // per-frame low-pass factor (~130 ms at 30 fps)
    float redrawStep    = 1.0f * 3.14159265f / 180.0f;   // redraw once the angle moved this much
};

struct Tilt {
    float angle = 0.0f;   // smoothed icon angle, radians clockwise
    float drawn = 0.0f;   // angle of the icon currently on screen
    bool  valid = false;  // a usable reading has been seen
};

// Wraps to [-pi, pi]
float wrapAngle(float a);

// Feed one accelerometer sample (in g). Returns true when the icon on
// screen is stale enough to be redrawn.
bool updateTilt(Tilt& t, const TiltConfig& cfg, float ax, float ay, float az);

}  // namespace monitor
