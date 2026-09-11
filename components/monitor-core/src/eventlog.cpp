#include "monitor/eventlog.h"

#include <utility>

namespace monitor {

void EventLog::push(HookEvent&& event, int64_t nowMs){
    entries_.push_back(EventEntry{ std::move(event), ++seq_, nowMs });
    while (entries_.size() > max_){
        entries_.pop_front();
    }
}

}  // namespace monitor
