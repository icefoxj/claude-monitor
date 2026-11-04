#include "monitor/usage.h"

#include <cstdio>
#include <cstdlib>

#include "monitor/protocol.h"

namespace monitor {

namespace {

// Splits "<label>|<percent>|<resets in>|<severity>"; the last two are optional
bool parseLimit(const std::string& value, UsageLimit& out){
    out = UsageLimit{};
    size_t a = value.find('|');
    if (a == std::string::npos || a == 0) return false;
    out.label = value.substr(0, a);

    size_t b = value.find('|', a + 1);
    std::string pct = value.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    char* end = nullptr;
    float p = strtof(pct.c_str(), &end);
    if (pct.empty() || *end != '\0' || p < 0.0f) return false;
    out.percent = p;
    if (b == std::string::npos) return true;

    size_t c = value.find('|', b + 1);
    std::string reset = value.substr(b + 1, c == std::string::npos ? std::string::npos : c - b - 1);
    if (!reset.empty()){
        long r = strtol(reset.c_str(), &end, 10);
        if (*end != '\0') return false;
        out.resetsIn = r < 0 ? -1 : static_cast<int32_t>(r);
    }
    if (c != std::string::npos) out.severity = value.substr(c + 1);
    return true;
}

// "<label>:<percent>|<label>:<percent>...", the percent after the last ':'
void parseSplit(const std::string& value, std::vector<UsageShare>& out){
    size_t pos = 0;
    while (pos <= value.size() && out.size() < Usage::kMaxShares){
        size_t end = value.find('|', pos);
        if (end == std::string::npos) end = value.size();
        std::string item = value.substr(pos, end - pos);
        pos = end + 1;
        size_t colon = item.rfind(':');
        if (colon == std::string::npos || colon == 0) continue;
        std::string pct = item.substr(colon + 1);
        char* stop = nullptr;
        float p = strtof(pct.c_str(), &stop);
        if (pct.empty() || *stop != '\0' || p < 0.0f) continue;
        out.push_back({ item.substr(0, colon), p });
    }
}

std::vector<std::string> splitOn(const std::string& value, char sep){
    std::vector<std::string> parts;
    size_t pos = 0;
    while (true){
        size_t end = value.find(sep, pos);
        if (end == std::string::npos){ parts.push_back(value.substr(pos)); break; }
        parts.push_back(value.substr(pos, end - pos));
        pos = end + 1;
    }
    return parts;
}

bool toInt64(const std::string& s, int64_t& out){
    if (s.empty()) return false;
    char* end = nullptr;
    long long v = strtoll(s.c_str(), &end, 10);
    if (*end != '\0') return false;
    out = static_cast<int64_t>(v);
    return true;
}

bool toFloat(const std::string& s, float& out){
    if (s.empty()) return false;
    char* end = nullptr;
    float v = strtof(s.c_str(), &end);
    if (*end != '\0') return false;
    out = v;
    return true;
}

void parseHistory(const std::string& value, UsageHistory& out){
    out = UsageHistory{};
    std::vector<std::string> parts = splitOn(value, '|');
    int64_t start = 0, step = 0;
    if (parts.size() != 3 || !toInt64(parts[0], start) || !toInt64(parts[1], step) || step <= 0) return;
    out.start = start;
    out.step = static_cast<int32_t>(step);
    for (const std::string& v : splitOn(parts[2], ',')){
        if (out.values.size() >= Usage::kMaxHistory) break;
        int64_t p = -1;
        if (!toInt64(v, p) || p < 0) p = -1;
        out.values.push_back(static_cast<int16_t>(p > 999 ? 999 : p));
    }
}

int64_t floorDiv(int64_t a, int64_t b){
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

// Days since 1970-01-01 to a civil date (H. Hinnant's algorithm)
void civilFromDays(int64_t z, int& y, int& m, int& d){
    z += 719468;
    const int64_t era = floorDiv(z, 146097);
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

const char* const kWeekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char* const kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

}  // namespace

bool parseUsage(std::string_view arg, Usage& out){
    out = Usage{};
    HookEvent ev;
    if (!parseEventLine(arg, ev)) return false;
    out.source = ev.name;
    std::vector<int32_t> windows;
    for (const HookField& f : ev.fields){
        if (f.key == "age"){
            long a = strtol(f.value.c_str(), nullptr, 10);
            out.ageSec = a < 0 ? 0 : static_cast<int32_t>(a);
        } else if (f.key == "limit" && out.limits.size() < Usage::kMaxLimits){
            UsageLimit l;
            if (parseLimit(f.value, l)) out.limits.push_back(std::move(l));
        } else if (f.key == "split"){
            out.split.clear();
            parseSplit(f.value, out.split);
        } else if (f.key == "now"){
            std::vector<std::string> p = splitOn(f.value, '|');
            int64_t now = 0, offset = 0;
            if (toInt64(p[0], now) && now > 0){
                out.nowUnix = now;
                if (p.size() > 1 && toInt64(p[1], offset)) out.utcOffset = static_cast<int32_t>(offset);
            }
        } else if (f.key == "windows"){
            windows.clear();
            for (const std::string& w : splitOn(f.value, '|')){
                int64_t s = 0;
                windows.push_back(toInt64(w, s) && s > 0 ? static_cast<int32_t>(s) : 0);
            }
        } else if (f.key == "plan"){
            std::vector<std::string> p = splitOn(f.value, '|');
            out.plan = UsagePlan{};
            out.plan.name = p[0];
            if (p.size() > 1) out.plan.status = p[1];
            int64_t since = 0;
            if (p.size() > 2 && toInt64(p[2], since) && since > 0) out.plan.since = since;
        } else if (f.key == "extra"){
            std::vector<std::string> p = splitOn(f.value, '|');
            out.extra = UsageExtra{};
            out.extra.state = p[0];
            if (p.size() > 1) out.extra.used = p[1];
            if (p.size() > 2 && p[2] != "-") out.extra.limit = p[2];
            if (p.size() > 3) out.extra.currency = p[3];
        } else if (f.key == "history"){
            parseHistory(f.value, out.history);
        } else if (f.key == "project" && out.projects.size() < Usage::kMaxProjects){
            std::vector<std::string> p = splitOn(f.value, '|');
            UsageProject pr;
            if (p[0].empty() || p.size() < 2 || !toFloat(p[1], pr.week)) continue;
            pr.name = p[0];
            if (p.size() > 2) toFloat(p[2], pr.today);
            out.projects.push_back(std::move(pr));
        }
    }
    for (size_t i = 0; i < out.limits.size() && i < windows.size(); i++){
        out.limits[i].windowSec = windows[i];
    }
    return true;
}

UsagePace usagePace(const UsageLimit& l, int32_t elapsedSec){
    UsagePace p;
    if (l.windowSec <= 0 || l.resetsIn < 0) return p;
    int64_t left = static_cast<int64_t>(l.resetsIn) - elapsedSec;
    if (left < 0) left = 0;
    int64_t gone = static_cast<int64_t>(l.windowSec) - left;
    if (gone < 0) gone = 0;
    p.known = true;
    p.elapsed = static_cast<float>(gone) / static_cast<float>(l.windowSec);
    if (p.elapsed > 1.0f) p.elapsed = 1.0f;
    if (l.percent >= 100.0f){
        p.valid = true;
        p.projected = l.percent;
        p.runsOutIn = 0;
        return p;
    }
    if (p.elapsed < 0.05f) return p;   // too early to say
    p.valid = true;
    p.projected = l.percent / p.elapsed;
    if (p.projected > 100.0f && l.percent > 0.0f){
        // At the same pace, 100 % is reached after gone * 100 / percent
        int64_t at100 = static_cast<int64_t>(static_cast<double>(gone) * 100.0 / l.percent);
        p.runsOutIn = static_cast<int32_t>(at100 > gone ? at100 - gone : 0);
    }
    return p;
}

int64_t usageLocalNow(const Usage& u, int32_t elapsedSec){
    if (u.nowUnix <= 0) return 0;
    return u.nowUnix + elapsedSec + u.utcOffset;
}

int usageWeekday(int64_t unix, int32_t utcOffset){
    int64_t days = floorDiv(unix + utcOffset, 86400);
    return static_cast<int>(((days + 4) % 7 + 7) % 7);   // 1970-01-01 was a Thursday
}

int32_t usageSecondsOfDay(int64_t unix, int32_t utcOffset){
    int64_t local = unix + utcOffset;
    return static_cast<int32_t>(local - floorDiv(local, 86400) * 86400);
}

const char* usageWeekdayName(int weekday){
    return kWeekdays[((weekday % 7) + 7) % 7];
}

void usageWhenText(const Usage& u, int64_t unix, int32_t elapsedSec, char* out, size_t size){
    if (size == 0) return;
    out[0] = '\0';
    const int64_t now = usageLocalNow(u, elapsedSec);
    if (now == 0) return;
    unix += 30;   // to the nearest minute: the account's resets fall a few ms either side of it
    const int64_t local = unix + u.utcOffset;
    const int64_t day = floorDiv(local, 86400), today = floorDiv(now, 86400);
    const int32_t sod = usageSecondsOfDay(unix, u.utcOffset);
    const int hh = sod / 3600, mm = (sod / 60) % 60;
    if (day == today){
        snprintf(out, size, "%02d:%02d", hh, mm);
    } else if (day > today && day - today <= 6){
        snprintf(out, size, "%s %02d:%02d", kWeekdays[usageWeekday(unix, u.utcOffset)], hh, mm);
    } else {
        int y, m, d;
        civilFromDays(day, y, m, d);
        snprintf(out, size, "%d %s", d, kMonths[m - 1]);
    }
}

void usageDateText(const Usage& u, int64_t unix, char* out, size_t size){
    if (size == 0) return;
    out[0] = '\0';
    if (u.nowUnix <= 0 || unix <= 0) return;
    int y, m, d;
    civilFromDays(floorDiv(unix + u.utcOffset, 86400), y, m, d);
    snprintf(out, size, "%d %s %d", d, kMonths[m - 1], y);
}

}  // namespace monitor
