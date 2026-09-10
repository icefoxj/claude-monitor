#include "monitor/protocol.h"

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
    if (line == "status"){
        cmd.kind = Command::Status;
        return cmd;
    }
    if (line == "subagent_start"){
        cmd.kind = Command::SubagentStart;
        return cmd;
    }
    if (line == "subagent_stop"){
        cmd.kind = Command::SubagentStop;
        return cmd;
    }
    static const State kStates[] = {
        State::Processing, State::WaitingUser, State::Question, State::Error,
        State::Paused, State::Compacting, State::Idle, State::Off,
    };
    for (State s : kStates){
        if (line == stateName(s)){
            cmd.kind = Command::SetState;
            cmd.state = s;
            return cmd;
        }
    }
    return cmd;   // Unknown: silently ignored by the caller
}

bool LineParser::feed(char c, std::string& out){
    if (c == '\n'){
        while (!line_.empty() && (line_.back() == '\r' || line_.back() == ' ')){
            line_.pop_back();
        }
        out = line_;
        line_.clear();
        return true;
    }
    line_ += c;
    if (line_.size() > maxLen_){
        line_.clear();   // protection against serial garbage
    }
    return false;
}

}  // namespace monitor
