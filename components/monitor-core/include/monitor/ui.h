#pragma once

#include <string_view>

#include "monitor/icons.h"
#include "monitor/protocol.h"
#include "monitor/tilt.h"

namespace monitor {

// Entry pulse of the "look at the terminal" signs: three breaths over ~2 s
// so the change catches the eye from across the desk
struct AttentionConfig {
    int   frames = 60;     // total length in frames (~2 s at 30 fps)
    int   period = 20;     // frames per breath
    float amp    = 0.10f;  // +10 % size at the peak
};

// State machine plus animation bookkeeping, board-independent. Drive it at
// ~30 fps: feedAccel (if there is an IMU), noteCommand / apply / subagent* /
// setSessions / ping for each line received, then tick once per frame.
class Ui {
public:
    Ui(Icons& icons, const TiltConfig& tiltCfg, const AttentionConfig& attention = {});

    // State change: redraws only when the state differs (and returns true
    // then), starts the entry pulse for the "look at the terminal" signs,
    // resets the subagent count and the work timer when the turn is over
    // (idle, off, error, paused)
    bool apply(State next);

    // Subagent bookkeeping: while the count is above zero the processing
    // gear is drawn with a satellite. The count never goes negative.
    void subagentStart();
    void subagentStop();

    // Live Claude Code sessions, one code letter each (see protocol.h); the
    // dots are drawn when there are two or more. Redraws on change.
    void setSessions(std::string_view codes);

    // Any complete line from the host: feeds the link watchdog
    void noteCommand();

    // Heartbeat: arms the watchdog. Until the first ping the link is never
    // reported lost, so a host that does not send pings still works.
    void ping();

    // Runtime calibration: replaces the tilt mapping and redraws
    void setTiltConfig(const TiltConfig& cfg);

    // One accelerometer sample (g). Redraws the static icon once the smoothed
    // angle has moved enough, unless a pulse is running (it redraws anyway)
    void feedAccel(float ax, float ay, float az);

    // Per-frame work: timers, gear spin, pulse step, link watchdog. With the
    // screen dark nothing is drawn and a pending pulse is dropped; the
    // timers still run.
    void tick(bool screenOn);

    State state() const { return state_; }
    int subagents() const { return subagents_; }
    const Tilt& tilt() const { return tilt_; }
    const char* sessions() const { return sessions_; }

    bool linkArmed() const { return linkArmed_; }
    bool linkLost() const;
    int framesInState() const { return stateFrames_; }        // since the last state change
    int framesSinceCommand() const { return linkFrames_; }    // since the last line from the host
    int workFrames() const { return workFrames_; }            // processing/compacting time this turn

private:
    void render(float scale = 1.0f);
    void overlays();
    Hourglass hourglass() const;   // phase of the hourglass for the current frame

    Icons& icons_;
    TiltConfig tiltCfg_;
    AttentionConfig attention_;

    State state_     = State::Off;
    float gearAngle_ = 0.0f;
    int   animFrame_ = 0;   // frames since the animated state was entered
    Tilt  tilt_;
    int   pulseLeft_ = 0;   // pulse frames left, 0 = none
    int   subagents_ = 0;

    int   stateFrames_ = 0;
    int   workFrames_  = 0;
    int   linkFrames_  = 0;
    bool  linkArmed_   = false;
    bool  linkShown_   = false;   // the lost-link mark is on screen

    char  sessions_[9] = {};      // up to 8 codes, NUL-terminated
    int   sessionCount_ = 0;
};

}  // namespace monitor
