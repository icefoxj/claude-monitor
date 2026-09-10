#pragma once

#include "monitor/icons.h"
#include "monitor/protocol.h"
#include "monitor/tilt.h"

namespace monitor {

// Entry pulse of the "needs you" signs: three breaths over ~2 s so the
// change catches the eye from across the desk
struct AttentionConfig {
    int   frames = 60;     // total length in frames (~2 s at 30 fps)
    int   period = 20;     // frames per breath
    float amp    = 0.10f;  // +10 % size at the peak
};

// State machine plus animation bookkeeping, board-independent. Drive it at
// ~30 fps: feedAccel (if there is an IMU), apply for each command received,
// then tick once per frame.
class Ui {
public:
    Ui(Icons& icons, const TiltConfig& tiltCfg, const AttentionConfig& attention = {});

    // State change: redraws only when the state differs, starts the entry
    // pulse for the "needs you" signs
    void apply(State next);

    // One accelerometer sample (g). Redraws the static icon once the smoothed
    // angle has moved enough, unless a pulse is running (it redraws anyway)
    void feedAccel(float ax, float ay, float az);

    // Per-frame work: gear spin, pulse step. With the screen dark nothing is
    // drawn and a pending pulse is dropped
    void tick(bool screenOn);

    State state() const { return state_; }
    const Tilt& tilt() const { return tilt_; }

private:
    void render(float scale = 1.0f);

    Icons& icons_;
    TiltConfig tiltCfg_;
    AttentionConfig attention_;

    State state_     = State::Off;
    float gearAngle_ = 0.0f;
    Tilt  tilt_;
    int   pulseLeft_ = 0;   // pulse frames left, 0 = none
};

}  // namespace monitor
