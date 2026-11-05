#include "monitor/usageview.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "monitor/anime.h"
#include "monitor/theme.h"

namespace monitor {

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kText  = rgb565(235, 235, 235);
constexpr uint16_t kDim   = rgb565(140, 140, 140);
constexpr uint16_t kTrack = rgb565(52, 52, 58);
constexpr uint16_t kGreen = rgb565(56, 176, 72);
constexpr uint16_t kAmber = rgb565(240, 160, 32);
constexpr uint16_t kRed   = rgb565(224, 48, 48);

// The apps in the week's split, in the order the account lists them
constexpr uint16_t kSplitColors[] = {
    rgb565(255, 200, 0),     // yellow, the working gear
    rgb565(40, 130, 230),    // blue, the question sign
    rgb565(160, 110, 230),   // violet
    rgb565(40, 175, 165),    // teal
    rgb565(150, 150, 150),   // grey
};

// One size of row: fonts and the offsets of the bar and the reset line
struct RowStyle {
    const lgfx::IFont* label;
    const lgfx::IFont* pct;
    const lgfx::IFont* small;   // nullptr: no reset line
    int rowH;
    int labelY;
    int barY;
    int barH;
    int resetY;
    int minWidth;               // narrower boxes take a smaller style
};

const RowStyle kStyles[] = {
    { &fonts::DejaVu24, &fonts::DejaVu40, &fonts::DejaVu18, 112, 10, 50, 18, 78, 400 },   // the screensaver's card
    { &fonts::DejaVu24, &fonts::DejaVu24, &fonts::DejaVu18,  72,  0, 28, 12, 44, 300 },
    { &fonts::DejaVu18, &fonts::DejaVu18, &fonts::DejaVu12,  56,  0, 24, 10, 40,   0 },
    { &fonts::DejaVu18, &fonts::DejaVu18, nullptr,           40,  0, 24, 10,  0,   0 },
};

// The second line of a row on the usage page: the reset as a clock time
// (and how far off it is, when there is room) on the left, where the usage
// ends up at the reset on the right
void drawDetailLine(M5Canvas& c, const UsageLimit& l, const Usage& usage, int32_t elapsedSec,
                    int x, int y, int w, uint16_t bg){
    UsageDetailText t;
    usageDetailText(l, usage, elapsedSec, t);
    const int rightW = t.pace[0] ? c.textWidth(t.pace) + 24 : 0;
    const char* text = (t.brief[0] && c.textWidth(t.full) > w - rightW) ? t.brief : t.full;
    c.setTextDatum(textdatum_t::top_left);
    c.setTextColor(kDim, bg);
    if (text[0]) c.drawString(text, x, y);
    if (t.pace[0]){
        c.setTextDatum(textdatum_t::top_right);
        c.setTextColor(t.tone == 2 ? kRed : (t.tone == 1 ? kAmber : kDim), bg);
        c.drawString(t.pace, x + w, y);
        c.setTextDatum(textdatum_t::top_left);
    }
}

void percentText(const UsageLimit& l, char* out, size_t size){
    snprintf(out, size, "%d%%", static_cast<int>(lroundf(l.percent)));
}

// The label in the room the percentage leaves, cut when it still does not fit
void drawLabel(M5Canvas& c, const std::string& label, int x, int y, int room, const lgfx::IFont* font){
    c.setFont(font);
    std::string s = label;
    if (c.textWidth(s.c_str()) > room){
        while (!s.empty() && c.textWidth((s + "...").c_str()) > room) s.pop_back();
        s += "...";
    }
    c.drawString(s.c_str(), x, y);
}

// The week by app: a title, a bar cut into one segment per app, and a
// legend "Chats 75%  Cowork 15%  ..." in the same colours, as far as it fits
void drawSplitRow(M5Canvas& c, int x, int top, int w, const RowStyle& st,
                  const lgfx::IFont* labelFont, const Usage& usage, uint16_t bg){
    c.setTextDatum(textdatum_t::top_left);
    c.setFont(labelFont);
    c.setTextColor(kText, bg);
    c.drawString("This week by app", x, top + st.labelY);

    float total = 0.0f;
    for (const UsageShare& s : usage.split) total += s.percent;
    const int by = top + st.barY;
    c.fillRoundRect(x, by, w, st.barH, st.barH / 2, kTrack);
    if (total > 0.0f){
        // Square segments inside a rounded track: draw the track's shape in
        // the colours, segment by segment, then round the two ends
        int sx = x;
        const size_t n = usage.split.size();
        for (size_t i = 0; i < n; i++){
            int sw = (i + 1 == n) ? (x + w - sx) : static_cast<int>(w * usage.split[i].percent / total);
            if (sw <= 0) continue;
            c.fillRect(sx, by, sw, st.barH, kSplitColors[i % 5]);
            sx += sw;
        }
        const int r = st.barH / 2;
        // Black (bg) corners outside the pill, as fillRoundRect leaves them
        for (int k = 0; k < r; k++){
            int cut = r - static_cast<int>(sqrtf(static_cast<float>(r * r - (r - k) * (r - k))));
            if (cut <= 0) continue;
            c.drawFastHLine(x, by + k, cut, bg);
            c.drawFastHLine(x, by + st.barH - 1 - k, cut, bg);
            c.drawFastHLine(x + w - cut, by + k, cut, bg);
            c.drawFastHLine(x + w - cut, by + st.barH - 1 - k, cut, bg);
        }
    }

    if (!st.small) return;
    c.setFont(st.small);
    int lx = x;
    for (size_t i = 0; i < usage.split.size(); i++){
        char item[48];
        snprintf(item, sizeof(item), "%s%s %d%%", i ? "  " : "", usage.split[i].label.c_str(),
                 static_cast<int>(lroundf(usage.split[i].percent)));
        const int iw = c.textWidth(item);
        if (lx + iw > x + w) break;
        c.setTextColor(kSplitColors[i % 5], bg);
        c.drawString(item, lx, top + st.resetY);
        lx += iw;
    }
}

}  // namespace

uint16_t usageLevelColor(float percent, const std::string& severity){
    int level = percent >= 80.0f ? 2 : (percent >= 50.0f ? 1 : 0);
    if (severity == "critical") level = 2;
    else if (severity == "warning" && level < 1) level = 1;
    return level == 2 ? kRed : (level == 1 ? kAmber : kGreen);
}

void usageDurationText(int32_t sec, char* out, size_t size){
    int m = static_cast<int>((sec + 59) / 60);
    if (m < 60)            snprintf(out, size, "%d min", m);
    else if (m < 24 * 60)  snprintf(out, size, "%d h %02d min", m / 60, m % 60);
    else                   snprintf(out, size, "%d d %d h", m / (24 * 60), (m / 60) % 24);
}

void usageResetText(const UsageLimit& l, int32_t elapsedSec, char* out, size_t size){
    const int32_t left = l.resetsIn < 0 ? -1 : (l.resetsIn > elapsedSec ? l.resetsIn - elapsedSec : 0);
    if (left < 0){ out[0] = '\0'; return; }
    if (left == 0){ snprintf(out, size, "resetting now"); return; }
    char d[32];
    usageDurationText(left, d, sizeof(d));
    snprintf(out, size, "resets in %s", d);
}

void usageDetailText(const UsageLimit& l, const Usage& usage, int32_t elapsedSec, UsageDetailText& out){
    out = UsageDetailText{};
    const int32_t left = l.resetsIn < 0 ? -1 : (l.resetsIn > elapsedSec ? l.resetsIn - elapsedSec : 0);
    const UsagePace pace = usagePace(l, elapsedSec);
    if (pace.valid){
        if (pace.runsOutIn == 0){
            snprintf(out.pace, sizeof(out.pace), "limit reached");
            out.tone = 2;
        } else if (pace.runsOutIn > 0){
            char when[24];
            usageWhenText(usage, usage.nowUnix + elapsedSec + pace.runsOutIn, elapsedSec, when, sizeof(when));
            if (when[0]){
                snprintf(out.pace, sizeof(out.pace), "runs out %s", when);
            } else {
                char d[32];
                usageDurationText(pace.runsOutIn, d, sizeof(d));
                snprintf(out.pace, sizeof(out.pace), "runs out in %s", d);
            }
            out.tone = 1;
        } else {
            snprintf(out.pace, sizeof(out.pace), "on pace for %d%%", static_cast<int>(lroundf(pace.projected)));
        }
    }
    char when[24] = "";
    if (left > 0) usageWhenText(usage, usage.nowUnix + elapsedSec + left, elapsedSec, when, sizeof(when));
    if (when[0]){
        char d[32];
        usageDurationText(left, d, sizeof(d));
        snprintf(out.full, sizeof(out.full), "resets %s, in %s", when, d);
        snprintf(out.brief, sizeof(out.brief), "resets %s", when);
    } else {
        usageResetText(l, elapsedSec, out.full, sizeof(out.full));
    }
}

int usageRowCount(const Usage& usage){
    return static_cast<int>(usage.limits.size()) + (usage.split.empty() ? 0 : 1);
}

int usageRowsHeight(int rows){
    return (rows < 1 ? 1 : rows) * kStyles[0].rowH;
}

void usageAgeText(int32_t age, char* out, size_t size){
    if (age < 90)        snprintf(out, size, "updated just now");
    else if (age < 3600) snprintf(out, size, "updated %d min ago", static_cast<int>(age / 60));
    else                 snprintf(out, size, "updated %d h ago", static_cast<int>(age / 3600));
}

void drawUsageRows(M5Canvas& c, int x, int y, int w, int h,
                   const Usage& usage, int32_t elapsedSec, uint16_t bg, bool detailed){
    if (animeTheme()){ anime::usageRows(c, x, y, w, h, usage, elapsedSec, bg, detailed); return; }
    const int rows = usageRowCount(usage);
    const int limitRows = static_cast<int>(usage.limits.size());
    if (rows == 0 || w <= 0 || h <= 0) return;

    const RowStyle* st = &kStyles[sizeof(kStyles) / sizeof(kStyles[0]) - 1];
    for (const RowStyle& s : kStyles){
        if (rows * s.rowH <= h && w >= s.minWidth){ st = &s; break; }
    }
    int shown = rows;
    if (shown * st->rowH > h) shown = h / st->rowH;   // even the smallest rows do not all fit
    int top = y + (h - shown * st->rowH) / 2;

    // One label size for every row: a size smaller for all of them when
    // one does not fit next to its percentage
    const lgfx::IFont* labelFont = st->label;
    if (labelFont == &fonts::DejaVu24){
        for (int i = 0; i < shown && i < limitRows; i++){
            char pct[16];
            percentText(usage.limits[i], pct, sizeof(pct));
            c.setFont(st->pct);
            const int room = w - c.textWidth(pct) - 16;
            c.setFont(labelFont);
            if (c.textWidth(usage.limits[i].label.c_str()) > room){ labelFont = &fonts::DejaVu18; break; }
        }
    }

    for (int i = 0; i < shown; i++){
        if (i >= limitRows){   // the last row: the week by app
            drawSplitRow(c, x, top, w, *st, labelFont, usage, bg);
            break;
        }
        const UsageLimit& l = usage.limits[i];

        char pct[16];
        percentText(l, pct, sizeof(pct));
        c.setTextDatum(textdatum_t::top_right);
        c.setFont(st->pct);
        c.setTextColor(kText, bg);
        c.drawString(pct, x + w, top);
        const int pctW = c.textWidth(pct);

        c.setTextDatum(textdatum_t::top_left);
        drawLabel(c, l.label, x, top + st->labelY, w - pctW - 16, labelFont);

        const int by = top + st->barY;
        c.fillRoundRect(x, by, w, st->barH, st->barH / 2, kTrack);
        float f = l.percent / 100.0f;
        if (f > 1.0f) f = 1.0f;
        if (f < 0.0f) f = 0.0f;
        int fw = static_cast<int>(w * f);
        if (fw > 0){
            if (fw < st->barH) fw = st->barH;   // a sliver still reads as a pill
            c.fillRoundRect(x, by, fw, st->barH, st->barH / 2, usageLevelColor(l.percent, l.severity));
        }
        if (detailed){
            // Where an even pace would have the bar by now
            const UsagePace pace = usagePace(l, elapsedSec);
            if (pace.known){
                const int tx = x + static_cast<int>((w - 3) * pace.elapsed);
                const int ext = st->barH >= 16 ? 5 : 3;   // past the bar, clear of the texts
                c.fillRect(tx - 1, by - ext - 1, 5, st->barH + 2 * ext + 2, bg);
                c.fillRect(tx, by - ext, 3, st->barH + 2 * ext, kText);
            }
        }

        if (st->small && detailed){
            c.setFont(st->small);
            drawDetailLine(c, l, usage, elapsedSec, x, top + st->resetY, w, bg);
        } else if (st->small){
            char reset[48];
            usageResetText(l, elapsedSec, reset, sizeof(reset));
            if (reset[0]){
                c.setFont(st->small);
                c.setTextColor(kDim, bg);
                c.drawString(reset, x, top + st->resetY);
            }
        }
        top += st->rowH;
    }
}

}  // namespace monitor
