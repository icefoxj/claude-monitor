// Host tests for the board-independent part of monitor-core (protocol and
// tilt filter): no ESP-IDF, no M5Unified, any C++17 compiler. Run them with
// tests/host/run.sh (Linux/macOS, CI) or tests/host/run.ps1 (Windows).
// No framework: a CHECK macro, one function per area, exit code = failures.

#include "monitor/orientation.h"
#include "monitor/protocol.h"
#include "monitor/tilt.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int failures = 0;
int checks   = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

constexpr float kPi = 3.14159265f;

bool near(float a, float b, float eps = 1e-3f){
    return std::fabs(a - b) < eps;
}

using namespace monitor;

// Feeds a whole string and returns the lines the parser produced
std::string feedAll(LineParser& p, const std::string& bytes, int& lines){
    std::string out, last;
    lines = 0;
    for (char c : bytes){
        if (p.feed(c, out)){
            ++lines;
            last = out;
        }
    }
    return last;
}

void testLineParser(){
    LineParser p;
    int lines = 0;

    CHECK(feedAll(p, "idle\n", lines) == "idle");
    CHECK(lines == 1);

    // \r\n and trailing spaces are stripped
    CHECK(feedAll(p, "processing\r\n", lines) == "processing");
    CHECK(feedAll(p, "waiting_user  \r\n", lines) == "waiting_user");

    // Several lines in one buffer: every one is reported
    CHECK(feedAll(p, "a\nb\nc\n", lines) == "c");
    CHECK(lines == 3);

    // An empty line is still a line
    CHECK(feedAll(p, "\n", lines) == "");
    CHECK(lines == 1);

    // Anything over 64 bytes is dropped whole, newline included, and the
    // next line is parsed normally
    std::string longLine(65, 'x');
    feedAll(p, longLine + "\n", lines);
    CHECK(lines == 0);
    CHECK(feedAll(p, std::string(64, 'y') + "\n", lines) == std::string(64, 'y'));
    CHECK(lines == 1);
    feedAll(p, std::string(3000, 'e') + "\nidle\n", lines);   // an event line meant for a bigger board
    CHECK(lines == 1);

    // A board that shows events uses a bigger limit
    LineParser big(4096);
    CHECK(feedAll(big, std::string(3000, 'e') + "\n", lines) == std::string(3000, 'e'));

    // Bytes before a newline are kept across feeds
    std::string out;
    CHECK(!p.feed('o', out));
    CHECK(!p.feed('f', out));
    CHECK(!p.feed('f', out));
    CHECK(p.feed('\n', out));
    CHECK(out == "off");
}

void testStates(){
    const State all[] = { State::Idle, State::Processing, State::WaitingUser, State::Question,
                          State::Error, State::Paused, State::Compacting, State::Off };
    for (State s : all){
        ParsedCommand c = parseCommand(stateName(s));
        CHECK(c.kind == Command::SetState);
        CHECK(c.state == s);
    }
    CHECK(std::string(stateName(State::WaitingUser)) == "waiting_user");

    CHECK(sessionCode(State::Processing) == 'p');
    CHECK(sessionCode(State::WaitingUser) == 'w');
    CHECK(sessionCode(State::Question) == 'q');
    CHECK(sessionCode(State::Error) == 'e');
    CHECK(sessionCode(State::Paused) == 'h');
    CHECK(sessionCode(State::Compacting) == 'c');
    CHECK(sessionCode(State::Idle) == 'i');
    CHECK(sessionCode(State::Off) == '-');

    // The three classifications partition the states as documented
    CHECK(needsAttention(State::WaitingUser) && needsAttention(State::Question) && needsAttention(State::Error));
    CHECK(!needsAttention(State::Idle) && !needsAttention(State::Processing) && !needsAttention(State::Paused));
    CHECK(isStatic(State::Idle) && isStatic(State::WaitingUser) && isStatic(State::Question) && isStatic(State::Error));
    CHECK(isAnimated(State::Processing) && isAnimated(State::Compacting) && isAnimated(State::Paused));
    for (State s : all){
        CHECK(!(isStatic(s) && isAnimated(s)));
        CHECK(isStatic(s) || isAnimated(s) || s == State::Off);
    }
}

void testParseCommand(){
    CHECK(parseCommand("status").kind == Command::Status);
    CHECK(parseCommand("version").kind == Command::Version);
    CHECK(parseCommand("version").arg == "");
    CHECK(parseCommand("tool_start").kind == Command::ToolStart);
    CHECK(parseCommand("tool_stop").kind == Command::ToolStop);
    CHECK(parseCommand("screenshot").kind == Command::Screenshot);
    CHECK(parseCommand("view grid").kind == Command::View);
    CHECK(parseCommand("view grid").arg == "grid");
    CHECK(parseCommand("view detail 33db5bf1-e7eb").arg == "detail 33db5bf1-e7eb");
    CHECK(kProtocolVersion == 6);

    ParsedCommand se = parseCommand("session 33db5bf1-e7eb\tlabel=claude-monitor\tstate=waiting_user\tsubagents=0\ttool=1");
    CHECK(se.kind == Command::Session);
    HookEvent sl;
    CHECK(parseEventLine(se.arg, sl));
    CHECK(sl.name == "33db5bf1-e7eb");
    State st;
    CHECK(parseState(sl.find("state"), st) && st == State::WaitingUser);
    CHECK(std::string(sl.find("tool")) == "1");
    CHECK(parseCommand("session_end 33db5bf1-e7eb").kind == Command::SessionEnd);
    CHECK(parseCommand("session_end 33db5bf1-e7eb").arg == "33db5bf1-e7eb");
    CHECK(parseCommand("session_clear").kind == Command::SessionClear);
    CHECK(!parseState("bogus", st));
    CHECK(parseState("idle", st) && st == State::Idle);

    ParsedCommand ev = parseCommand("event PostToolUse\tsummary=PostToolUse/Bash\tsession_id=abc");
    CHECK(ev.kind == Command::Event);
    CHECK(ev.arg == "PostToolUse\tsummary=PostToolUse/Bash\tsession_id=abc");
    CHECK(parseCommand("ping").kind == Command::Ping);
    CHECK(parseCommand("subagent_start").kind == Command::SubagentStart);
    CHECK(parseCommand("subagent_stop").kind == Command::SubagentStop);

    ParsedCommand s = parseCommand("sessions pwi");
    CHECK(s.kind == Command::Sessions);
    CHECK(s.arg == "pwi");
    CHECK(parseCommand("sessions").arg == "");
    CHECK(parseCommand("sessions   pw").arg == "pw");

    ParsedCommand c = parseCommand("calibrate rot=1 sign=-1");
    CHECK(c.kind == Command::Calibrate);
    CHECK(c.arg == "rot=1 sign=-1");

    CHECK(parseCommand("").kind == Command::Unknown);
    CHECK(parseCommand("PROCESSING").kind == Command::Unknown);
    CHECK(parseCommand("processing_now").kind == Command::Unknown);
    CHECK(parseCommand("STATUS state=idle").kind == Command::Unknown);   // our own reply echoed back
    CHECK(parseCommand("processing extra").kind == Command::SetState);   // arguments after a state are ignored
}

void testCalibration(){
    CalibrationRequest r;

    CHECK(parseCalibration("reset", r));
    CHECK(r.reset && !r.hasRotation && !r.hasSign && !r.hasOffset);

    CHECK(parseCalibration("rot=1 sign=-1 offset=0", r));
    CHECK(!r.reset);
    CHECK(r.hasRotation && r.rotation == 1);
    CHECK(r.hasSign && r.sign == -1);
    CHECK(r.hasOffset && near(r.offsetDeg, 0.0f));

    CHECK(parseCalibration("rot=3", r));
    CHECK(r.hasRotation && r.rotation == 3 && !r.hasSign && !r.hasOffset);

    CHECK(parseCalibration("offset=-12.5", r));
    CHECK(r.hasOffset && near(r.offsetDeg, -12.5f));

    CHECK(parseCalibration("sign=1", r));
    CHECK(r.hasSign && r.sign == 1);

    CHECK(parseCalibration("  rot=2   sign=1 ", r));   // stray spaces
    CHECK(r.rotation == 2 && r.sign == 1);

    CHECK(!parseCalibration("", r));
    CHECK(!parseCalibration("rot=4", r));
    CHECK(!parseCalibration("rot=-1", r));
    CHECK(!parseCalibration("sign=0", r));
    CHECK(!parseCalibration("sign=2", r));
    CHECK(!parseCalibration("offset=400", r));
    CHECK(!parseCalibration("offset=abc", r));
    CHECK(!parseCalibration("rot=1x", r));
    CHECK(!parseCalibration("rot=", r));
    CHECK(!parseCalibration("rot", r));
    CHECK(!parseCalibration("foo=1", r));
    CHECK(!parseCalibration("rot=1 foo=1", r));   // one bad field rejects the whole line
}

void testEventLine(){
    HookEvent e;
    CHECK(parseEventLine("PostToolUse\tsummary=PostToolUse/Bash\tsession_id=33db5bf1\ttool_input.command=git status\tflag", e));
    CHECK(e.name == "PostToolUse");
    CHECK(e.fields.size() == 4);
    CHECK(e.fields[0].key == "summary" && e.fields[0].value == "PostToolUse/Bash");
    CHECK(e.fields[2].key == "tool_input.command" && e.fields[2].value == "git status");
    CHECK(e.fields[3].key == "flag" && e.fields[3].value == "");
    CHECK(std::string(e.find("session_id")) == "33db5bf1");
    CHECK(e.find("missing") == nullptr);

    // Values keep their own '=' signs and spaces
    CHECK(parseEventLine("UserPromptSubmit\tprompt=a = b and c", e));
    CHECK(e.fields[0].value == "a = b and c");

    // Name only, name with stray spaces, empty tokens
    CHECK(parseEventLine("Stop", e));
    CHECK(e.name == "Stop" && e.fields.empty());
    CHECK(parseEventLine(" Stop \t\tx=1\t", e));
    CHECK(e.name == "Stop" && e.fields.size() == 1);

    // No name: rejected
    CHECK(!parseEventLine("", e));
    CHECK(!parseEventLine("\tx=1", e));
}

void testWrapAngle(){
    CHECK(near(wrapAngle(0.0f), 0.0f));
    CHECK(near(wrapAngle(kPi / 2), kPi / 2));
    CHECK(near(std::fabs(wrapAngle(3 * kPi)), kPi));
    CHECK(near(wrapAngle(2 * kPi + 0.5f), 0.5f));
    CHECK(near(wrapAngle(-2 * kPi - 0.5f), -0.5f));
}

void testTilt(){
    TiltConfig cfg;   // the AtomS3R defaults: sign -1, offset 0
    Tilt t;

    // Lying flat: no reading is taken, nothing to redraw
    CHECK(!updateTilt(t, cfg, 0.0f, 0.0f, 1.0f));
    CHECK(!t.valid);
    CHECK(!updateTilt(t, cfg, 0.1f, 0.1f, -0.98f));
    CHECK(!t.valid);

    // Too little in-plane gravity to be trusted: held as well
    CHECK(!updateTilt(t, cfg, 0.2f, 0.2f, 0.5f));
    CHECK(!t.valid);

    // First real reading snaps, no easing from zero
    updateTilt(t, cfg, 1.0f, 0.0f, 0.0f);
    CHECK(t.valid);
    CHECK(near(t.angle, 0.0f));

    // A 90-degree change is followed with the low-pass factor, and reported stale
    bool stale = updateTilt(t, cfg, 0.0f, 1.0f, 0.0f);
    CHECK(stale);
    CHECK(near(t.angle, -kPi / 2 * cfg.smoothing));
    for (int i = 0; i < 200; i++){
        updateTilt(t, cfg, 0.0f, 1.0f, 0.0f);
    }
    CHECK(near(t.angle, -kPi / 2, 1e-2f));

    // Once drawn at that angle, it is no longer stale; a tiny move is under the threshold
    t.drawn = t.angle;
    CHECK(!updateTilt(t, cfg, 0.0f, 1.0f, 0.0f));
    CHECK(!updateTilt(t, cfg, 0.001f, 1.0f, 0.0f));

    // Sign and offset are applied
    TiltConfig cfg2;
    cfg2.angleSign   = 1.0f;
    cfg2.angleOffset = kPi / 2;
    Tilt t2;
    updateTilt(t2, cfg2, 0.0f, 1.0f, 0.0f);   // atan2 = pi/2, plus pi/2 = pi
    CHECK(near(std::fabs(t2.angle), kPi, 1e-3f));

    // The filter is wrap-aware: from just below +pi towards just above -pi
    // it moves the short way, not back through zero
    Tilt t3;
    t3.valid = true;
    t3.angle = kPi - 0.1f;
    float target = -kPi + 0.1f;               // sign -1: target = -atan2(ay, ax)
    updateTilt(t3, cfg, std::cos(-target), std::sin(-target), 0.0f);
    CHECK(near(t3.angle, kPi - 0.1f + 0.2f * cfg.smoothing, 1e-3f));
}

// Feeds the same sample n times; returns how many times the rotation changed
int feedSamples(Orientation& o, float ax, float ay, float az, int n){
    int changes = 0;
    for (int i = 0; i < n; i++){
        if (o.update(ax, ay, az)) changes++;
    }
    return changes;
}

void testOrientation(){
    OrientationConfig cfg;   // offset 0, sign +1, hysteresis 12, 15 stable frames
    Orientation o(cfg);

    // Flat on the desk: nothing known yet
    CHECK(!o.update(0.0f, 0.0f, 1.0f));
    CHECK(!o.valid());
    CHECK(o.rotation() == -1);

    // First real sample snaps: +x down = sector 0 = rotation 0
    CHECK(o.update(1.0f, 0.0f, 0.1f));
    CHECK(o.valid() && o.sector() == 0 && o.rotation() == 0);

    // Same sector again: no change
    CHECK(feedSamples(o, 0.9f, 0.2f, 0.1f, 5) == 0);

    // Jitter just past 45 degrees (50) is inside the hysteresis band: held
    float a50 = 50.0f * kPi / 180.0f;
    CHECK(feedSamples(o, std::cos(a50), std::sin(a50), 0.0f, 40) == 0);
    CHECK(o.sector() == 0);

    // A real turn to +y (90 degrees) takes stableFrames samples, then one change
    CHECK(feedSamples(o, 0.0f, 1.0f, 0.0f, cfg.stableFrames - 1) == 0);
    CHECK(o.sector() == 0);
    CHECK(feedSamples(o, 0.0f, 1.0f, 0.0f, 1) == 1);
    CHECK(o.sector() == 1 && o.rotation() == 1);

    // Going flat holds the last rotation
    CHECK(feedSamples(o, 0.0f, 0.0f, -1.0f, 10) == 0);
    CHECK(o.rotation() == 1);

    // Opposite side: -x = sector 2
    CHECK(feedSamples(o, -1.0f, 0.0f, 0.0f, cfg.stableFrames) == 1);
    CHECK(o.sector() == 2 && o.rotation() == 2);

    // -y = sector 3 -> rotation 3; then back to +x wraps to 0
    CHECK(feedSamples(o, 0.0f, -1.0f, 0.0f, cfg.stableFrames) == 1);
    CHECK(o.rotation() == 3);
    CHECK(feedSamples(o, 1.0f, 0.0f, 0.0f, cfg.stableFrames) == 1);
    CHECK(o.rotation() == 0);

    // Sign and offset: with sign -1 and offset 2, sector 1 shows (2 - 1) = 1 and sector 3 shows 3
    OrientationConfig c2;
    c2.offset = 2;
    c2.sign   = -1;
    Orientation o2(c2);
    o2.update(0.0f, 1.0f, 0.0f);
    CHECK(o2.sector() == 1 && o2.rotation() == 1);
    feedSamples(o2, 0.0f, -1.0f, 0.0f, c2.stableFrames);
    CHECK(o2.sector() == 3 && o2.rotation() == 3);
    feedSamples(o2, -1.0f, 0.0f, 0.0f, c2.stableFrames);
    CHECK(o2.sector() == 2 && o2.rotation() == 0);

    // offsetFor: "this sector must show rotation N" -> the offset to store
    CHECK(o2.offsetFor(1, -1) == 3);   // sector 2, sign -1: 1 + 2 = 3
    CHECK(o2.offsetFor(1, 1) == 3);    // sector 2, sign +1: 1 - 2 = -1 -> 3
    Orientation o3;
    CHECK(o3.offsetFor(2, 1) == 2);    // no sample yet: the rotation itself

    // setConfig recomputes at once
    OrientationConfig c3 = o2.config();
    c3.offset = o2.offsetFor(1, c3.sign);
    o2.setConfig(c3);
    CHECK(o2.rotation() == 1);
}

}  // namespace

int main(){
    testLineParser();
    testStates();
    testParseCommand();
    testCalibration();
    testEventLine();
    testWrapAngle();
    testTilt();
    testOrientation();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
