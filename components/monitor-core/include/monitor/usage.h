#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The account's plan usage, as the host reads it (the 5-hour session, the
// week, per-model weekly limits), for boards that list "usage" in
// features=. The host sends
//
//   usage <source>\tage=<s>\tlimit=<label>|<percent>|<resets in s>|<severity>\t...
//         [\tsplit=<label>:<percent>|<label>:<percent>...]
//
// <source> is where the numbers came from ("account", "statusline") or
// "none" while it has none; age is how old they are; one limit= field per
// limit, in display order, the reset in seconds from now (-1 unknown) and
// the severity the account reports (normal, warning, critical; may be empty).
// split= is how the week's usage divides between the apps (Claude Code,
// chats, Cowork), in percent of the week's usage; only the account has it.

namespace monitor {

// One app's share of the week's usage
struct UsageShare {
    std::string label;
    float percent = 0.0f;
};

struct UsageLimit {
    std::string label;
    float percent = 0.0f;       // 0..100, can pass 100
    int32_t resetsIn = -1;      // seconds from when the line was sent, -1 = unknown
    std::string severity;
};

struct Usage {
    static constexpr size_t kMaxLimits = 4;
    static constexpr size_t kMaxShares = 5;

    std::string source;         // "account", "statusline", "none"
    int32_t ageSec = 0;         // age of the numbers when the line was sent
    std::vector<UsageLimit> limits;
    std::vector<UsageShare> split;   // the week by app; empty when unknown
};

// Parses the argument of "usage". A limit= field or a split= item that
// does not parse is skipped, extra limits beyond kMaxLimits and shares
// beyond kMaxShares are dropped. Returns false when there is no source name.
bool parseUsage(std::string_view arg, Usage& out);

}  // namespace monitor
