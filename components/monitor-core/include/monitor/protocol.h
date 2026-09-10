#pragma once

#include <cstddef>
#include <string>
#include <string_view>

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
//   "status"         -> device replies with one STATUS line (debug/calibration)

namespace monitor {

enum class State { Idle, Processing, WaitingUser, Question, Error, Paused, Compacting, Off };

// Protocol word for a state ("processing", "idle", ...)
const char* stateName(State s);

// States that mean "look at the terminal": they get an entry pulse
bool needsAttention(State s);

// States drawn once and then only redrawn when the tilt changes
bool isStatic(State s);

// States redrawn every frame (spinning gears, the running hourglass)
bool isAnimated(State s);

enum class Command { Unknown, SetState, SubagentStart, SubagentStop, Status };

struct ParsedCommand {
    Command kind = Command::Unknown;
    State state = State::Off;   // meaningful when kind == SetState
};

// Maps one complete, trimmed line to a command. Unknown lines are ignored.
ParsedCommand parseCommand(std::string_view line);

// Accumulates bytes into \n-terminated lines. Strips trailing \r and
// spaces ("processing\r\n" from some senders) and discards anything longer
// than maxLen as serial garbage.
class LineParser {
public:
    explicit LineParser(size_t maxLen = 64) : maxLen_(maxLen) {}

    // Feed one byte; returns true when `out` holds a complete line
    bool feed(char c, std::string& out);

private:
    std::string line_;
    size_t maxLen_;
};

}  // namespace monitor
