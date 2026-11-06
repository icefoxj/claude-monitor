// The galactic theme's views: the usage rows (the usage tile, the
// screensaver's card, the usage page), the usage page, the screensaver's
// card and the hook inspector. The same data in the same boxes as the
// classic views; only the drawing differs.

#include "monitor/galactic.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "monitor/usagepage.h"
#include "monitor/usageview.h"

namespace monitor::galactic {

namespace {

// Green, amber or red for a limit's percentage, as in the other themes
uint16_t levelColor(float percent, const std::string& severity){
    int level = percent >= 80.0f ? 2 : (percent >= 50.0f ? 1 : 0);
    if (severity == "critical") level = 2;
    else if (severity == "warning" && level < 1) level = 1;
    return level == 2 ? kRed : (level == 1 ? kAmber : kGreen);
}

uint16_t splitColor(int i){
    static constexpr uint16_t kColors[] = { kYellow, kBlue, rgb(200, 120, 255), kGreen, kGrey };
    return kColors[i % 5];
}

void dashed(LovyanGFX& g, int x0, int y0, int x1, int y1, uint16_t color){
    const float dx = static_cast<float>(x1 - x0), dy = static_cast<float>(y1 - y0);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    for (float s = 0.0f; s < len; s += 14.0f){
        const float e = s + 7.0f < len ? s + 7.0f : len;
        for (int k = 0; k < 2; k++){
            g.drawLine(x0 + static_cast<int>(dx * s / len), y0 + static_cast<int>(dy * s / len) + k,
                       x0 + static_cast<int>(dx * e / len), y0 + static_cast<int>(dy * e / len) + k, color);
        }
    }
}

// A section heading: the words, then a thin line to the column's end
void heading(LovyanGFX& g, int x, int y, int w, const char* words, uint16_t color){
    const int tw = text(g, words, x, y + 12, Face::Galactic, 24, color, textdatum_t::middle_left);
    g.drawFastHLine(x + tw + 12, y + 12, w - tw - 12, kDim);
    g.drawFastVLine(x + w - 1, y + 6, 12, kDim);
}

// A bar in a thin outline, filled to `fraction`, with a scale under it
void meter(LovyanGFX& g, int x, int y, int w, int h, float fraction, uint16_t color, bool scale){
    g.drawRect(x, y, w, h, kDim);
    if (fraction > 1.0f) fraction = 1.0f;
    if (fraction > 0.0f){
        int fw = static_cast<int>((w - 4) * fraction);
        if (fw < 2) fw = 2;
        g.fillRect(x + 2, y + 2, fw, h - 4, color);
    }
    if (scale){
        for (int k = 0; k <= 10; k++) g.drawFastVLine(x + (w - 1) * k / 10, y + h + 1, k % 5 ? 2 : 4, kDim);
    }
}

void ageText(int64_t receivedMs, int64_t nowMs, char* out, size_t size){
    long long s = (nowMs - receivedMs) / 1000;
    if (s < 0) s = 0;
    if (s < 60)        snprintf(out, size, "T+%llds", s);
    else if (s < 3600) snprintf(out, size, "T+%lldm%02llds", s / 60, s % 60);
    else               snprintf(out, size, "T+%lldh%02lldm", s / 3600, (s % 3600) / 60);
}

// One size of row, as tall as the classic ones; texts stand on baselines
struct RowStyle {
    int labelPx, pctPx, smallPx;   // smallPx 0: no reset line
    int rowH, barY, barH;
    int resetBase;
    int minWidth;
};

const RowStyle kRows[] = {
    { 26, 40, 18, 112, 52, 18, 102, 400 },
    { 22, 30, 16,  72, 32, 13,  68, 300 },
    { 18, 22, 16,  56, 25, 11,  53,   0 },
    { 18, 20,  0,  40, 25, 11,   0,   0 },
};

void splitRow(M5Canvas& c, int x, int top, int w, const RowStyle& st, const Usage& usage){
    text(c, "THIS WEEK BY APP", x, top + st.barY - 6, Face::Galactic, st.labelPx, kWhite, textdatum_t::baseline_left);
    const int n = static_cast<int>(usage.split.size() < Usage::kMaxShares ? usage.split.size() : Usage::kMaxShares);
    float total = 0.0f;
    for (int i = 0; i < n; i++) total += usage.split[i].percent;
    const int by = top + st.barY, gap = 3;
    c.drawRect(x, by, w, st.barH, kDim);
    if (total > 0.0f && n > 0){
        int sx = x + 2;
        const int inner = w - 4;
        for (int i = 0; i < n; i++){
            int sw = (i + 1 == n) ? (x + 2 + inner - sx) : static_cast<int>((inner - gap * (n - 1)) * usage.split[i].percent / total);
            if (sw <= 0) continue;
            c.fillRect(sx, by + 2, sw, st.barH - 4, splitColor(i));
            sx += sw + gap;
        }
    }
    if (!st.smallPx) return;
    int lx = x;
    for (int i = 0; i < n; i++){
        char item[48];
        snprintf(item, sizeof(item), "%s%s %d%%", i ? "   " : "", upper(usage.split[i].label).c_str(),
                 static_cast<int>(lroundf(usage.split[i].percent)));
        const int iw = textWidth(c, item, Face::Galactic, st.smallPx);
        if (lx + iw > x + w) break;
        text(c, item, lx, top + st.resetBase, Face::Galactic, st.smallPx, splitColor(i), textdatum_t::baseline_left);
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
        const int pctW = text(c, pct, x + w, by - 6, Face::Galactic, st->pctPx, level, textdatum_t::baseline_right);
        const std::string label = fit(c, upper(l.label), Face::Galactic, st->labelPx, w - pctW - 24);
        text(c, label.c_str(), x, by - 6, Face::Galactic, st->labelPx, kWhite, textdatum_t::baseline_left);

        meter(c, x, by, w, st->barH, l.percent / 100.0f, level, false);
        if (detailed){
            // Where an even pace would have the bar by now
            const UsagePace pace = usagePace(l, elapsedSec);
            if (pace.known){
                const int tx = x + static_cast<int>((w - 2) * pace.elapsed);
                c.fillRect(tx, by - 4, 2, st->barH + 8, kWhite);
                c.fillTriangle(tx - 5, by - 10, tx + 6, by - 10, tx + 1, by - 4, kWhite);
            }
        }

        // The line under the bar in the bar's colour; a warning in its own
        if (st->smallPx && detailed){
            UsageDetailText t;
            usageDetailText(l, usage, elapsedSec, t);
            const std::string pace = upper(t.pace);
            const int paceW = pace.empty() ? 0 : textWidth(c, pace.c_str(), Face::Galactic, st->smallPx) + 24;
            std::string left = upper(t.full);
            if (t.brief[0] && textWidth(c, left.c_str(), Face::Galactic, st->smallPx) > w - paceW) left = upper(t.brief);
            text(c, left.c_str(), x, top + st->resetBase, Face::Galactic, st->smallPx, level, textdatum_t::baseline_left);
            if (!pace.empty()){
                const uint16_t pc = t.tone == 2 ? kRed : (t.tone == 1 ? kAmber : level);
                text(c, pace.c_str(), x + w, top + st->resetBase, Face::Galactic, st->smallPx, pc, textdatum_t::baseline_right);
            }
        } else if (st->smallPx){
            char reset[48];
            usageResetText(l, elapsedSec, reset, sizeof(reset));
            if (reset[0]) text(c, upper(reset).c_str(), x, top + st->resetBase, Face::Galactic, st->smallPx, level, textdatum_t::baseline_left);
        }
        top += st->rowH;
    }
}

// ---------------- the usage page ----------------

namespace {

void chartBox(M5Canvas& c, const Usage& usage, int32_t elapsedSec, int bx, int by, int bw, int bh){
    UsageChart ch;
    if (!usageChart(usage, elapsedSec, ch)){
        text(c, "NO WEEKLY DATA", bx, by + 8, Face::Galactic, 20, kAmber);
        return;
    }
    const int px = bx + 58, py = by + 10;
    const int pw = bw - 58 - 10, ph = bh - 10 - 36;
    brackets(c, px - 6, py - 6, pw + 12, ph + 12, 14, 2, kYellow);
    auto X = [&](int64_t t) { return px + static_cast<int>(static_cast<double>(pw) * (t - ch.start) / (ch.end - ch.start)); };
    auto Y = [&](float v) { return py + ph - static_cast<int>(ph * v / ch.yMax); };

    for (int v = 25; v <= static_cast<int>(ch.yMax); v += 25){
        const int y = Y(static_cast<float>(v));
        for (int x = px; x < px + pw; x += 6) c.drawFastHLine(x, y, 3, v == 100 ? kAmber : kDim);
        char t[16];
        snprintf(t, sizeof(t), "%d%%", v);
        text(c, t, px - 12, y, Face::Galactic, 16, kAmber, textdatum_t::middle_right);
    }
    c.drawFastHLine(px, py + ph, pw, kDim);

    // Local midnights, the weekday under each day (today in white)
    const int32_t off = usage.utcOffset;
    const int todayWd = usage.nowUnix ? usageWeekday(ch.now, off) : -1;
    int64_t dayStart = ch.start;
    int64_t midnight = ch.start + (86400 - usageSecondsOfDay(ch.start, off));
    while (dayStart < ch.end){
        const int64_t dayEnd = midnight < ch.end ? midnight : ch.end;
        if (midnight < ch.end){
            for (int y = py; y < py + ph; y += 6) c.drawFastVLine(X(midnight), y, 3, kDim);
        }
        const int x0 = X(dayStart), x1 = X(dayEnd);
        if (x1 - x0 >= 40){
            const int wd = usageWeekday(dayStart, off);
            const bool today = wd == todayWd && ch.now >= dayStart && ch.now < dayEnd;
            text(c, upper(usageWeekdayName(wd)).c_str(), (x0 + x1) / 2, py + ph + 12, Face::Galactic, 18,
                 today ? kWhite : kAmber, textdatum_t::top_center);
        }
        dayStart = dayEnd;
        midnight += 86400;
    }

    // The even pace; the usage so far, filled underneath, the line on top
    dashed(c, X(ch.start), Y(0.0f), X(ch.end), Y(100.0f), kGrey);
    const uint16_t curve = ch.week ? levelColor(ch.week->percent, ch.week->severity) : kYellow;
    const uint16_t fill = shade(curve, 26);
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

    // Where the pace leads by the reset; now, under a targeting cross
    if (ch.week && ch.pace.valid && ch.pace.projected > ch.week->percent){
        const int yNow = Y(ch.week->percent);
        int x1 = X(ch.end);
        float v1 = ch.pace.projected;
        if (v1 > ch.yMax){
            v1 = ch.yMax;
            const float f = (ch.yMax - ch.week->percent) / (ch.pace.projected - ch.week->percent);
            x1 = xNow + static_cast<int>((X(ch.end) - xNow) * f);
        }
        const uint16_t pc = ch.pace.projected > 100.0f ? kRed : kAmber;
        dashed(c, xNow, yNow, x1, Y(v1), pc);
        char t[16];
        snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(ch.pace.projected)));
        if (Y(v1) - 30 < py) text(c, t, x1 - 6, Y(v1) + 6, Face::Galactic, 22, pc, textdatum_t::top_right);
        else                 text(c, t, x1 - 2, Y(v1) - 6, Face::Galactic, 22, pc, textdatum_t::bottom_right);
    }
    if (ch.week){
        const int yNow = Y(ch.week->percent);
        circle(c, xNow, yNow, 8, 2, curve);
        c.drawFastHLine(xNow - 14, yNow, 5, curve);
        c.drawFastHLine(xNow + 10, yNow, 5, curve);
        c.drawFastVLine(xNow, yNow - 14, 5, curve);
        c.drawFastVLine(xNow, yNow + 10, 5, curve);
    }
    if (ch.noHistory){
        text(c, "NO READINGS YET: THE LINE IS THE AVERAGE SO FAR", px + 8, py + 4, Face::Galactic, 16, kAmber);
    }
}

void projectsBox(M5Canvas& c, const Usage& usage, int bx, int by, int bw, int bh){
    if (usage.projects.empty()){
        text(c, "NO CLAUDE CODE TRANSCRIPTS READ THIS WEEK", bx, by + 8, Face::Galactic, 18, kAmber);
        return;
    }
    const int todayR = bx + bw;
    const int weekR  = todayR - 96;
    const int nameW  = bw * 30 / 100;
    const int barX   = bx + nameW + 12;
    const int barW   = weekR - 86 - barX;
    text(c, "WEEK", weekR, by, Face::Galactic, 16, kAmber, textdatum_t::top_right);
    text(c, "TODAY", todayR, by, Face::Galactic, 16, kAmber, textdatum_t::top_right);

    float maxWeek = 0.0f;
    for (const UsageProject& p : usage.projects) if (p.week > maxWeek) maxWeek = p.week;
    const int n = static_cast<int>(usage.projects.size());
    int rowH = (bh - 26) / (n > 0 ? n : 1);
    if (rowH > 42) rowH = 42;
    int y = by + 26;
    for (const UsageProject& p : usage.projects){
        const bool other = p.name == "other";
        const int cy = y + rowH / 2;
        const std::string name = fit(c, other ? "OTHER PROJECTS" : upper(p.name), Face::Galactic, 20, nameW);
        text(c, name.c_str(), bx, cy, Face::Galactic, 20, other ? kGrey : kWhite, textdatum_t::middle_left);
        meter(c, barX, cy - 7, barW, 14, maxWeek > 0.0f ? p.week / maxWeek : 0.0f, other ? kGrey : kYellow, false);
        char t[16];
        if (p.week > 0.0f && p.week < 1.0f) snprintf(t, sizeof(t), "<1%%");
        else snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(p.week)));
        text(c, t, weekR, cy, Face::Galactic, 26, kYellow, textdatum_t::middle_right);
        if (p.today <= 0.0f) snprintf(t, sizeof(t), "-");
        else if (p.today < 1.0f) snprintf(t, sizeof(t), "<1%%");
        else snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(p.today)));
        text(c, t, todayR, cy, Face::Galactic, 18, kAmber, textdatum_t::middle_right);
        y += rowH;
    }
}

void planBox(M5Canvas& c, const Usage& usage, int bx, int by, int bw){
    std::string plan, extra;
    int planTone = 0, extraTone = 0;
    const bool havePlan = usagePlanText(usage, plan, planTone);
    const bool haveExtra = usageExtraText(usage, extra, extraTone);
    if (!havePlan && !haveExtra) return;
    heading(c, bx, by, bw, "PLAN", kYellow);
    const int labelW = 170;
    int y = by + 48;
    auto row = [&](const char* label, const std::string& value, int tone) {
        text(c, label, bx, y, Face::Galactic, 16, kAmber, textdatum_t::middle_left);
        const uint16_t color = tone < 0 ? kGrey : (tone == 2 ? kRed : (tone == 1 ? kAmber : kWhite));
        const std::string v = fit(c, upper(value), Face::Galactic, 22, bw - labelW);
        text(c, v.c_str(), bx + labelW, y, Face::Galactic, 22, color, textdatum_t::middle_left);
        y += 34;
    };
    if (havePlan) row("PLAN", plan, planTone);
    if (haveExtra) row("EXTRA USAGE", extra, extraTone);
}

}  // namespace

void usagePage(M5Canvas& c, const Usage& usage, int32_t elapsedSec){
    const int W = c.width(), H = c.height();
    c.fillScreen(kBg);
    brackets(c, 6, 6, W - 12, H - 12, 44, 3, kYellow);

    // Header: the title, the subtitle, the age left of the theme switch,
    // a scale under them
    const int M = 24;
    const UsageSwitch sw = usageThemeSwitch(W);
    const int tw = text(c, "CLAUDE USAGE", M, 58, Face::Galactic, 44, kYellow, textdatum_t::baseline_left);
    text(c, "PLAN TELEMETRY", M + tw + 18, 58, Face::Galactic, 18, kAmber, textdatum_t::baseline_left);
    char age[40];
    usageAgeText(usage.ageSec + elapsedSec, age, sizeof(age));
    text(c, upper(age).c_str(), sw.x - 24, 58, Face::Galactic, 18, kGreen, textdatum_t::baseline_right);
    c.fillRect(M, 74, 200, 3, kYellow);
    c.drawFastHLine(M, 76, W - 2 * M, kDim);
    for (int k = M; k <= W - M; k += 20) c.drawFastVLine(k, 77, (k - M) % 100 ? 3 : 6, kDim);
    drawThemeSwitch(c, sw);

    const int top = 98;
    const int rows = usageRowCount(usage);
    const int planH = 100;
    if (W > H){
        const int colW = (W - 3 * M) / 2;
        const int rx = 2 * M + colW;
        heading(c, M, top, colW, "LIMITS", kYellow);
        int rowsH = usageRowsHeight(rows);
        if (rowsH > H - (top + 34) - planH - 18) rowsH = H - (top + 34) - planH - 18;
        if (rows > 0) usageRows(c, M, top + 34, colW, rowsH, usage, elapsedSec, true);
        planBox(c, usage, M, H - planH - 12, colW);

        const int chartH = 290;
        heading(c, rx, top, colW, "THIS WEEK", kYellow);
        chartBox(c, usage, elapsedSec, rx, top + 38, colW, chartH);
        const int py = top + 38 + chartH + 14;
        heading(c, rx, py, colW - 290, "BY PROJECT", kYellow);
        text(c, "EST. SHARE OF THE WEEKLY LIMIT", rx + colW, py + 12, Face::Galactic, 16, kAmber, textdatum_t::middle_right);
        projectsBox(c, usage, rx, py + 32, colW, H - (py + 32) - 16);
    } else {
        const int w = W - 2 * M;
        heading(c, M, top, w, "LIMITS", kYellow);
        int rowsH = usageRowsHeight(rows);
        if (rowsH > 460) rowsH = 460;
        if (rows > 0) usageRows(c, M, top + 34, w, rowsH, usage, elapsedSec, true);
        int y = top + 34 + rowsH + 10;
        planBox(c, usage, M, y, w);
        y += planH + 8;
        const int chartH = 300;
        heading(c, M, y, w, "THIS WEEK", kYellow);
        chartBox(c, usage, elapsedSec, M, y + 38, w, chartH);
        y += 38 + chartH + 14;
        heading(c, M, y, w, "BY PROJECT", kYellow);
        projectsBox(c, usage, M, y + 32, w, H - (y + 32) - 16);
    }
}

// ---------------- the screensaver's card ----------------

void saverCard(M5Canvas& c, int x, int y, int w, int h, int pad, int header,
               const Usage& usage, int32_t elapsedSec, bool data){
    c.fillScreen(kBg);
    brackets(c, x, y, w, h, 28, 3, kYellow);
    const int x1 = x + w - pad;
    // Everything stays inside the card: its border must stay black, or the
    // drift leaves a trail
    text(c, "CLAUDE USAGE", x + pad, y + 36, Face::Galactic, 26, kYellow, textdatum_t::baseline_left);
    if (!data){
        text(c, "NO USAGE DATA YET", x + pad, y + header + 16, Face::Galactic, 26, kWhite);
        text(c, "WAITING FOR THE HOST TO SEND IT", x + pad, y + header + 56, Face::Galactic, 18, kAmber);
        return;
    }
    char age[40];
    usageAgeText(usage.ageSec + elapsedSec, age, sizeof(age));
    text(c, upper(age).c_str(), x1, y + 36, Face::Galactic, 16, kGreen, textdatum_t::baseline_right);
    c.drawFastHLine(x + pad, y + 46, x1 - x - pad, kDim);
    usageRows(c, x + pad, y + header, x1 - x - pad, usageRowsHeight(usageRowCount(usage)), usage, elapsedSec, false);
}

// ---------------- the hook inspector ----------------

void inspector(M5Canvas& c, const EventLog& log, const char* statusLine, int64_t nowMs){
    const int W = c.width(), H = c.height();
    const int m = 18;
    c.fillScreen(kBg);
    brackets(c, 4, 4, W - 8, H - 8, 34, 3, kYellow);

    // Header: what this is, the status under it, a scale
    text(c, "HOOK MONITOR", m, 46, Face::Galactic, 34, kYellow, textdatum_t::baseline_left);
    if (statusLine && statusLine[0]){
        const std::string st = fit(c, upper(statusLine), Face::Galactic, 16, W / 2);
        text(c, st.c_str(), W - m, 46, Face::Galactic, 16, kGreen, textdatum_t::baseline_right);
    }
    c.drawFastHLine(m, 58, W - 2 * m, kDim);
    for (int k = m; k <= W - m; k += 20) c.drawFastVLine(k, 59, 3, kDim);

    const int lineH = 26;
    const int historyRows = 8;
    const int historyH = (historyRows + 1) * lineH + 24;
    const int detailBottom = H - historyH - 8;
    int y = 76;

    const EventEntry* last = log.last();
    if (!last){
        text(c, "WAITING FOR THE FIRST HOOK EVENT", m, y, Face::Galactic, 20, kAmber);
    } else {
        // The last event: its number boxed, name, project, age
        char seq[16];
        snprintf(seq, sizeof(seq), "%lu", static_cast<unsigned long>(last->seq));
        const int bw = boxed(c, seq, m, y + 16, Face::Galactic, 20, kYellow, false, 6);
        char age[32];
        ageText(last->receivedMs, nowMs, age, sizeof(age));
        const int ageW = text(c, age, W - m, y + 16, Face::Galactic, 16, kGreen, textdatum_t::middle_right);
        const int nx = m + bw + 12;
        const std::string name = fit(c, upper(last->event.name), Face::Galactic, 26, W - m - nx - ageW - 110);
        const int nw = text(c, name.c_str(), nx, y + 16, Face::Galactic, 26, kWhite, textdatum_t::middle_left);
        const char* project = last->event.find("project");
        if (project && project[0]){
            const std::string p = fit(c, upper(project), Face::Galactic, 26, W - m - nx - nw - ageW - 30);
            text(c, p.c_str(), nx + nw + 14, y + 16, Face::Galactic, 26, kAmber, textdatum_t::middle_left);
        }
        y += 46;

        // Every field: the key in amber, the value wrapped in green
        const int keyW = 230;
        const int valueX = m + keyW;
        const int valueW = W - valueX - m;
        for (const HookField& f : last->event.fields){
            if (y + lineH > detailBottom){
                text(c, "...", m, y, Face::Galactic, 20, kAmber);
                break;
            }
            text(c, fit(c, upper(f.key), Face::Galactic, 16, keyW - 10).c_str(), m, y + 2, Face::Galactic, 16, kAmber);
            int room = (detailBottom - y) / lineH;
            const int maxLines = room < 3 ? (room < 1 ? 1 : room) : 3;
            const int lines = glyphs::wrap(c, f.value, valueX, y, valueW, Face::Galactic, 18, lineH, kGreen, maxLines, kGrey);
            y += lines * lineH + 2;
        }
    }

    // History, newest first
    y = detailBottom;
    c.drawFastHLine(m, y, W - 2 * m, kDim);
    y += 12;
    const int lw = text(c, "LOG", m, y, Face::Galactic, 20, kYellow);
    char head[48];
    snprintf(head, sizeof(head), "%lu RECEIVED SINCE BOOT", static_cast<unsigned long>(log.count()));
    text(c, head, m + lw + 14, y + 3, Face::Galactic, 16, kAmber);
    y += lineH + 4;
    const auto& entries = log.entries();
    int rows = 0;
    for (auto it = entries.rbegin(); it != entries.rend() && rows < historyRows; ++it, ++rows){
        char age[32];
        ageText(it->receivedMs, nowMs, age, sizeof(age));
        const char* summary = it->event.find("summary");
        const char* project = it->event.find("project");
        const char* session = it->event.find("session_id");
        const uint16_t color = rows == 0 ? kWhite : shade(kGreen, 75);
        char seq[16];
        snprintf(seq, sizeof(seq), "%lu", static_cast<unsigned long>(it->seq));
        text(c, seq, m + 34, y, Face::Galactic, 18, color, textdatum_t::top_right);
        text(c, age, m + 52, y, Face::Galactic, 18, color);
        std::string what = summary ? summary : it->event.name;
        if (project && project[0]) what += "   " + upper(project);
        else if (session) what += std::string("   ") + std::string(session).substr(0, 8);
        text(c, fit(c, what, Face::Galactic, 18, W - m - (m + 170)).c_str(), m + 170, y, Face::Galactic, 18, color);
        y += lineH;
    }
}

}  // namespace monitor::galactic
