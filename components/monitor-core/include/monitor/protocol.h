#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Serial protocol shared by every board: one command per line, terminated
// by \n. The transport (USB Serial/JTAG, CDC, ...) is the board's business.
//   "processing"     -> spinning yellow gear (Claude processing)
//   "waiting_user"   -> red sign with an exclamation mark (waiting for a permission)
//   "question"       -> blue sign with a question mark (Claude asked you something)
//   "error"          -> red circle with a cross (the turn ended with an API error)
//   "paused"         -> amber hourglass (waiting for a usage limit to reset)
//   "compacting"     -> spinning grey gear (compacting context, back soon)
//   "idle"           -> green circle with check (idle)
//   "off"            -> screen off
//   "subagent_start" -> one more subagent running: the gear grows a satellite
//   "subagent_stop"  -> one subagent finished
//   "tool_start"     -> a tool process is running under the session: blue dot
//                     on the band (the host sees the process, so this also
//                     means a permission was approved)
//   "tool_stop"      -> no tool process running any more
//   "sessions <codes>" -> one letter per live Claude Code session, most urgent
//                     first (p processing, w waiting_user, q question, e error,
//                     h paused, c compacting, i idle); no codes = none
//   "ping"           -> heartbeat from the host: arms the link watchdog, and
//                     from then on a minute without any line = link lost
//   "calibrate rot=<0-3> sign=<1|-1> offset=<degrees>" -> orientation
//                     calibration, any subset of fields, stored by the board
//   "calibrate reset" -> back to the compiled-in calibration
//   "status"         -> device replies with one STATUS line (debug/calibration)
//   "version"        -> device replies with one VERSION line: the board the
//                     firmware was built for, the model M5Unified detected,
//                     chip, flash, firmware / ESP-IDF / M5Unified versions,
//                     protocol version, features, build time, ELF SHA,
//                     uptime, reset reason
//   "event <name>\t<key>=<value>\t<key>=<value>..." -> one Claude Code hook
//                     event with its fields, tab-separated, values already
//                     trimmed by the host. The host sends these only to a
//                     board whose VERSION lists "events" in features=; the
//                     others never see them (a line can be a few KB)
//   "session <id>\tlabel=<name>\tstate=<state>\tsubagents=<n>\ttool=<0|1>\tproject=..."
//                  -> one live Claude Code session, for boards that list
//                     "sessions" in features= and show one tile per session;
//                     sent whenever something in it changes
//   "session_end <id>" -> that session is gone
//   "session_clear"    -> forget every session (the host re-sends them)

namespace monitor {

// Bumped when words are added: 1 = the 1.0/1.1 set (states, subagents,
// status); 2 = sessions, ping, calibrate, version; 3 = event;
// 4 = tool_start/stop; 5 = session, session_end, session_clear
constexpr int kProtocolVersion = 5;

enum class State { Idle, Processing, WaitingUser, Question, Error, Paused, Compacting, Off };

// Protocol word for a state ("processing", "idle", ...)
const char* stateName(State s);

// The reverse: false when the word is not a state
bool parseState(std::string_view name, State& out);

// One-letter code used in the "sessions" command ('p', 'w', ...; '-' for off)
char sessionCode(State s);

// States that mean "look at the terminal": they get an entry pulse
bool needsAttention(State s);

// States drawn once and then only redrawn when the tilt changes
bool isStatic(State s);

// States redrawn every frame (spinning gears, the running hourglass)
bool isAnimated(State s);

enum class Command { Unknown, SetState, SubagentStart, SubagentStop, ToolStart, ToolStop,
                     Status, Version, Ping, Sessions, Calibrate, Event,
                     Session, SessionEnd, SessionClear };

struct ParsedCommand {
    Command kind = Command::Unknown;
    State state = State::Off;   // meaningful when kind == SetState
    std::string arg;            // the rest of the line for Sessions, Calibrate, Event, Session, SessionEnd
};

// One field of a hook event, as the host sent it
struct HookField {
    std::string key;
    std::string value;
};

// One Claude Code hook event: its name and every field the host forwarded,
// in the order the hook JSON had them (nested objects flattened one level
// by the host, "tool_input.command")
struct HookEvent {
    std::string name;
    std::vector<HookField> fields;

    // Value of a field, or nullptr
    const char* find(std::string_view key) const;
};

// Parses the argument of "event": "<name>\t<key>=<value>\t...". Empty
// tokens are skipped, a token without '=' becomes a key with an empty
// value. Returns false when there is no name.
bool parseEventLine(std::string_view arg, HookEvent& out);

// Maps one complete, trimmed line to a command. Unknown lines are ignored.
ParsedCommand parseCommand(std::string_view line);

// Fields of a "calibrate" command; each one is optional
struct CalibrationRequest {
    bool  reset       = false;
    bool  hasRotation = false;
    int   rotation    = 0;      // absolute display rotation, 0..3
    bool  hasSign     = false;
    int   sign        = -1;     // +1 or -1
    bool  hasOffset   = false;
    float offsetDeg   = 0.0f;   // -360..360
};

// Parses the argument of "calibrate": "reset" or "key=value" pairs. Returns
// false on an unknown key, a value out of range, or no field at all.
bool parseCalibration(std::string_view args, CalibrationRequest& out);

// Accumulates bytes into \n-terminated lines. Strips trailing \r and
// spaces ("processing\r\n" from some senders). A line longer than maxLen
// is dropped whole, up to and including its newline, so serial garbage or
// a command meant for a bigger board cannot leave a tail behind.
class LineParser {
public:
    explicit LineParser(size_t maxLen = 64) : maxLen_(maxLen) {}

    // Feed one byte; returns true when `out` holds a complete line
    bool feed(char c, std::string& out);

private:
    std::string line_;
    size_t maxLen_;
    bool overflow_ = false;
};

}  // namespace monitor
