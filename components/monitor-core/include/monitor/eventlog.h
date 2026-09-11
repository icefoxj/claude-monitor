#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include "monitor/protocol.h"

// The hook events a board has received, for boards that show them: the
// last one in full and a short history. Board-independent; the board
// supplies the clock (milliseconds since boot) so ages can be shown.

namespace monitor {

struct EventEntry {
    HookEvent event;
    uint32_t  seq;          // 1 for the first event received since boot
    int64_t   receivedMs;
};

class EventLog {
public:
    explicit EventLog(size_t history = 16) : max_(history) {}

    void push(HookEvent&& event, int64_t nowMs);

    // The most recent event, or nullptr before the first one
    const EventEntry* last() const { return entries_.empty() ? nullptr : &entries_.back(); }

    // Oldest first, at most `history` entries
    const std::deque<EventEntry>& entries() const { return entries_; }

    // Events received since boot, dropped ones included
    uint32_t count() const { return seq_; }

private:
    std::deque<EventEntry> entries_;
    uint32_t seq_ = 0;
    size_t max_;
};

}  // namespace monitor
