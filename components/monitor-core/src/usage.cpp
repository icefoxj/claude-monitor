#include "monitor/usage.h"

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

}  // namespace

bool parseUsage(std::string_view arg, Usage& out){
    out = Usage{};
    HookEvent ev;
    if (!parseEventLine(arg, ev)) return false;
    out.source = ev.name;
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
        }
    }
    return true;
}

}  // namespace monitor
