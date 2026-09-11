#include "monitor/ui.h"

#include <cmath>
#include <cstring>

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

// Elapsed-work ring: one lap per 10 minutes
constexpr int kRingLapFrames = 10 * 60 * 30;

// Link watchdog: once the host has sent a ping, this long without any line
// means the host is gone
constexpr int kLinkLostFrames = 60 * 30;   // 60 s

constexpr int kMaxSessions = 8;
}

Ui::Ui(Icons& icons, const TiltConfig& tiltCfg, const AttentionConfig& attention)
    : icons_(icons), tiltCfg_(tiltCfg), attention_(attention) {}

bool Ui::linkLost() const {
    return linkArmed_ && linkFrames_ >= kLinkLostFrames;
}

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
    if (state_ != State::Off){
        overlays();
    }
    icons_.push();
}

void Ui::overlays(){
    if (state_ == State::Processing || state_ == State::Compacting){
        int lap = workFrames_ / kRingLapFrames;
        float fraction = static_cast<float>(workFrames_ % kRingLapFrames) / kRingLapFrames;
        icons_.ring(tilt_.angle, fraction, lap);
    }
    if (sessionCount_ >= 2){
        icons_.sessionDots(tilt_.angle, sessions_);
    }
    if (toolRunning_ && state_ == State::Processing){
        icons_.toolMark(tilt_.angle);
    }
    if (linkLost()){
        icons_.linkLost(tilt_.angle);
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

bool Ui::apply(State next){
    if (next == state_){
        return false;   // avoids needless redraws of the static icons
    }
    state_ = next;
    stateFrames_ = 0;
    if (isAnimated(next)){
        gearAngle_ = 0.0f;
        animFrame_ = 0;
    }
    if (next == State::Idle || next == State::Off || next == State::Error || next == State::Paused){
        subagents_   = 0;   // the turn is over: nothing can still be running
        toolRunning_ = false;
        workFrames_  = 0;
    }
    pulseLeft_ = needsAttention(next) ? attention_.frames : 0;
    render();
    return true;
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

void Ui::toolStart(){
    toolRunning_ = true;   // the gear picks the mark up on the next tick
}

void Ui::toolStop(){
    toolRunning_ = false;
}

void Ui::setSessions(std::string_view codes){
    char next[sizeof(sessions_)] = {};
    int n = 0;
    for (char c : codes){
        if (c == ' ') continue;
        if (n >= kMaxSessions) break;
        next[n++] = c;
    }
    if (n == sessionCount_ && std::strncmp(next, sessions_, sizeof(sessions_)) == 0){
        return;
    }
    std::memcpy(sessions_, next, sizeof(sessions_));
    sessionCount_ = n;
    if (isStatic(state_) && pulseLeft_ == 0){
        render();   // animated states pick the dots up on the next frame
    }
}

void Ui::noteCommand(){
    linkFrames_ = 0;
}

void Ui::ping(){
    linkArmed_ = true;
    linkFrames_ = 0;
}

void Ui::setTiltConfig(const TiltConfig& cfg){
    tiltCfg_ = cfg;
    tilt_.valid = false;   // next sample snaps to the new mapping
    render();
}

void Ui::feedAccel(float ax, float ay, float az){
    bool stale = updateTilt(tilt_, tiltCfg_, ax, ay, az);
    if (stale && pulseLeft_ == 0 && isStatic(state_)){
        render();
    }
}

void Ui::tick(bool screenOn){
    // Timers run whether or not the screen is on
    ++stateFrames_;
    ++linkFrames_;
    if (state_ == State::Processing || state_ == State::Compacting){
        ++workFrames_;
    }

    // Link mark appearing or disappearing on a static icon (the animated
    // ones redraw every frame anyway)
    bool lost = linkLost();
    if (lost != linkShown_){
        linkShown_ = lost;
        if (isStatic(state_) && pulseLeft_ == 0 && screenOn){
            render();
        }
    }

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
