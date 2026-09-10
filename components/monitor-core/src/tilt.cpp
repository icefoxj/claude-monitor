#include "monitor/tilt.h"

#include <cmath>

namespace monitor {

namespace {
constexpr float kPi = 3.14159265f;
}

float wrapAngle(float a){
    while (a >  kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

bool updateTilt(Tilt& t, const TiltConfig& cfg, float ax, float ay, float az){
    if (fabsf(az) < cfg.flatThreshold && sqrtf(ax*ax + ay*ay) > cfg.tiltThreshold){
        float target = wrapAngle(cfg.angleSign * atan2f(ay, ax) + cfg.angleOffset);
        if (!t.valid){
            t.angle = target;   // first reading: snap, no easing from zero
            t.valid = true;
        } else {
            t.angle = wrapAngle(t.angle + wrapAngle(target - t.angle) * cfg.smoothing);
        }
    }
    return fabsf(wrapAngle(t.angle - t.drawn)) >= cfg.redrawStep;
}

}  // namespace monitor
