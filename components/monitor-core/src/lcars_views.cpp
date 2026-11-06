// The LCARS theme's views: the usage rows (the usage tile, the
// screensaver's card, the usage page), the usage page, the screensaver's
// card and the hook inspector. The same data in the same boxes as the
// classic views; only the drawing differs.

#include "monitor/lcars.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "monitor/usagepage.h"
#include "monitor/usageview.h"

namespace monitor::lcars {

namespace {

// Green, gold or red for a limit's percentage, as in the other themes
uint16_t levelColor(float percent, const std::string& severity){
    int level = percent >= 80.0f ? 2 : (percent >= 50.0f ? 1 : 0);
    if (severity == "critical") level = 2;
    else if (severity == "warning" && level < 1) level = 1;
    return level == 2 ? kRed : (level == 1 ? kGold : kGreen);
}

// The colours of the week-by-app split, in the account's order
uint16_t splitColor(int i){
    static constexpr uint16_t kColors[] = { kOrange, kBlue, kLilac, kGreen, kGrey };
    return kColors[i % 5];
}

void dashed(LovyanGFX& g, int x0, int y0, int x1, int y1, uint16_t color){
    const float dx = static_cast<float>(x1 - x0), dy = static_cast<float>(y1 - y0);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    for (float s = 0.0f; s < len; s += 18.0f){
        const float e = s + 10.0f < len ? s + 10.0f : len;
        for (int k = 0; k < 2; k++){
            g.drawLine(x0 + static_cast<int>(dx * s / len), y0 + static_cast<int>(dy * s / len) + k,
                       x0 + static_cast<int>(dx * e / len), y0 + static_cast<int>(dy * e / len) + k, color);
        }
    }
}

// A section heading: a short bar with a round left end, then the words
void heading(LovyanGFX& g, int x, int y, const char* words, uint16_t color, int h = 24){
    endCap(g, x, y, h + 10, h, true, color);
    text(g, words, x + h + 20, y + h / 2, Face::Lcars, h + 4, color, textdatum_t::middle_left);
}

// A bar of `w` px with round ends, filled to `fraction` over a dim track
void meter(LovyanGFX& g, int x, int y, int w, int h, float fraction, uint16_t color){
    capsule(g, x, y, w, h, kDim);
    if (fraction > 1.0f) fraction = 1.0f;
    if (fraction <= 0.0f) return;
    int fw = static_cast<int>(w * fraction);
    if (fw < h) fw = h;   // a sliver still reads as a capsule
    capsule(g, x, y, fw, h, color);
}

void ageText(int64_t receivedMs, int64_t nowMs, char* out, size_t size){
    long long s = (nowMs - receivedMs) / 1000;
    if (s < 0) s = 0;
    if (s < 60)        snprintf(out, size, "%lld S AGO", s);
    else if (s < 3600) snprintf(out, size, "%lld M %02lld S AGO", s / 60, s % 60);
    else               snprintf(out, size, "%lld H %02lld M AGO", s / 3600, (s % 3600) / 60);
}

// One size of row, as tall as the classic ones (the page and the card lay
// out with usageRowsHeight). Every text stands on a baseline: Antonio's
// cell is much taller than its capitals, so a text placed by its top
// reaches into the next row
struct RowStyle {
    int labelPx, pctPx, smallPx;   // smallPx 0: no reset line
    int rowH, barY, barH;
    int resetBase;                 // the reset line's baseline
    int minWidth;
};

const RowStyle kRows[] = {
    { 32, 50, 22, 112, 52, 20, 102, 400 },
    { 26, 34, 20,  72, 32, 14,  68, 300 },
    { 20, 24, 16,  56, 25, 12,  52,   0 },
    { 20, 22,  0,  40, 25, 12,   0,   0 },
};

void splitRow(M5Canvas& c, int x, int top, int w, const RowStyle& st, const Usage& usage){
    text(c, "THIS WEEK BY APP", x, top + st.barY - 6, Face::Lcars, st.labelPx, kTan, textdatum_t::baseline_left);
    const int n = static_cast<int>(usage.split.size() < Usage::kMaxShares ? usage.split.size() : Usage::kMaxShares);
    float total = 0.0f;
    for (int i = 0; i < n; i++) total += usage.split[i].percent;
    const int by = top + st.barY, gap = 4;
    if (total <= 0.0f || n == 0){
        capsule(c, x, by, w, st.barH, kDim);
    } else {
        // One block per app, black gaps between, the two ends round
        int sx = x;
        for (int i = 0; i < n; i++){
            int sw = (i + 1 == n) ? (x + w - sx) : static_cast<int>((w - gap * (n - 1)) * usage.split[i].percent / total);
            if (sw <= 0) continue;
            if (n == 1)          capsule(c, sx, by, sw, st.barH, splitColor(i));
            else if (i == 0)     endCap(c, sx, by, sw, st.barH, true, splitColor(i));
            else if (i + 1 == n) endCap(c, sx, by, sw, st.barH, false, splitColor(i));
            else                 c.fillRect(sx, by, sw, st.barH, splitColor(i));
            sx += sw + gap;
        }
    }
    if (!st.smallPx) return;
    int lx = x;
    for (int i = 0; i < n; i++){
        char item[48];
        snprintf(item, sizeof(item), "%s%s %d%%", i ? "   " : "", upper(usage.split[i].label).c_str(),
                 static_cast<int>(lroundf(usage.split[i].percent)));
        const int iw = textWidth(c, item, Face::Lcars, st.smallPx);
        if (lx + iw > x + w) break;
        text(c, item, lx, top + st.resetBase, Face::Lcars, st.smallPx, splitColor(i), textdatum_t::baseline_left);
        lx += iw;
    }
}

}  // namespace

// ---------------- usage rows ----------------

void usageRows(M5Canvas& c, int x, int y, int w, int h, const Usage& usage, int32_t elapsedSec, bool detailed){
    const int rows = usageRowCount(usage);
    const int limitRows = static_cast<int>(usage.limits.size());
    if (rows == 0 || w <= 0 || h <= 0) return;

    const RowStyle* st = &kRows[sizeof(kRows) / sizeof(kRows[0]) - 1];
    for (const RowStyle& s : kRows){
        if (rows * s.rowH <= h && w >= s.minWidth){ st = &s; break; }
    }
    int shown = rows;
    if (shown * st->rowH > h) shown = h / st->rowH;
    int top = y + (h - shown * st->rowH) / 2;

    for (int i = 0; i < shown; i++){
        if (i >= limitRows){
            splitRow(c, x, top, w, *st, usage);
            break;
        }
        const UsageLimit& l = usage.limits[i];
        const uint16_t level = levelColor(l.percent, l.severity);
        const int by = top + st->barY;

        char pct[12];
        snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(lroundf(l.percent)));
        const int pctW = text(c, pct, x + w, by - 6, Face::Lcars, st->pctPx, level, textdatum_t::baseline_right);
        const std::string label = fit(c, upper(l.label), Face::Lcars, st->labelPx, w - pctW - 24);
        text(c, label.c_str(), x, by - 6, Face::Lcars, st->labelPx, kTan, textdatum_t::baseline_left);

        meter(c, x, by, w, st->barH, l.percent / 100.0f, level);
        if (detailed){
            // Where an even pace would have the bar by now
            const UsagePace pace = usagePace(l, elapsedSec);
            if (pace.known){
                const int tx = x + static_cast<int>((w - 3) * pace.elapsed);
                c.fillRect(tx - 1, by - 5, 5, st->barH + 10, kBg);
                c.fillRect(tx, by - 4, 3, st->barH + 8, rgb(255, 255, 255));
            }
        }

        // The line under the bar in the bar's colour; a warning in its own
        if (st->smallPx && detailed){
            UsageDetailText t;
            usageDetailText(l, usage, elapsedSec, t);
            const std::string pace = upper(t.pace);
            const int paceW = pace.empty() ? 0 : textWidth(c, pace.c_str(), Face::Lcars, st->smallPx) + 24;
            std::string left = upper(t.full);
            if (t.brief[0] && textWidth(c, left.c_str(), Face::Lcars, st->smallPx) > w - paceW) left = upper(t.brief);
            text(c, left.c_str(), x, top + st->resetBase, Face::Lcars, st->smallPx, level, textdatum_t::baseline_left);
            if (!pace.empty()){
                const uint16_t pc = t.tone == 2 ? kRed : (t.tone == 1 ? kGold : level);
                text(c, pace.c_str(), x + w, top + st->resetBase, Face::Lcars, st->smallPx, pc, textdatum_t::baseline_right);
            }
        } else if (st->smallPx){
            char reset[48];
            usageResetText(l, elapsedSec, reset, sizeof(reset));
            if (reset[0]) text(c, upper(reset).c_str(), x, top + st->resetBase, Face::Lcars, st->smallPx, level, textdatum_t::baseline_left);
        }
        top += st->rowH;
    }
}

// ---------------- the usage page ----------------

namespace {

void chartBox(M5Canvas& c, const Usage& usage, int32_t elapsedSec, int bx, int by, int bw, int bh){
    UsageChart ch;
    if (!usageChart(usage, elapsedSec, ch)){
        text(c, "NO WEEKLY DATA", bx, by + 8, Face::Lcars, 22, kPeriwinkle);
        return;
    }
    const int px = bx + 56, py = by + 10;
    const int pw = bw - 56 - 8, ph = bh - 10 - 36;
    auto X = [&](int64_t t) { return px + static_cast<int>(static_cast<double>(pw) * (t - ch.start) / (ch.end - ch.start)); };
    auto Y = [&](float v) { return py + ph - static_cast<int>(ph * v / ch.yMax); };

    for (int v = 25; v <= static_cast<int>(ch.yMax); v += 25){
        const int y = Y(static_cast<float>(v));
        c.fillRect(px, y, pw, v == 100 ? 2 : 1, v == 100 ? kPeriwinkle : kDim);
        char t[16];
        snprintf(t, sizeof(t), "%d%%", v);
        text(c, t, px - 10, y, Face::Lcars, 20, kViolet, textdatum_t::middle_right);
    }
    endCap(c, px, py + ph, pw, 4, true, kPeriwinkle);   // the time axis

    // Local midnights, the weekday under each day (today in tan)
    const int32_t off = usage.utcOffset;
    const int todayWd = usage.nowUnix ? usageWeekday(ch.now, off) : -1;
    int64_t dayStart = ch.start;
    int64_t midnight = ch.start + (86400 - usageSecondsOfDay(ch.start, off));
    while (dayStart < ch.end){
        const int64_t dayEnd = midnight < ch.end ? midnight : ch.end;
        if (midnight < ch.end) c.drawFastVLine(X(midnight), py, ph, kDim);
        const int x0 = X(dayStart), x1 = X(dayEnd);
        if (x1 - x0 >= 40){
            const int wd = usageWeekday(dayStart, off);
            const bool today = wd == todayWd && ch.now >= dayStart && ch.now < dayEnd;
            text(c, upper(usageWeekdayName(wd)).c_str(), (x0 + x1) / 2, py + ph + 10, Face::Lcars, 22,
                 today ? kTan : kPeriwinkle, textdatum_t::top_center);
        }
        dayStart = dayEnd;
        midnight += 86400;
    }

    // The even pace; the usage so far, filled underneath, the line on top
    dashed(c, X(ch.start), Y(0.0f), X(ch.end), Y(100.0f), kPeriwinkle);
    const uint16_t curve = ch.week ? levelColor(ch.week->percent, ch.week->severity) : kOrange;
    const uint16_t fill = shade(curve, 32);
    const int xNow = X(ch.points.back().t);
    int prev = -1;
    for (int x = px; x <= xNow; x++){
        const int64_t t = ch.start + static_cast<int64_t>(static_cast<double>(x - px) * (ch.end - ch.start) / pw);
        const int y = Y(usageChartValue(ch, t));
        if (py + ph - y > 0) c.drawFastVLine(x, y, py + ph - y, fill);
        const int a = prev < 0 ? y : prev;
        const int lo = a < y ? a : y, hi = a < y ? y : a;
        c.fillRect(x, lo - 1, 1, hi - lo + 3, curve);
        prev = y;
    }

    // Where the pace leads by the reset, and now
    if (ch.week && ch.pace.valid && ch.pace.projected > ch.week->percent){
        const int yNow = Y(ch.week->percent);
        int x1 = X(ch.end);
        float v1 = ch.pace.projected;
        if (v1 > ch.yMax){
            v1 = ch.yMax;
            const float f = (ch.yMax - ch.week->percent) / (ch.pace.projected - ch.week->percent);
            x1 = xNow + static_cast<int>((X(ch.end) - xNow) * f);
        }
        const uint16_t pc = ch.pace.projected > 100.0f ? kRed : kGold;
        dashed(c, xNow, yNow, x1, Y(v1), pc);
        char t[16];
        snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(ch.pace.projected)));
        if (Y(v1) - 34 < py) text(c, t, x1 - 6, Y(v1) + 6, Face::Lcars, 26, pc, textdatum_t::top_right);
        else                 text(c, t, x1 - 2, Y(v1) - 6, Face::Lcars, 26, pc, textdatum_t::bottom_right);
    }
    if (ch.week){
        const int yNow = Y(ch.week->percent);
        capsule(c, xNow - 10, yNow - 7, 20, 14, kBg);
        capsule(c, xNow - 8, yNow - 5, 16, 10, curve);
    }
    if (ch.noHistory){
        text(c, "NO READINGS YET: THE LINE IS THE AVERAGE SO FAR", px + 8, py + 4, Face::Lcars, 18, kPeriwinkle);
    }
}

void projectsBox(M5Canvas& c, const Usage& usage, int bx, int by, int bw, int bh){
    if (usage.projects.empty()){
        text(c, "NO CLAUDE CODE TRANSCRIPTS READ THIS WEEK", bx, by + 8, Face::Lcars, 22, kPeriwinkle);
        return;
    }
    const int todayR = bx + bw;
    const int weekR  = todayR - 90;
    const int nameW  = bw * 30 / 100;
    const int barX   = bx + nameW + 12;
    const int barW   = weekR - 80 - barX;
    text(c, "WEEK", weekR, by, Face::Lcars, 20, kPeriwinkle, textdatum_t::top_right);
    text(c, "TODAY", todayR, by, Face::Lcars, 20, kPeriwinkle, textdatum_t::top_right);

    float maxWeek = 0.0f;
    for (const UsageProject& p : usage.projects) if (p.week > maxWeek) maxWeek = p.week;
    const int n = static_cast<int>(usage.projects.size());
    int rowH = (bh - 28) / (n > 0 ? n : 1);
    if (rowH > 42) rowH = 42;
    int y = by + 28;
    for (const UsageProject& p : usage.projects){
        const bool other = p.name == "other";
        const int cy = y + rowH / 2;
        const std::string name = fit(c, other ? "OTHER PROJECTS" : upper(p.name), Face::Lcars, 26, nameW);
        text(c, name.c_str(), bx, cy, Face::Lcars, 26, other ? kPeriwinkle : kTan, textdatum_t::middle_left);
        meter(c, barX, cy - 7, barW, 14, maxWeek > 0.0f ? p.week / maxWeek : 0.0f, other ? kPeriwinkle : kOrange);
        char t[16];
        if (p.week > 0.0f && p.week < 1.0f) snprintf(t, sizeof(t), "<1%%");
        else snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(p.week)));
        text(c, t, weekR, cy, Face::Lcars, 32, kOrange, textdatum_t::middle_right);
        if (p.today <= 0.0f) snprintf(t, sizeof(t), "-");
        else if (p.today < 1.0f) snprintf(t, sizeof(t), "<1%%");
        else snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(p.today)));
        text(c, t, todayR, cy, Face::Lcars, 24, kViolet, textdatum_t::middle_right);
        y += rowH;
    }
}

void planBox(M5Canvas& c, const Usage& usage, int bx, int by, int bw){
    std::string plan, extra;
    int planTone = 0, extraTone = 0;
    const bool havePlan = usagePlanText(usage, plan, planTone);
    const bool haveExtra = usageExtraText(usage, extra, extraTone);
    if (!havePlan && !haveExtra) return;
    heading(c, bx, by, "PLAN", kTan);
    const int labelW = 150;
    int y = by + 48;
    auto row = [&](const char* label, const std::string& value, int tone) {
        text(c, label, bx, y, Face::Lcars, 22, kOrange, textdatum_t::middle_left);
        const uint16_t color = tone < 0 ? kPeriwinkle : (tone == 2 ? kRed : (tone == 1 ? kGold : kTan));
        const std::string v = fit(c, upper(value), Face::Lcars, 30, bw - labelW);
        text(c, v.c_str(), bx + labelW, y, Face::Lcars, 30, color, textdatum_t::middle_left);
        y += 36;
    };
    if (havePlan) row("PLAN", plan, planTone);
    if (haveExtra) row("EXTRA USAGE", extra, extraTone);
}

// Blocks down a side bar between two heights, labelled in black at their
// bottom right; gaps of `gap` px between them
void sideBlocks(LovyanGFX& g, int x, int w, int top, int bottom, const char* const* labels,
                const uint16_t* colors, int n, int gap, int px){
    if (bottom - top < n * (gap + 10)) return;
    const int each = (bottom - top - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++){
        const int y = top + i * (each + gap);
        const int h = (i + 1 == n) ? bottom - y : each;
        g.fillRect(x, y, w, h, colors[i]);
        if (labels[i] && h > px){
            text(g, labels[i], x + w - 6, y + h - 4, Face::Lcars, fitSize(g, labels[i], Face::Lcars, px, w - 10),
                 kBg, textdatum_t::bottom_right, colors[i]);
        }
    }
}

}  // namespace

void usagePage(M5Canvas& c, const Usage& usage, int32_t elapsedSec){
    const int W = c.width(), H = c.height();
    c.fillScreen(kBg);
    const bool landscape = W > H;
    const int M = 16, BH = 28, gap = 6;
    const int SW = landscape ? 120 : 84;
    const int elbowH = 110;

    // Top: the elbow, the title at the bar's end, left of the theme switch
    const UsageSwitch sw = usageThemeSwitch(W);
    const int titleW = text(c, "CLAUDE USAGE", sw.x - 24, M + BH + 4, Face::Lcars, 56, kOrange, textdatum_t::baseline_right);
    elbow(c, M, M, SW, elbowH, sw.x - 24 - titleW - 16 - M, BH, true, kOrange);

    // Bottom: the elbow, the age in a capsule at the bar's end
    char age[40];
    usageAgeText(usage.ageSec + elapsedSec, age, sizeof(age));
    const std::string ageUp = upper(age);
    const int agePx = 22;
    const int ageW = textWidth(c, ageUp.c_str(), Face::Lcars, agePx) + BH;
    endCap(c, W - M - ageW, H - M - BH, ageW, BH, false, kPeriwinkle);
    text(c, ageUp.c_str(), W - M - ageW + BH / 3, H - M - BH / 2, Face::Lcars, agePx, kBg, textdatum_t::middle_left, kPeriwinkle);
    elbow(c, M, H - M - elbowH, SW, elbowH, W - M - ageW - gap - M, BH, false, kLilac);

    // The side bar between the elbows: one block per section
    static const char* const kLabels[] = { "LIMITS", "PLAN", "WEEK", "PROJECTS" };
    static const uint16_t kColors[] = { kOrange, kTan, kLilac, kPeriwinkle };
    sideBlocks(c, M, SW, M + elbowH + gap, H - M - elbowH - gap, kLabels, kColors, 4, gap, 24);

    drawThemeSwitch(c, sw);

    const int x0 = M + SW + 28, x1 = W - M;
    const int top = M + BH + 30;
    const int bottom = H - M - BH - 16;
    const int rows = usageRowCount(usage);
    const int planH = 96;
    if (landscape){
        const int colW = (x1 - x0 - 32) / 2;
        const int rx = x0 + colW + 32;
        heading(c, x0, top, "LIMITS", kOrange);
        int rowsH = usageRowsHeight(rows);
        if (rowsH > bottom - planH - (top + 30) - 6) rowsH = bottom - planH - (top + 30) - 6;
        if (rows > 0) usageRows(c, x0, top + 30, colW, rowsH, usage, elapsedSec, true);
        planBox(c, usage, x0, bottom - planH, colW);

        const int chartH = 280;
        heading(c, rx, top, "THIS WEEK", kLilac);
        chartBox(c, usage, elapsedSec, rx, top + 34, colW, chartH);
        const int py = top + 34 + chartH + 14;
        heading(c, rx, py, "BY PROJECT", kPeriwinkle);
        text(c, "EST. SHARE OF THE WEEKLY LIMIT", rx + colW, py + 12, Face::Lcars, 20, kPeriwinkle, textdatum_t::middle_right);
        projectsBox(c, usage, rx, py + 32, colW, bottom - (py + 32));
    } else {
        const int w = x1 - x0;
        heading(c, x0, top, "LIMITS", kOrange);
        int rowsH = usageRowsHeight(rows);
        if (rowsH > 460) rowsH = 460;
        if (rows > 0) usageRows(c, x0, top + 30, w, rowsH, usage, elapsedSec, true);
        int y = top + 30 + rowsH + 8;
        planBox(c, usage, x0, y, w);
        y += planH + 8;
        const int chartH = 290;
        heading(c, x0, y, "THIS WEEK", kLilac);
        chartBox(c, usage, elapsedSec, x0, y + 34, w, chartH);
        y += 34 + chartH + 14;
        heading(c, x0, y, "BY PROJECT", kPeriwinkle);
        projectsBox(c, usage, x0, y + 32, w, bottom - (y + 32));
    }
}

// ---------------- the screensaver's card ----------------

void saverCard(M5Canvas& c, int x, int y, int w, int h, int pad, int header,
               const Usage& usage, int32_t elapsedSec, bool data){
    c.fillScreen(kBg);
    const int sw = 44, bh = 16, gap = 5;
    const int x1 = x + w - pad;
    // The title stays inside the card: its border must stay black, or the
    // drift leaves a trail
    const int titleW = text(c, "CLAUDE USAGE", x1, y + 30, Face::Lcars, 28, kOrange, textdatum_t::baseline_right);
    elbow(c, x, y, sw, 56, x1 - titleW - 12 - x, bh, true, kOrange);
    elbow(c, x, y + h - 44, sw, 44, x1 - x, 14, false, kLilac);
    if (h - 100 - 2 * gap > 10) c.fillRect(x, y + 56 + gap, sw, h - 100 - 2 * gap, kPeriwinkle);

    const int cx = x + sw + 18;
    if (!data){
        text(c, "NO USAGE DATA YET", cx, y + header + 16, Face::Lcars, 32, kTan);
        text(c, "WAITING FOR THE HOST TO SEND IT", cx, y + header + 56, Face::Lcars, 22, kPeriwinkle);
        return;
    }
    char age[40];
    usageAgeText(usage.ageSec + elapsedSec, age, sizeof(age));
    text(c, upper(age).c_str(), x1, y + 54, Face::Lcars, 20, kPeriwinkle, textdatum_t::baseline_right);
    usageRows(c, cx, y + header, x1 - cx, usageRowsHeight(usageRowCount(usage)), usage, elapsedSec, false);
}

// ---------------- the hook inspector ----------------

void inspector(M5Canvas& c, const EventLog& log, const char* statusLine, int64_t nowMs){
    const int W = c.width(), H = c.height();
    const int M = 14, SW = 80, BH = 22, gap = 6;
    const int elbowH = 96;
    c.fillScreen(kBg);

    // Top: the elbow, the title, the status under it
    const int titleW = text(c, "HOOK MONITOR", W - M, M + BH + 4, Face::Lcars, 44, kOrange, textdatum_t::baseline_right);
    elbow(c, M, M, SW, elbowH, W - M - titleW - 16 - M, BH, true, kOrange);
    if (statusLine && statusLine[0]){
        const std::string st = fit(c, upper(statusLine), Face::Lcars, 22, W - M - (M + SW + 16));
        text(c, st.c_str(), W - M, M + BH + 26, Face::Lcars, 22, kTan, textdatum_t::middle_right);
    }

    // Bottom: the elbow, the count of events in a capsule
    char count[40];
    snprintf(count, sizeof(count), "%lu RECEIVED", static_cast<unsigned long>(log.count()));
    const int cw = textWidth(c, count, Face::Lcars, 20) + BH;
    endCap(c, W - M - cw, H - M - BH, cw, BH, false, kPeriwinkle);
    text(c, count, W - M - cw + BH / 3, H - M - BH / 2, Face::Lcars, 20, kBg, textdatum_t::middle_left, kPeriwinkle);
    elbow(c, M, H - M - elbowH, SW, elbowH, W - M - cw - gap - M, BH, false, kLilac);

    const int lineH = 27;
    const int historyRows = 8;
    const int contentBottom = H - M - BH - 12;
    const int historyH = 36 + historyRows * lineH;
    const int detailBottom = contentBottom - historyH;
    static const char* const kLabels[] = { "EVENT", "LOG" };
    static const uint16_t kColors[] = { kPeriwinkle, kTan };
    sideBlocks(c, M, SW, M + elbowH + gap, H - M - elbowH - gap, kLabels, kColors, 2, gap, 22);

    const int x0 = M + SW + 18, x1 = W - M;
    int y = M + BH + 52;
    const EventEntry* last = log.last();
    if (!last){
        text(c, "WAITING FOR THE FIRST HOOK EVENT", x0, y, Face::Lcars, 24, kPeriwinkle);
    } else {
        // The last event: its number in a capsule, name, project, age
        char seq[16];
        snprintf(seq, sizeof(seq), "%lu", static_cast<unsigned long>(last->seq));
        const int sh = 32;
        const int sw = textWidth(c, seq, Face::Lcars, 24) + sh;
        capsule(c, x0, y, sw, sh, kGold);
        text(c, seq, x0 + sw / 2, y + sh / 2, Face::Lcars, 24, kBg, textdatum_t::middle_center, kGold);
        char age[32];
        ageText(last->receivedMs, nowMs, age, sizeof(age));
        const int ageW = text(c, age, x1, y + sh / 2, Face::Lcars, 20, kViolet, textdatum_t::middle_right);
        const int nx = x0 + sw + 12;
        const std::string name = fit(c, upper(last->event.name), Face::Lcars, 32, x1 - nx - ageW - 100);
        const int nw = text(c, name.c_str(), nx, y + sh / 2, Face::Lcars, 32, kTan, textdatum_t::middle_left);
        const char* project = last->event.find("project");
        if (project && project[0]){
            const std::string p = fit(c, upper(project), Face::Lcars, 32, x1 - nx - nw - ageW - 30);
            text(c, p.c_str(), nx + nw + 14, y + sh / 2, Face::Lcars, 32, kOrange, textdatum_t::middle_left);
        }
        y += sh + 14;

        // Every field: the key in orange, the value wrapped in blue
        const int keyW = 190;
        const int valueX = x0 + keyW;
        const int valueW = x1 - valueX;
        for (const HookField& f : last->event.fields){
            if (y + lineH > detailBottom){
                text(c, "...", x0, y, Face::Lcars, 22, kPeriwinkle);
                break;
            }
            text(c, fit(c, upper(f.key), Face::Lcars, 20, keyW - 10).c_str(), x0, y + 2, Face::Lcars, 20, kOrange);
            int room = (detailBottom - y) / lineH;
            const int maxLines = room < 3 ? (room < 1 ? 1 : room) : 3;
            const int lines = glyphs::wrap(c, f.value, valueX, y, valueW, Face::Lcars, 22, lineH, kBlue, maxLines, kPeriwinkle);
            y += lines * lineH + 2;
        }
    }

    // History, newest first
    y = detailBottom;
    endCap(c, x0, y, x1 - x0, 6, true, kDim);
    y += 14;
    text(c, "LOG", x0, y, Face::Lcars, 24, kOrange);
    char head[48];
    snprintf(head, sizeof(head), "%lu RECEIVED SINCE BOOT", static_cast<unsigned long>(log.count()));
    text(c, head, x0 + 60, y + 4, Face::Lcars, 20, kPeriwinkle);
    y += 34;
    const auto& entries = log.entries();
    int rows = 0;
    for (auto it = entries.rbegin(); it != entries.rend() && rows < historyRows; ++it, ++rows){
        char age[32];
        ageText(it->receivedMs, nowMs, age, sizeof(age));
        const char* summary = it->event.find("summary");
        const char* project = it->event.find("project");
        const char* session = it->event.find("session_id");
        const uint16_t color = rows == 0 ? kTan : kPeriwinkle;
        char seq[16];
        snprintf(seq, sizeof(seq), "%lu", static_cast<unsigned long>(it->seq));
        text(c, seq, x0 + 40, y, Face::Lcars, 22, color, textdatum_t::top_right);
        text(c, age, x0 + 60, y, Face::Lcars, 22, color);
        std::string what = summary ? summary : it->event.name;
        if (project && project[0]) what += "   " + upper(project);
        else if (session) what += std::string("   ") + std::string(session).substr(0, 8);
        text(c, fit(c, what, Face::Lcars, 22, x1 - (x0 + 190)).c_str(), x0 + 190, y, Face::Lcars, 22, color);
        y += lineH;
    }
}

}  // namespace monitor::lcars
