#include "monitor/ui.h"

#include <cmath>

namespace monitor {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kGearStep = 0.10f;   // radians per frame: ~3 s per turn at 30 fps

// Hourglass cycle at ~30 fps: the sand runs out, the glass turns over, the
// sand slides to the neck, repeat
constexpr int kDrainFrames  = 360;   // 12 s
constexpr int kFlipFrames   = 24;    // 0.8 s
constexpr int kSettleFrames = 12;    // 0.4 s
constexpr int kCycleFrames  = kDrainFrames + kFlipFrames + kSettleFrames;
}

Ui::Ui(Icons& icons, const TiltConfig& tiltCfg, const AttentionConfig& attention)
    : icons_(icons), tiltCfg_(tiltCfg), attention_(attention) {}

void Ui::render(float scale){
    switch(state_){
        case State::Processing:
            icons_.gear(gearAngle_, subagents_ > 0 ? GearStyle::Subagents : GearStyle::Working);
            break;
        case State::Compacting:
            icons_.gear(gearAngle_, GearStyle::Compacting);
            break;
        case State::WaitingUser:
            icons_.waiting(tilt_.angle, scale);
            tilt_.drawn = tilt_.angle;
            break;
        case State::Question:
            icons_.question(tilt_.angle, scale);
            tilt_.drawn = tilt_.angle;
            break;
        case State::Error:
            icons_.error(tilt_.angle, scale);
            tilt_.drawn = tilt_.angle;
            break;
        case State::Paused:
            icons_.paused(tilt_.angle, hourglass());
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

Hourglass Ui::hourglass() const {
    int f = animFrame_ % kCycleFrames;
    Hourglass h;
    if (f < kDrainFrames){
        h.drained = static_cast<float>(f) / kDrainFrames;
        h.falling = true;
    } else if (f < kDrainFrames + kFlipFrames){
        float u = static_cast<float>(f - kDrainFrames + 1) / kFlipFrames;
        h.drained = 1.0f;
        h.flip = u * u * (3.0f - 2.0f * u);   // ease in and out
    } else {
        h.drained = 0.0f;
        h.settle = static_cast<float>(f - kDrainFrames - kFlipFrames + 1) / kSettleFrames;
    }
    return h;
}

void Ui::apply(State next){
    if (next == state_){
        return;   // avoids needless redraws of the static icons
    }
    state_ = next;
    if (isAnimated(next)){
        gearAngle_ = 0.0f;
        animFrame_ = 0;
    }
    if (next == State::Idle || next == State::Off || next == State::Error || next == State::Paused){
        subagents_ = 0;   // the turn is over: nothing can still be running
    }
    pulseLeft_ = needsAttention(next) ? attention_.frames : 0;
    render();
}

void Ui::subagentStart(){
    ++subagents_;
    // The gear picks the style up on the next tick
}

void Ui::subagentStop(){
    if (subagents_ > 0){
        --subagents_;
    }
}

void Ui::feedAccel(float ax, float ay, float az){
    bool stale = updateTilt(tilt_, tiltCfg_, ax, ay, az);
    if (stale && pulseLeft_ == 0 && isStatic(state_)){
        render();
    }
}

void Ui::tick(bool screenOn){
    // The animated states redraw every frame
    if (isAnimated(state_) && screenOn){
        gearAngle_ += kGearStep;
        if (gearAngle_ >= 2.0f * kPi){
            gearAngle_ -= 2.0f * kPi;
        }
        ++animFrame_;
        render();
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
