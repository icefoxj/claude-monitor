// Host tests for the board-independent part of monitor-core (protocol and
// tilt filter): no ESP-IDF, no M5Unified, any C++17 compiler. Run them with
// tests/host/run.sh (Linux/macOS, CI) or tests/host/run.ps1 (Windows).
// No framework: a CHECK macro, one function per area, exit code = failures.

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

    // Anything over 64 bytes is dropped as garbage
    std::string longLine(65, 'x');
    CHECK(feedAll(p, longLine + "\n", lines) == "");
    CHECK(feedAll(p, std::string(64, 'y') + "\n", lines) == std::string(64, 'y'));

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
    CHECK(kProtocolVersion == 2);
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

}  // namespace

int main(){
    testLineParser();
    testStates();
    testParseCommand();
    testCalibration();
    testWrapAngle();
    testTilt();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
