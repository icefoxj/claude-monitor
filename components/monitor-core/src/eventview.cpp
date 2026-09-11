#include "monitor/eventview.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace monitor {

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t kBg        = rgb565(0, 0, 0);
constexpr uint16_t kText      = rgb565(235, 235, 235);
constexpr uint16_t kDim       = rgb565(140, 140, 140);
constexpr uint16_t kAccent    = rgb565(255, 200, 0);
constexpr uint16_t kRule      = rgb565(48, 48, 56);
constexpr uint16_t kHistory   = rgb565(190, 190, 190);

constexpr int kMargin      = 16;
constexpr int kKeyColumn   = 230;   // px reserved for the field name
constexpr int kMaxValueLines = 3;
constexpr int kHistoryRows = 8;

const lgfx::IFont* const kFontBody  = &fonts::DejaVu18;
const lgfx::IFont* const kFontTitle = &fonts::DejaVu24;

}  // namespace

EventView::EventView(M5Canvas& canvas, int pushX, int pushY)
    : canvas_(canvas), pushX_(pushX), pushY_(pushY) {}

void EventView::ageText(int64_t receivedMs, int64_t nowMs, char* out, size_t size) const {
    int64_t s = (nowMs - receivedMs) / 1000;
    if (s < 0) s = 0;
    if (s < 60){
        snprintf(out, size, "%llds ago", static_cast<long long>(s));
    } else if (s < 3600){
        snprintf(out, size, "%lldm%02llds ago", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
    } else {
        snprintf(out, size, "%lldh%02lldm ago", static_cast<long long>(s / 3600), static_cast<long long>((s % 3600) / 60));
    }
}

int EventView::drawWrapped(int x, int y, int width, const char* text, uint16_t color, int maxLines){
    canvas_.setTextColor(color, kBg);
    const int lineH = canvas_.fontHeight() + 4;
    std::string rest(text);
    int lines = 0;
    if (rest.empty()){
        canvas_.drawString("(empty)", x, y);
        return 1;
    }
    while (!rest.empty() && lines < maxLines){
        // Longest prefix that fits, preferring a break after a space
        size_t n = rest.size();
        while (n > 1 && canvas_.textWidth(rest.substr(0, n).c_str()) > width){
            n--;
        }
        size_t cut = n;
        if (n < rest.size()){
            size_t sp = rest.rfind(' ', n);
            if (sp != std::string::npos && sp > n / 2){
                cut = sp + 1;
            }
        }
        std::string piece = rest.substr(0, cut);
        if (lines == maxLines - 1 && cut < rest.size()){
            // Last allowed line and more to come: mark the truncation
            while (piece.size() > 3 && canvas_.textWidth((piece + "...").c_str()) > width){
                piece.pop_back();
            }
            piece += "...";
            rest.clear();
        } else {
            rest.erase(0, cut);
        }
        canvas_.drawString(piece.c_str(), x, y + lines * lineH);
        lines++;
    }
    return lines;
}

void EventView::draw(const EventLog& log, const char* statusLine, int64_t nowMs){
    const int W = canvas_.width();
    const int H = canvas_.height();
    canvas_.fillSprite(kBg);
    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setTextWrap(false);

    // ---- header: title left, status right ----
    canvas_.setFont(kFontBody);
    const int bodyH = canvas_.fontHeight() + 4;
    int y = kMargin;
    canvas_.setTextColor(kDim, kBg);
    canvas_.drawString("claude-monitor  hooks", kMargin, y);
    if (statusLine && statusLine[0]){
        canvas_.setTextColor(kText, kBg);
        canvas_.setTextDatum(textdatum_t::top_right);
        canvas_.drawString(statusLine, W - kMargin, y);
        canvas_.setTextDatum(textdatum_t::top_left);
    }
    y += bodyH + 6;
    canvas_.drawFastHLine(kMargin, y, W - 2 * kMargin, kRule);
    y += 10;

    // ---- history block size, reserved at the bottom ----
    const int historyH = (kHistoryRows + 1) * bodyH + 16;
    const int detailBottom = H - historyH;

    const EventEntry* last = log.last();
    if (!last){
        canvas_.setTextColor(kDim, kBg);
        canvas_.drawString("waiting for the first hook event...", kMargin, y);
    } else {
        // ---- last event: name, sequence number, project, age ----
        canvas_.setFont(kFontTitle);
        char title[128];
        const char* project = last->event.find("project");
        if (project && project[0]){
            snprintf(title, sizeof(title), "#%lu  %s  [%s]", static_cast<unsigned long>(last->seq), last->event.name.c_str(), project);
        } else {
            snprintf(title, sizeof(title), "#%lu  %s", static_cast<unsigned long>(last->seq), last->event.name.c_str());
        }
        canvas_.setTextColor(kAccent, kBg);
        canvas_.drawString(title, kMargin, y);
        canvas_.setFont(kFontBody);
        char age[32];
        ageText(last->receivedMs, nowMs, age, sizeof(age));
        canvas_.setTextColor(kDim, kBg);
        canvas_.setTextDatum(textdatum_t::top_right);
        canvas_.drawString(age, W - kMargin, y + 4);
        canvas_.setTextDatum(textdatum_t::top_left);
        y += canvas_.fontHeight() + 18;

        // ---- every field: key in grey, value wrapped in white ----
        const int valueX = kMargin + kKeyColumn;
        const int valueW = W - valueX - kMargin;
        for (const HookField& f : last->event.fields){
            if (y + bodyH > detailBottom){
                canvas_.setTextColor(kDim, kBg);
                canvas_.drawString("...", kMargin, y);
                break;
            }
            canvas_.setTextColor(kDim, kBg);
            std::string key = f.key;
            while (key.size() > 1 && canvas_.textWidth(key.c_str()) > kKeyColumn - 8){
                key.pop_back();
            }
            canvas_.drawString(key.c_str(), kMargin, y);
            int room = (detailBottom - y) / bodyH;
            int maxLines = room < kMaxValueLines ? (room < 1 ? 1 : room) : kMaxValueLines;
            int lines = drawWrapped(valueX, y, valueW, f.value.c_str(), kText, maxLines);
            y += lines * bodyH + 2;
        }
    }

    // ---- history: newest first ----
    y = detailBottom;
    canvas_.drawFastHLine(kMargin, y, W - 2 * kMargin, kRule);
    y += 8;
    canvas_.setFont(kFontBody);
    canvas_.setTextColor(kDim, kBg);
    char head[64];
    snprintf(head, sizeof(head), "recent  (%lu received since boot)", static_cast<unsigned long>(log.count()));
    canvas_.drawString(head, kMargin, y);
    y += bodyH;
    const auto& entries = log.entries();
    int rows = 0;
    for (auto it = entries.rbegin(); it != entries.rend() && rows < kHistoryRows; ++it, ++rows){
        char age[32];
        ageText(it->receivedMs, nowMs, age, sizeof(age));
        const char* summary = it->event.find("summary");
        const char* project = it->event.find("project");
        const char* session = it->event.find("session_id");
        char line[200];
        if (project && project[0]){
            snprintf(line, sizeof(line), "#%-4lu %-10s %s  [%s]",
                     static_cast<unsigned long>(it->seq), age,
                     summary ? summary : it->event.name.c_str(), project);
        } else {
            snprintf(line, sizeof(line), "#%-4lu %-10s %s  %.8s",
                     static_cast<unsigned long>(it->seq), age,
                     summary ? summary : it->event.name.c_str(),
                     session ? session : "");
        }
        canvas_.setTextColor(rows == 0 ? kText : kHistory, kBg);
        std::string s(line);
        while (s.size() > 1 && canvas_.textWidth(s.c_str()) > W - 2 * kMargin){
            s.pop_back();
        }
        canvas_.drawString(s.c_str(), kMargin, y);
        y += bodyH;
    }

    canvas_.pushSprite(pushX_, pushY_);
}

}  // namespace monitor
