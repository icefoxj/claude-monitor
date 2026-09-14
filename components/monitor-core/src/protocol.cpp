#include "monitor/protocol.h"

#include <cstdlib>

namespace monitor {

const char* stateName(State s){
    switch(s){
        case State::Processing:  return "processing";
        case State::WaitingUser: return "waiting_user";
        case State::Question:    return "question";
        case State::Error:       return "error";
        case State::Paused:      return "paused";
        case State::Compacting:  return "compacting";
        case State::Idle:        return "idle";
        case State::Off:         return "off";
    }
    return "?";
}

bool parseState(std::string_view name, State& out){
    static const State kStates[] = {
        State::Processing, State::WaitingUser, State::Question, State::Error,
        State::Paused, State::Compacting, State::Idle, State::Off,
    };
    for (State s : kStates){
        if (name == stateName(s)){
            out = s;
            return true;
        }
    }
    return false;
}

char sessionCode(State s){
    switch(s){
        case State::Processing:  return 'p';
        case State::WaitingUser: return 'w';
        case State::Question:    return 'q';
        case State::Error:       return 'e';
        case State::Paused:      return 'h';
        case State::Compacting:  return 'c';
        case State::Idle:        return 'i';
        case State::Off:         return '-';
    }
    return '-';
}

bool needsAttention(State s){
    return s == State::WaitingUser || s == State::Question || s == State::Error;
}

bool isStatic(State s){
    return s == State::Idle || s == State::WaitingUser || s == State::Question || s == State::Error;
}

bool isAnimated(State s){
    return s == State::Processing || s == State::Compacting || s == State::Paused;
}

ParsedCommand parseCommand(std::string_view line){
    ParsedCommand cmd;

    // First word and, after the spaces, the argument
    size_t sp = line.find(' ');
    std::string_view word = line.substr(0, sp);
    std::string_view arg;
    if (sp != std::string_view::npos){
        arg = line.substr(sp + 1);
        while (!arg.empty() && arg.front() == ' '){
            arg.remove_prefix(1);
        }
    }

    if (word == "status"){
        cmd.kind = Command::Status;
        return cmd;
    }
    if (word == "version"){
        cmd.kind = Command::Version;
        return cmd;
    }
    if (word == "ping"){
        cmd.kind = Command::Ping;
        return cmd;
    }
    if (word == "subagent_start"){
        cmd.kind = Command::SubagentStart;
        return cmd;
    }
    if (word == "subagent_stop"){
        cmd.kind = Command::SubagentStop;
        return cmd;
    }
    if (word == "tool_start"){
        cmd.kind = Command::ToolStart;
        return cmd;
    }
    if (word == "tool_stop"){
        cmd.kind = Command::ToolStop;
        return cmd;
    }
    if (word == "sessions"){
        cmd.kind = Command::Sessions;
        cmd.arg = std::string(arg);
        return cmd;
    }
    if (word == "calibrate"){
        cmd.kind = Command::Calibrate;
        cmd.arg = std::string(arg);
        return cmd;
    }
    if (word == "event"){
        cmd.kind = Command::Event;
        cmd.arg = std::string(arg);
        return cmd;
    }
    if (word == "session"){
        cmd.kind = Command::Session;
        cmd.arg = std::string(arg);
        return cmd;
    }
    if (word == "session_end"){
        cmd.kind = Command::SessionEnd;
        cmd.arg = std::string(arg);
        return cmd;
    }
    if (word == "session_clear"){
        cmd.kind = Command::SessionClear;
        return cmd;
    }
    if (word == "screenshot"){
        cmd.kind = Command::Screenshot;
        return cmd;
    }
    if (word == "view"){
        cmd.kind = Command::View;
        cmd.arg = std::string(arg);
        return cmd;
    }
    State s;
    if (parseState(word, s)){
        cmd.kind = Command::SetState;
        cmd.state = s;
        return cmd;
    }
    return cmd;   // Unknown: silently ignored by the caller
}

bool parseCalibration(std::string_view args, CalibrationRequest& out){
    out = CalibrationRequest{};
    if (args == "reset"){
        out.reset = true;
        return true;
    }

    size_t pos = 0;
    while (pos < args.size()){
        size_t end = args.find(' ', pos);
        if (end == std::string_view::npos){
            end = args.size();
        }
        std::string_view tok = args.substr(pos, end - pos);
        pos = end + 1;
        if (tok.empty()){
            continue;
        }
        size_t eq = tok.find('=');
        if (eq == std::string_view::npos){
            return false;
        }
        std::string_view key = tok.substr(0, eq);
        std::string value(tok.substr(eq + 1));
        if (value.empty()){
            return false;
        }
        char* endp = nullptr;
        if (key == "rot"){
            long v = strtol(value.c_str(), &endp, 10);
            if (*endp != '\0' || v < 0 || v > 3) return false;
            out.hasRotation = true;
            out.rotation = static_cast<int>(v);
        } else if (key == "sign"){
            long v = strtol(value.c_str(), &endp, 10);
            if (*endp != '\0' || (v != 1 && v != -1)) return false;
            out.hasSign = true;
            out.sign = static_cast<int>(v);
        } else if (key == "offset"){
            float v = strtof(value.c_str(), &endp);
            if (*endp != '\0' || v < -360.0f || v > 360.0f) return false;
            out.hasOffset = true;
            out.offsetDeg = v;
        } else {
            return false;
        }
    }
    return out.hasRotation || out.hasSign || out.hasOffset;
}

const char* HookEvent::find(std::string_view key) const {
    for (const HookField& f : fields){
        if (f.key == key){
            return f.value.c_str();
        }
    }
    return nullptr;
}

bool parseEventLine(std::string_view arg, HookEvent& out){
    out = HookEvent{};
    size_t pos = 0;
    bool first = true;
    while (pos <= arg.size()){
        size_t end = arg.find('\t', pos);
        if (end == std::string_view::npos){
            end = arg.size();
        }
        std::string_view tok = arg.substr(pos, end - pos);
        pos = end + 1;
        if (first){
            while (!tok.empty() && tok.front() == ' ') tok.remove_prefix(1);
            while (!tok.empty() && tok.back() == ' ') tok.remove_suffix(1);
            out.name = std::string(tok);
            first = false;
            continue;
        }
        if (tok.empty()){
            continue;
        }
        size_t eq = tok.find('=');
        HookField f;
        if (eq == std::string_view::npos){
            f.key = std::string(tok);
        } else {
            f.key = std::string(tok.substr(0, eq));
            f.value = std::string(tok.substr(eq + 1));
        }
        out.fields.push_back(std::move(f));
    }
    return !out.name.empty();
}

bool LineParser::feed(char c, std::string& out){
    if (c == '\n'){
        if (overflow_){
            overflow_ = false;   // the dropped line ends here
            line_.clear();
            return false;
        }
        while (!line_.empty() && (line_.back() == '\r' || line_.back() == ' ')){
            line_.pop_back();
        }
        out = line_;
        line_.clear();
        return true;
    }
    if (overflow_){
        return false;   // discarding until the newline
    }
    line_ += c;
    if (line_.size() > maxLen_){
        line_.clear();   // too long for this board: drop the whole line
        overflow_ = true;
    }
    return false;
}

}  // namespace monitor
