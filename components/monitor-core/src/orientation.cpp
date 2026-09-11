#include "monitor/orientation.h"

#include <cmath>

namespace monitor {

namespace {
constexpr float kPi = 3.14159265f;

int mod4(int v){
    return ((v % 4) + 4) % 4;
}

float wrapDeg(float d){
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}
}  // namespace

int Orientation::rotationFor(int sector) const {
    return mod4(cfg_.offset + cfg_.sign * sector);
}

int Orientation::offsetFor(int rotation, int sign) const {
    if (sector_ < 0){
        return mod4(rotation);
    }
    return mod4(rotation - sign * sector_);
}

void Orientation::setConfig(const OrientationConfig& cfg){
    cfg_ = cfg;
    if (sector_ >= 0){
        rotation_ = rotationFor(sector_);
    }
}

bool Orientation::update(float ax, float ay, float az){
    (void)az;
    float inPlane = sqrtf(ax * ax + ay * ay);
    if (inPlane < cfg_.minInPlane){
        candidate_ = -1;   // flat on the desk: keep what we have
        candidateFrames_ = 0;
        return false;
    }
    float deg = atan2f(ay, ax) * 180.0f / kPi;
    int s = mod4(static_cast<int>(lroundf(deg / 90.0f)));

    if (sector_ < 0){
        sector_   = s;   // first usable reading: snap
        rotation_ = rotationFor(s);
        return true;
    }
    if (s == sector_){
        candidate_ = -1;
        candidateFrames_ = 0;
        return false;
    }
    // Past the boundary by the hysteresis band, and steady for a while
    float fromCentre = fabsf(wrapDeg(deg - sector_ * 90.0f));
    if (fromCentre < 45.0f + cfg_.hysteresisDeg){
        candidate_ = -1;
        candidateFrames_ = 0;
        return false;
    }
    if (s != candidate_){
        candidate_ = s;
        candidateFrames_ = 1;
        return false;
    }
    if (++candidateFrames_ < cfg_.stableFrames){
        return false;
    }
    sector_ = s;
    candidate_ = -1;
    candidateFrames_ = 0;
    int r = rotationFor(s);
    bool changed = (r != rotation_);
    rotation_ = r;
    return changed;
}

}  // namespace monitor
