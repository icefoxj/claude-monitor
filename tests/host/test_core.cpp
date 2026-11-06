// Host tests for the board-independent part of monitor-core (protocol and
// tilt filter): no ESP-IDF, no M5Unified, any C++17 compiler. Run them with
// tests/host/run.sh (Linux/macOS, CI) or tests/host/run.ps1 (Windows).
// No framework: a CHECK macro, one function per area, exit code = failures.

#include "monitor/orientation.h"
#include "monitor/protocol.h"
#include "monitor/screendiff.h"
#include "monitor/sounds.h"
#include "monitor/tilt.h"
#include "monitor/usage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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
    CHECK(parseCommand("view saver").arg == "saver");
    CHECK(parseCommand("view usage").arg == "usage");
    CHECK(parseCommand("usage none").kind == Command::Usage);
    CHECK(parseCommand("usage account\tage=12").arg == "account\tage=12");
    CHECK(parseCommand("theme anime").kind == Command::Theme);
    CHECK(parseCommand("theme anime").arg == "anime");
    CHECK(parseCommand("theme").kind == Command::Theme);
    CHECK(parseCommand("theme").arg.empty());
    {
        Theme t = Theme::Anime;
        CHECK(parseTheme("classic", t) && t == Theme::Classic);
        CHECK(parseTheme("anime", t) && t == Theme::Anime);
        CHECK(!parseTheme("nerv", t) && t == Theme::Anime);
        CHECK(!parseTheme("", t));
        CHECK(std::string(themeName(Theme::Anime)) == "anime");
        CHECK(std::string(themeName(Theme::Classic)) == "classic");
        CHECK(parseTheme("lcars", t) && t == Theme::Lcars);
        CHECK(std::string(themeName(Theme::Lcars)) == "lcars");
        CHECK(nextTheme(Theme::Classic) == Theme::Anime);
        CHECK(nextTheme(Theme::Anime) == Theme::Lcars);
        CHECK(nextTheme(Theme::Lcars) == Theme::Galactic);
        CHECK(nextTheme(Theme::Galactic) == Theme::Classic);
        CHECK(parseTheme("galactic", t) && t == Theme::Galactic);
        CHECK(std::string(themeName(Theme::Galactic)) == "galactic");
    }
    CHECK(kProtocolVersion == 9);

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

void testUsage(){
    Usage u;
    CHECK(parseUsage("account\tage=42\tlimit=Current session|25|2580|normal"
                     "\tlimit=This week|4.5|471600|\tlimit=This week (Fable)|0|471600|normal", u));
    CHECK(u.source == "account");
    CHECK(u.ageSec == 42);
    CHECK(u.limits.size() == 3);
    CHECK(u.limits[0].label == "Current session");
    CHECK(near(u.limits[0].percent, 25.0f));
    CHECK(u.limits[0].resetsIn == 2580);
    CHECK(u.limits[0].severity == "normal");
    CHECK(near(u.limits[1].percent, 4.5f));
    CHECK(u.limits[1].severity.empty());
    CHECK(u.limits[2].label == "This week (Fable)");

    // Optional reset and severity; a bad limit is skipped, not the line
    CHECK(parseUsage("statusline\tlimit=Week|101\tlimit=broken\tlimit=|5|1\tlimit=X|abc|1\tlimit=Y|7|-1|warning", u));
    CHECK(u.limits.size() == 2);
    CHECK(near(u.limits[0].percent, 101.0f) && u.limits[0].resetsIn == -1);
    CHECK(u.limits[1].label == "Y" && u.limits[1].resetsIn == -1 && u.limits[1].severity == "warning");

    // No data yet: a source and nothing else
    CHECK(parseUsage("none", u));
    CHECK(u.source == "none" && u.limits.empty() && u.ageSec == 0);

    // The week by app: label before the last ':', bad items skipped
    CHECK(parseUsage("account\tlimit=Week|4\tsplit=Chats:75|Cowork:15|Claude Code:10|bad|:3|X:y", u));
    CHECK(u.split.size() == 3);
    CHECK(u.split[0].label == "Chats" && near(u.split[0].percent, 75.0f));
    CHECK(u.split[2].label == "Claude Code" && near(u.split[2].percent, 10.0f));
    CHECK(parseUsage("account\tlimit=Week|4", u) && u.split.empty());
    CHECK(parseUsage("account\tsplit=a:1|b:2|c:3|d:4|e:5|f:6", u) && u.split.size() == Usage::kMaxShares);

    // At most kMaxLimits are kept; no source is an error
    CHECK(parseUsage("account\tlimit=a|1\tlimit=b|2\tlimit=c|3\tlimit=d|4\tlimit=e|5", u));
    CHECK(u.limits.size() == Usage::kMaxLimits);
    CHECK(!parseUsage("", u));

    // Protocol 8, the usage page: the host's clock, windows in limit
    // order, plan, extra usage, the week's history, projects
    CHECK(parseUsage("account\tage=0\tlimit=Current session|6|10800|normal\tlimit=This week|61|75599|normal"
                     "\tnow=1793700000|-10800\twindows=18000|604800\tplan=Max 5x|active|1771100000"
                     "\textra=on|12.40|50.00|USD\thistory=1793170800|3600|0,1,,3,x"
                     "\tproject=api|39.1|0.4\tproject=claude-monitor|1|\tproject=|3|1\tproject=bad|y", u));
    CHECK(u.nowUnix == 1793700000 && u.utcOffset == -10800);
    CHECK(u.limits.size() == 2 && u.limits[0].windowSec == 18000 && u.limits[1].windowSec == 604800);
    CHECK(u.plan.name == "Max 5x" && u.plan.status == "active" && u.plan.since == 1771100000);
    CHECK(u.extra.state == "on" && u.extra.used == "12.40" && u.extra.limit == "50.00" && u.extra.currency == "USD");
    CHECK(u.history.start == 1793170800 && u.history.step == 3600 && u.history.values.size() == 5);
    CHECK(u.history.values[0] == 0 && u.history.values[1] == 1 && u.history.values[2] == -1
          && u.history.values[3] == 3 && u.history.values[4] == -1);
    CHECK(u.projects.size() == 2);
    CHECK(u.projects[0].name == "api" && near(u.projects[0].week, 39.1f) && near(u.projects[0].today, 0.4f));
    CHECK(u.projects[1].name == "claude-monitor" && near(u.projects[1].week, 1.0f) && near(u.projects[1].today, 0.0f));
    CHECK(parseUsage("account\tlimit=a|1\twindows=5\textra=off", u));
    CHECK(u.limits[0].windowSec == 5 && u.extra.state == "off" && u.extra.used.empty() && u.nowUnix == 0);
    CHECK(parseUsage("account\textra=on|3.10|-|EUR\thistory=5|0|1,2", u));
    CHECK(u.extra.limit.empty() && u.extra.currency == "EUR" && u.history.values.empty());
    CHECK(parseUsage("account\tlimit=a|1", u) && u.limits[0].windowSec == 0 && u.plan.name.empty() && u.extra.state.empty());

    // Pace: 61 % with 7/8 of the week gone ends near 70 %
    UsageLimit l;
    l.percent = 61.0f; l.resetsIn = 75600; l.windowSec = 604800;
    UsagePace p = usagePace(l, 0);
    CHECK(p.known && p.valid && near(p.elapsed, 0.875f) && near(p.projected, 69.714f, 0.01f) && p.runsOutIn == -1);
    p = usagePace(l, 75600 + 100);   // past the reset: the whole window gone
    CHECK(p.known && near(p.elapsed, 1.0f) && near(p.projected, 61.0f));
    // 90 % halfway through a 5-hour window: 100 % 1000 s from now
    l.percent = 90.0f; l.resetsIn = 9000; l.windowSec = 18000;
    p = usagePace(l, 0);
    CHECK(p.valid && near(p.projected, 180.0f) && p.runsOutIn == 1000);
    p = usagePace(l, 500);           // the countdown follows the line's age
    CHECK(near(p.elapsed, 0.5278f, 0.001f));
    l.percent = 100.0f;
    CHECK(usagePace(l, 0).runsOutIn == 0);
    l.percent = 3.0f; l.resetsIn = 17500;   // 2.8 % of the window: no projection yet
    p = usagePace(l, 0);
    CHECK(p.known && !p.valid);
    l.windowSec = 0;
    CHECK(!usagePace(l, 0).known);
    l.windowSec = 18000; l.resetsIn = -1;
    CHECK(!usagePace(l, 0).known);

    // The host's clock: 2026-11-03 10:00 UTC, 07:00 at UTC-3, a Tuesday
    Usage c;
    c.nowUnix = 1793700000;
    c.utcOffset = -10800;
    char t[32];
    usageWhenText(c, 1793700000 + 2 * 3600, 0, t, sizeof(t));
    CHECK(std::string(t) == "09:00");
    usageWhenText(c, 1793775599, 0, t, sizeof(t));          // the weekly reset, Wed 06:59:59 UTC: to the nearest minute
    CHECK(std::string(t) == "Wed 04:00");
    usageWhenText(c, 1793775500, 0, t, sizeof(t));
    CHECK(std::string(t) == "Wed 03:58");
    usageWhenText(c, 1793700000 + 17 * 86400, 0, t, sizeof(t));
    CHECK(std::string(t) == "20 Nov");
    usageWhenText(c, 1793700000 + 14 * 3600, 3600 * 4, t, sizeof(t));   // 21:00 local, four hours after the line
    CHECK(std::string(t) == "21:00");
    usageDateText(c, 1771100000, t, sizeof(t));
    CHECK(std::string(t) == "14 Feb 2026");
    CHECK(usageWeekday(1793700000, -10800) == 2);
    CHECK(usageWeekday(1793664000, -10800) == 1);          // 00:00 UTC is still Monday at UTC-3
    CHECK(usageSecondsOfDay(1793664000, -10800) == 21 * 3600);
    CHECK(std::string(usageWeekdayName(3)) == "Wed");
    c.nowUnix = 0;                                          // no clock from the host
    usageWhenText(c, 1793700000, 0, t, sizeof(t));
    CHECK(t[0] == '\0');
    CHECK(usageLocalNow(c, 10) == 0);
}

void testSounds(){
    // The classic theme's notes are the board's tones: nothing rendered
    CHECK(renderAlert(Theme::Classic, Alert::Attention, 32000).empty());
    CHECK(renderAlert(Theme::Classic, Alert::Failure, 32000).empty());
    CHECK(renderAlert(Theme::Lcars, Alert::Attention, 100).empty());
    CHECK(alertFor(State::Error) == Alert::Failure);
    CHECK(alertFor(State::WaitingUser) == Alert::Attention);

    const Theme themes[] = {Theme::Anime, Theme::Lcars, Theme::Galactic};
    const Alert alerts[] = {Alert::Attention, Alert::Failure};
    for (Theme t : themes){
        for (Alert a : alerts){
            const std::vector<int16_t> pcm = renderAlert(t, a, 32000);
            const double sec = static_cast<double>(pcm.size()) / 32000.0;
            CHECK(sec > 0.2 && sec < 2.5);
            int peak = 0;
            for (int16_t v : pcm) peak = std::max(peak, std::abs(static_cast<int>(v)));
            CHECK(peak > 25000 && peak <= 26214);   // 80 % of full scale
            CHECK(!pcm.empty() && pcm.front() == 0 && pcm.back() == 0);
            // the same sound at half the rate: half the samples
            const size_t half = renderAlert(t, a, 16000).size();
            CHECK(half + 2 >= pcm.size() / 2 && half <= pcm.size() / 2 + 2);
        }
    }
    // the red alert is the longest: three whoops, about two seconds
    const size_t klaxon = renderAlert(Theme::Lcars, Alert::Attention, 32000).size();
    CHECK(klaxon > 32000 * 19 / 10 && klaxon < 32000 * 21 / 10);
}

// A WAV file in memory: `extra` adds an odd-sized chunk before the data
std::vector<uint8_t> makeWav(uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits,
                             const std::vector<uint8_t>& data, bool extra = false, uint32_t claimed = 0){
    std::vector<uint8_t> f;
    auto tag = [&](const char* s) { f.insert(f.end(), s, s + 4); };
    auto u16 = [&](uint32_t v) { f.push_back(static_cast<uint8_t>(v)); f.push_back(static_cast<uint8_t>(v >> 8)); };
    auto u32 = [&](uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
    tag("RIFF"); u32(0); tag("WAVE");
    tag("fmt "); u32(16); u16(format); u16(channels); u32(rate);
    u32(rate * channels * bits / 8); u16(static_cast<uint16_t>(channels * bits / 8)); u16(bits);
    if (extra){ tag("LIST"); u32(3); f.push_back('a'); f.push_back('b'); f.push_back('c'); f.push_back(0); }
    tag("data"); u32(claimed ? claimed : static_cast<uint32_t>(data.size()));
    f.insert(f.end(), data.begin(), data.end());
    return f;
}

void testWav(){
    std::vector<int16_t> pcm;
    uint32_t rate = 0;
    // 16-bit mono, little-endian: 1000, -1000, 32767
    std::vector<uint8_t> d16 = {0xE8, 0x03, 0x18, 0xFC, 0xFF, 0x7F};
    std::vector<uint8_t> f = makeWav(1, 1, 44100, 16, d16);
    CHECK(decodeWav(f.data(), f.size(), pcm, rate));
    CHECK(rate == 44100 && pcm.size() == 3 && pcm[0] == 1000 && pcm[1] == -1000 && pcm[2] == 32767);
    // read byte by byte: the same file one byte off its alignment
    std::vector<uint8_t> shifted(f.size() + 1);
    std::copy(f.begin(), f.end(), shifted.begin() + 1);
    CHECK(decodeWav(shifted.data() + 1, f.size(), pcm, rate) && pcm.size() == 3 && pcm[1] == -1000);
    // stereo is averaged, an odd chunk before the data is skipped with its pad byte
    f = makeWav(1, 2, 22050, 16, {0xE8, 0x03, 0xD0, 0x07, 0x00, 0x00, 0x18, 0xFC}, true);
    CHECK(decodeWav(f.data(), f.size(), pcm, rate));
    CHECK(rate == 22050 && pcm.size() == 2 && pcm[0] == 1500 && pcm[1] == -500);
    // 8-bit is unsigned around 128
    f = makeWav(1, 1, 8000, 8, {128, 255, 0});
    CHECK(decodeWav(f.data(), f.size(), pcm, rate));
    CHECK(pcm.size() == 3 && pcm[0] == 0 && pcm[1] == 127 * 256 && pcm[2] == -128 * 256);
    // a data chunk claiming more than is there (a streamed file): what is there, whole frames
    f = makeWav(1, 1, 16000, 16, {1, 0, 2, 0, 3}, false, 0xFFFFFFFF);
    CHECK(decodeWav(f.data(), f.size(), pcm, rate) && pcm.size() == 2 && pcm[1] == 2);
    // extensible format with PCM inside
    f = makeWav(1, 1, 16000, 16, d16);
    f[16] = 40;                                   // fmt chunk of 40 bytes
    f[20] = 0xFE; f[21] = 0xFF;                   // WAVE_FORMAT_EXTENSIBLE
    std::vector<uint8_t> ext(24, 0);
    ext[0] = 22; ext[8] = 1;                      // cbSize, subformat PCM
    f.insert(f.begin() + 36, ext.begin(), ext.end());
    CHECK(decodeWav(f.data(), f.size(), pcm, rate) && pcm.size() == 3 && pcm[0] == 1000);
    // refused: compressed, 24-bit, five channels, not a WAV, empty data
    f = makeWav(3, 1, 16000, 32, {0, 0, 0, 0});
    CHECK(!decodeWav(f.data(), f.size(), pcm, rate) && pcm.empty());
    f = makeWav(1, 1, 16000, 24, {0, 0, 0});
    CHECK(!decodeWav(f.data(), f.size(), pcm, rate));
    f = makeWav(1, 5, 16000, 16, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(!decodeWav(f.data(), f.size(), pcm, rate));
    f = makeWav(1, 1, 16000, 16, {});
    CHECK(!decodeWav(f.data(), f.size(), pcm, rate));
    const uint8_t junk[] = "ID3 this is an mp3";
    CHECK(!decodeWav(junk, sizeof(junk), pcm, rate) && rate == 0);
    CHECK(!decodeWav(nullptr, 0, pcm, rate));
}

// The changed parts of a frame: none when equal, a tight box per run of
// differing rows, cut at maxRows, inside the region only
void testScreenDiff(){
    const int W = 10, H = 8;
    std::vector<uint16_t> a(W * H, 0), b(W * H, 0);
    const DiffRect all{0, 0, W, H};
    auto same = [](const DiffRect& r, int x, int y, int w, int h) {
        return r.x == x && r.y == y && r.w == w && r.h == h;
    };
    CHECK(changedRects(a.data(), b.data(), W, H, all).empty());

    a[2 * W + 3] = 0xF800;                       // one pixel
    auto r = changedRects(a.data(), b.data(), W, H, all);
    CHECK(r.size() == 1 && same(r[0], 3, 2, 1, 1));

    a[3 * W + 7] = 0x07E0;                       // the next row, further right: one box
    r = changedRects(a.data(), b.data(), W, H, all);
    CHECK(r.size() == 1 && same(r[0], 3, 2, 5, 2));

    a[6 * W + 0] = 0x001F;                       // an unchanged row between: two boxes
    r = changedRects(a.data(), b.data(), W, H, all);
    CHECK(r.size() == 2 && same(r[0], 3, 2, 5, 2) && same(r[1], 0, 6, 1, 1));

    a[7 * W + 9] = 1;                            // the last pixel, at the image's edge
    r = changedRects(a.data(), b.data(), W, H, all);
    CHECK(r.size() == 2 && same(r[1], 0, 6, 10, 2));

    // A region sees only what is inside it, clipped to the image
    r = changedRects(a.data(), b.data(), W, H, DiffRect{4, 0, 20, 5});
    CHECK(r.size() == 1 && same(r[0], 7, 3, 1, 1));
    r = changedRects(a.data(), b.data(), W, H, DiffRect{0, 0, 3, 8});
    CHECK(r.size() == 1 && same(r[0], 0, 6, 1, 1));
    CHECK(changedRects(a.data(), b.data(), W, H, DiffRect{0, 0, 3, 5}).empty());
    CHECK(changedRects(a.data(), b.data(), W, H, DiffRect{20, 20, 5, 5}).empty());

    // A tall run is cut every maxRows rows
    std::vector<uint16_t> c(W * H, 0);
    for (int y = 0; y < 5; ++y) c[y * W + 4] = 2;
    r = changedRects(c.data(), b.data(), W, H, all, 2);
    CHECK(r.size() == 3 && same(r[0], 4, 0, 1, 2) && same(r[1], 4, 2, 1, 2) && same(r[2], 4, 4, 1, 1));

    CHECK(changedRects(nullptr, b.data(), W, H, all).empty());
    CHECK(changedRects(a.data(), b.data(), W, H, all, 0).empty());
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
    testUsage();
    testSounds();
    testWav();
    testScreenDiff();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
