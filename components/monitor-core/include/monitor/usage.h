#pragma once

#include <cstddef>
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
//         [\tnow=<unix s>|<UTC offset s>] [\twindows=<s>|<s>...]
//         [\tplan=<name>|<status>|<since, unix s>]
//         [\textra=<on|off|full>|<used>|<limit>|<currency>]
//         [\thistory=<start, unix s>|<step s>|<percent>,<percent>,...]
//         [\tproject=<name>|<week %>|<today %>\t...]
//
// <source> is where the numbers came from ("account", "statusline") or
// "none" while it has none; age is how old they are; one limit= field per
// limit, in display order, the reset in seconds from now (-1 unknown) and
// the severity the account reports (normal, warning, critical; may be empty).
// split= is how the week's usage divides between the apps (Claude Code,
// chats, Cowork), in percent of the week's usage; only the account has it.
//
// The rest feeds the usage page (protocol 8; older boards ignore it):
// now= is the host's clock, since the board has none; windows= each
// limit's length in seconds, in limit order (0 unknown), for the pace;
// plan= the subscription; extra= the extra usage the plan can buy (amounts
// as decimal text, limit "-" for none); history= the week's percentage at
// every step from the window's start, empty where the host has no reading;
// project= Claude Code's share of the weekly limit per project, estimated
// by the host from its transcripts, this week and today.

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
    int32_t windowSec = 0;      // the window's length, 0 = unknown
};

struct UsagePlan {
    std::string name;           // "Max 5x"; empty = unknown
    std::string status;         // "active"
    int64_t since = 0;          // subscription start, Unix seconds, 0 = unknown
};

struct UsageExtra {
    std::string state;          // "on", "off", "full" (limit reached); empty = unknown
    std::string used;           // "12.40"
    std::string limit;          // "50.00", empty = no limit
    std::string currency;       // "USD"
};

struct UsageHistory {
    int64_t start = 0;          // Unix seconds of the first value
    int32_t step = 0;           // seconds between values
    std::vector<int16_t> values;   // percent, -1 = no reading
};

// One project's share of the weekly limit, in percent of the limit
struct UsageProject {
    std::string name;
    float week = 0.0f;
    float today = 0.0f;
};

struct Usage {
    static constexpr size_t kMaxLimits   = 4;
    static constexpr size_t kMaxShares   = 5;
    static constexpr size_t kMaxHistory  = 200;   // a week in hours, and some
    static constexpr size_t kMaxProjects = 6;

    std::string source;         // "account", "statusline", "none"
    int32_t ageSec = 0;         // age of the numbers when the line was sent
    std::vector<UsageLimit> limits;
    std::vector<UsageShare> split;   // the week by app; empty when unknown
    int64_t nowUnix = 0;        // the host's clock when it sent the line, 0 = not sent
    int32_t utcOffset = 0;      // its local time minus UTC, in seconds
    UsagePlan plan;
    UsageExtra extra;
    UsageHistory history;
    std::vector<UsageProject> projects;
};

// Parses the argument of "usage". A limit= field or a split= item that
// does not parse is skipped, extra limits beyond kMaxLimits and shares
// beyond kMaxShares are dropped. Returns false when there is no source name.
bool parseUsage(std::string_view arg, Usage& out);

// How a limit is going: how much of its window is gone and where the
// usage ends up at the reset if it keeps the pace it had so far
struct UsagePace {
    bool known = false;         // window and reset known: `elapsed` is meaningful
    bool valid = false;         // far enough into the window (5 %) for a projection
    float elapsed = 0.0f;       // 0..1, the part of the window gone
    float projected = 0.0f;     // percent at the reset
    int32_t runsOutIn = -1;     // seconds until 100 % at that pace; -1 = lasts to the reset, 0 = already out
};

// `elapsedSec`: how long ago the usage line arrived
UsagePace usagePace(const UsageLimit& limit, int32_t elapsedSec);

// The host's local time `elapsedSec` after the line, as Unix seconds plus
// its UTC offset; 0 when the host sent no clock
int64_t usageLocalNow(const Usage& usage, int32_t elapsedSec);

// A moment (Unix seconds) in the host's local time, to the nearest minute: "14:05" on the same
// day, "Wed 04:00" within the next six days, "14 Feb" otherwise; empty
// when the host sent no clock
void usageWhenText(const Usage& usage, int64_t unix, int32_t elapsedSec, char* out, size_t size);

// "14 Feb 2026", in the host's local time; empty without a clock
void usageDateText(const Usage& usage, int64_t unix, char* out, size_t size);

// Local midnights and weekdays for the chart: the weekday (0 = Sunday) and
// the seconds since local midnight of a moment, given the UTC offset
int usageWeekday(int64_t unix, int32_t utcOffset);
int32_t usageSecondsOfDay(int64_t unix, int32_t utcOffset);
const char* usageWeekdayName(int weekday);   // "Sun".."Sat"

}  // namespace monitor
