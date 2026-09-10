#include "monitor/ui.h"

#include <cmath>

namespace monitor {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kGearStep = 0.10f;   // radians per frame: ~3 s per turn at 30 fps
}

Ui::Ui(Icons& icons, const TiltConfig& tiltCfg, const AttentionConfig& attention)
    : icons_(icons), tiltCfg_(tiltCfg), attention_(attention) {}

void Ui::render(float scale){
    switch(state_){
        case State::Processing:
            icons_.gear(gearAngle_);
            break;
        case State::WaitingUser:
            icons_.waiting(tilt_.angle, scale);
            tilt_.drawn = tilt_.angle;
            break;
        case State::Question:
            icons_.question(tilt_.angle, scale);
            tilt_.drawn = tilt_.angle;
            break;
        case State::Idle:
            icons_.done(tilt_.angle);
            tilt_.drawn = tilt_.angle;
            break;
        case State::Off:
            icons_.blank();
            break;
    }
}

void Ui::apply(State next){
    if (next == state_){
        return;   // avoids needless redraws of the static icons
    }
    state_ = next;
    if (next == State::Processing){
        gearAngle_ = 0.0f;
    }
    pulseLeft_ = needsAttention(next) ? attention_.frames : 0;
    render();
}

void Ui::feedAccel(float ax, float ay, float az){
    bool stale = updateTilt(tilt_, tiltCfg_, ax, ay, az);
    if (stale && pulseLeft_ == 0 && isStatic(state_)){
        render();
    }
}

void Ui::tick(bool screenOn){
    // Only the "processing" state redraws every frame
    if (state_ == State::Processing && screenOn){
        gearAngle_ += kGearStep;
        if (gearAngle_ >= 2.0f * kPi){
            gearAngle_ -= 2.0f * kPi;
        }
        icons_.gear(gearAngle_);
    }

    // Entry pulse: the last frame lands exactly on scale 1
    if (pulseLeft_ > 0){
        if (screenOn){
            int k = attention_.frames - pulseLeft_ + 1;   // 1..frames
            --pulseLeft_;
            float scale = (pulseLeft_ == 0)
                ? 1.0f
                : 1.0f + attention_.amp * fabsf(sinf(kPi * k / attention_.period));
            render(scale);
        } else {
            pulseLeft_ = 0;   // screen is dark: nothing to animate
        }
    }
}

}  // namespace monitor
