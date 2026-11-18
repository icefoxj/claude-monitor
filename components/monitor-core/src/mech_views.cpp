// The mech theme's views: the usage rows (the usage tile, the
// screensaver's card, the usage page), the usage page, the screensaver's
// card and the hook inspector. The same data in the same boxes as the
// classic views, laid out like the Studio apps: a title band with the unit
// code, house panels with their tabs, segmented gauges, the stroke font for
// words and a monospace face for the hook data.

#include "monitor/mech.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "monitor/usagepage.h"
#include "monitor/usageview.h"

namespace monitor::mech {

namespace {

inline int ip(float v){ return static_cast<int>(lroundf(v)); }

// Green, red-orange or red for a limit's percentage (the other themes: amber for the middle)
uint16_t levelColor(float percent, const std::string& severity){
    int level = percent >= 80.0f ? 2 : (percent >= 50.0f ? 1 : 0);
    if (severity == "critical") level = 2;
    else if (severity == "warning" && level < 1) level = 1;
    return level == 2 ? kRed : (level == 1 ? kWarn : kGreen);
}

uint16_t splitColor(int i){
    static constexpr uint16_t kColors[] = { kYellow, kCyan, kViolet, kGreen, kDim };
    return kColors[i % 5];
}

uint16_t toneColor(int tone, uint16_t plain){
    return tone < 0 ? kDim : (tone == 2 ? kRed : (tone == 1 ? kWarn : plain));
}

// A gauge: a chamfered track of slanted blocks, each lit in color(i, n)
// or left dark
template <class Color>
void blocks(LovyanGFX& g, int x, int y, int w, int h, Color color){
    const int cut = std::max(2, h / 3);
    plate(g, x, y, w, h, cut, kField);
    edge(g, x, y, w, h, cut, kLine);
    const int pad = std::max(2, h / 5);
    const float ih = static_cast<float>(h - 2 * pad);
    if (ih < 2.0f) return;
    const float sl = ih * 0.45f;
    const float gap = std::max(2.0f, ih * 0.3f);
    const float want = std::max(3.0f, ih * 0.9f);
    const float inner = w - 2.0f * pad - sl;
    const int n = std::max(1, static_cast<int>((inner + gap) / (want + gap)));
    const float bw = (inner - (n - 1) * gap) / n;
    const float top = static_cast<float>(y + pad), bot = top + ih - 1.0f;
    const uint16_t dark = mix(kField, kLine, 0.55f);
    for (int i = 0; i < n; i++){
        const float bx = x + pad + i * (bw + gap);
        int32_t lit = color(i, n);
        const uint16_t c = lit < 0 ? dark : static_cast<uint16_t>(lit);
        g.fillTriangle(ip(bx + sl), ip(top), ip(bx + sl + bw), ip(top), ip(bx + bw), ip(bot), c);
        g.fillTriangle(ip(bx + sl), ip(top), ip(bx + bw), ip(bot), ip(bx), ip(bot), c);
    }
}

void gauge(LovyanGFX& g, int x, int y, int w, int h, float fraction, uint16_t color){
    if (fraction > 1.0f) fraction = 1.0f;
    blocks(g, x, y, w, h, [&](int i, int n) -> int32_t {
        int lit = ip(n * fraction);
        if (fraction > 0.0f && lit == 0) lit = 1;
        return i < lit ? color : -1;
    });
}

void dashed(LovyanGFX& g, int x0, int y0, int x1, int y1, uint16_t color){
    const float dx = static_cast<float>(x1 - x0), dy = static_cast<float>(y1 - y0);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    for (float s = 0.0f; s < len; s += 14.0f){
        const float e = s + 8.0f < len ? s + 8.0f : len;
        for (int k = 0; k < 2; k++){
            g.drawLine(x0 + static_cast<int>(dx * s / len), y0 + static_cast<int>(dy * s / len) + k,
                       x0 + static_cast<int>(dx * e / len), y0 + static_cast<int>(dy * e / len) + k, color);
        }
    }
}

void ageText(int64_t receivedMs, int64_t nowMs, char* out, size_t size){
    long long s = (nowMs - receivedMs) / 1000;
    if (s < 0) s = 0;
    if (s < 60)        snprintf(out, size, "%llds", s);
    else if (s < 3600) snprintf(out, size, "%lldm%02llds", s / 60, s % 60);
    else               snprintf(out, size, "%lldh%02lldm", s / 3600, (s % 3600) / 60);
}

// One size of row, as tall as the classic ones; texts stand on baselines
struct RowStyle {
    float labelCap, pctCap, smallCap;   // smallCap 0: no reset line
    int rowH, barY, barH;
    int resetBase;
    int minWidth;
};

const RowStyle kRows[] = {
    { 17, 28, 12, 112, 52, 18, 100, 400 },
    { 14, 21, 11,  72, 33, 13,  67, 300 },
    { 12, 16, 10,  56, 26, 11,  52,   0 },
    { 12, 14,  0,  40, 26, 11,   0,   0 },
};

void splitRow(M5Canvas& c, int x, int top, int w, const RowStyle& st, const Usage& usage){
    const int base = top + st.barY - (st.rowH >= 72 ? 8 : 6);
    drawText(c, "THIS WEEK BY APP", static_cast<float>(x), static_cast<float>(base), st.labelCap, kWhite);
    const int n = static_cast<int>(usage.split.size() < Usage::kMaxShares ? usage.split.size() : Usage::kMaxShares);
    float total = 0.0f;
    for (int i = 0; i < n; i++) total += usage.split[i].percent;
    blocks(c, x, top + st.barY, w, st.barH, [&](int i, int blocksN) -> int32_t {
        if (total <= 0.0f || n == 0) return -1;
        const float f = (i + 0.5f) / blocksN * total;
        float acc = 0.0f;
        for (int k = 0; k < n; k++){
            acc += usage.split[k].percent;
            if (f <= acc) return splitColor(k);
        }
        return splitColor(n - 1);
    });
    if (st.smallCap <= 0.0f) return;
    float lx = static_cast<float>(x);
    for (int i = 0; i < n; i++){
        char item[48];
        snprintf(item, sizeof(item), "%s %d%%", upper(usage.split[i].label).c_str(),
                 static_cast<int>(lroundf(usage.split[i].percent)));
        const float iw = textWidth(item, st.smallCap);
        if (lx + iw > x + w) break;
        drawText(c, item, lx, static_cast<float>(top + st.resetBase), st.smallCap, splitColor(i));
        lx += iw + st.smallCap * 2.0f;
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
        const float base = static_cast<float>(by - (st->rowH >= 72 ? 8 : 6));

        char pct[12];
        snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(lroundf(l.percent)));
        const int pctW = drawText(c, pct, static_cast<float>(x + w), base, st->pctCap, level, Align::Right);
        const std::string label = fitText(upper(l.label), st->labelCap, static_cast<float>(w - pctW - 24));
        drawText(c, label.c_str(), static_cast<float>(x), base, st->labelCap, kWhite);

        gauge(c, x, by, w, st->barH, l.percent / 100.0f, level);
        if (detailed){
            // Where an even pace would have the gauge by now
            const UsagePace pace = usagePace(l, elapsedSec);
            if (pace.known){
                const int tx = x + static_cast<int>((w - 2) * pace.elapsed);
                c.fillRect(tx, by - 4, 2, st->barH + 8, kWhite);
                c.fillTriangle(tx - 5, by - 10, tx + 6, by - 10, tx + 1, by - 4, kWhite);
            }
        }

        // The line under the gauge in its colour; a warning in its own
        const float sc = st->smallCap;
        const float rb = static_cast<float>(top + st->resetBase);
        if (sc > 0.0f && detailed){
            UsageDetailText t;
            usageDetailText(l, usage, elapsedSec, t);
            const std::string pace = upper(t.pace);
            const float paceW = pace.empty() ? 0.0f : textWidth(pace.c_str(), sc) + 24.0f;
            std::string left = upper(t.full);
            if (t.brief[0] && textWidth(left.c_str(), sc) > w - paceW) left = upper(t.brief);
            drawText(c, left.c_str(), static_cast<float>(x), rb, sc, level);
            if (!pace.empty()){
                drawText(c, pace.c_str(), static_cast<float>(x + w), rb, sc, toneColor(t.tone, level), Align::Right);
            }
        } else if (sc > 0.0f){
            char reset[48];
            usageResetText(l, elapsedSec, reset, sizeof(reset));
            if (reset[0]) drawText(c, reset, static_cast<float>(x), rb, sc, level);
        }
        top += st->rowH;
    }
}

// ---------------- the usage page ----------------

namespace {

void chartBox(M5Canvas& c, const Usage& usage, int32_t elapsedSec, int bx, int by, int bw, int bh){
    UsageChart ch;
    if (!usageChart(usage, elapsedSec, ch)){
        drawText(c, "NO WEEKLY DATA", static_cast<float>(bx), static_cast<float>(by + 24), 13, kWarn);
        return;
    }
    const int px = bx + 52, py = by + 10;
    const int pw = bw - 52 - 10, ph = bh - 10 - 34;
    brackets(c, px - 7, py - 7, pw + 14, ph + 14, 16, 2, kYellow);
    auto X = [&](int64_t t) { return px + static_cast<int>(static_cast<double>(pw) * (t - ch.start) / (ch.end - ch.start)); };
    auto Y = [&](float v) { return py + ph - static_cast<int>(ph * v / ch.yMax); };

    for (int v = 25; v <= static_cast<int>(ch.yMax); v += 25){
        const int y = Y(static_cast<float>(v));
        for (int x = px; x < px + pw; x += 6) c.drawFastHLine(x, y, 3, v == 100 ? kWarn : kLine);
        char t[16];
        snprintf(t, sizeof(t), "%d%%", v);
        drawText(c, t, static_cast<float>(px - 12), y + 5.0f, 10, kDim, Align::Right);
    }
    c.drawFastHLine(px, py + ph, pw, kLine);

    // Local midnights, the weekday under each day (today in white)
    const int32_t off = usage.utcOffset;
    const int todayWd = usage.nowUnix ? usageWeekday(ch.now, off) : -1;
    int64_t dayStart = ch.start;
    int64_t midnight = ch.start + (86400 - usageSecondsOfDay(ch.start, off));
    while (dayStart < ch.end){
        const int64_t dayEnd = midnight < ch.end ? midnight : ch.end;
        if (midnight < ch.end){
            for (int y = py; y < py + ph; y += 6) c.drawFastVLine(X(midnight), y, 3, kLine);
        }
        const int x0 = X(dayStart), x1 = X(dayEnd);
        if (x1 - x0 >= 40){
            const int wd = usageWeekday(dayStart, off);
            const bool today = wd == todayWd && ch.now >= dayStart && ch.now < dayEnd;
            drawText(c, usageWeekdayName(wd), (x0 + x1) * 0.5f, static_cast<float>(py + ph + 24), 11,
                     today ? kWhite : kDim, Align::Center);
        }
        dayStart = dayEnd;
        midnight += 86400;
    }

    // The even pace; the usage so far, filled underneath, the line on top
    dashed(c, X(ch.start), Y(0.0f), X(ch.end), Y(100.0f), kDim);
    const uint16_t curve = ch.week ? levelColor(ch.week->percent, ch.week->severity) : kYellow;
    const uint16_t fill = mix(kPanel, curve, 0.22f);
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

    // Where the pace leads by the reset; now, under a reticle
    if (ch.week && ch.pace.valid && ch.pace.projected > ch.week->percent){
        const int yNow = Y(ch.week->percent);
        int x1 = X(ch.end);
        float v1 = ch.pace.projected;
        if (v1 > ch.yMax){
            v1 = ch.yMax;
            const float f = (ch.yMax - ch.week->percent) / (ch.pace.projected - ch.week->percent);
            x1 = xNow + static_cast<int>((X(ch.end) - xNow) * f);
        }
        const uint16_t pc = ch.pace.projected > 100.0f ? kRed : kWarn;
        dashed(c, xNow, yNow, x1, Y(v1), pc);
        char t[16];
        snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(ch.pace.projected)));
        if (Y(v1) - 30 < py) drawText(c, t, static_cast<float>(x1 - 6), Y(v1) + 22.0f, 13, pc, Align::Right);
        else                 drawText(c, t, static_cast<float>(x1 - 2), Y(v1) - 8.0f, 13, pc, Align::Right);
    }
    if (ch.week){
        const int yNow = Y(ch.week->percent);
        c.drawCircle(xNow, yNow, 8, curve);
        c.drawCircle(xNow, yNow, 7, curve);
        c.drawFastHLine(xNow - 14, yNow, 5, curve);
        c.drawFastHLine(xNow + 10, yNow, 5, curve);
        c.drawFastVLine(xNow, yNow - 14, 5, curve);
        c.drawFastVLine(xNow, yNow + 10, 5, curve);
        diamond(c, xNow, yNow, 3, kWhite);
    }
    if (ch.noHistory){
        drawText(c, "NO READINGS YET: THE LINE IS THE AVERAGE SO FAR", static_cast<float>(px + 10),
                 static_cast<float>(py + 20), 10, kWarn);
    }
}

void projectsBox(M5Canvas& c, const Usage& usage, int bx, int by, int bw, int bh){
    if (usage.projects.empty()){
        drawText(c, "NO CLAUDE CODE TRANSCRIPTS READ THIS WEEK", static_cast<float>(bx), static_cast<float>(by + 20), 11, kWarn);
        return;
    }
    const int todayR = bx + bw;
    const int weekR  = todayR - 90;
    const int nameW  = bw * 30 / 100;
    const int barX   = bx + nameW + 12;
    const int barW   = weekR - 80 - barX;
    drawText(c, "WEEK", static_cast<float>(weekR), static_cast<float>(by + 10), 9, kDim, Align::Right);
    drawText(c, "TODAY", static_cast<float>(todayR), static_cast<float>(by + 10), 9, kDim, Align::Right);

    float maxWeek = 0.0f;
    for (const UsageProject& p : usage.projects) if (p.week > maxWeek) maxWeek = p.week;
    const int n = static_cast<int>(usage.projects.size());
    int rowH = (bh - 22) / (n > 0 ? n : 1);
    if (rowH > 40) rowH = 40;
    int y = by + 22;
    for (const UsageProject& p : usage.projects){
        const bool other = p.name == "other";
        const int cy = y + rowH / 2;
        const std::string name = fitText(other ? "OTHER PROJECTS" : upper(p.name), 12, static_cast<float>(nameW));
        drawText(c, name.c_str(), static_cast<float>(bx), cy + 6.0f, 12, other ? kDim : kWhite);
        gauge(c, barX, cy - 6, barW, 12, maxWeek > 0.0f ? p.week / maxWeek : 0.0f, other ? kDim : kYellow);
        char t[16];
        if (p.week > 0.0f && p.week < 1.0f) snprintf(t, sizeof(t), "<1%%");
        else snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(p.week)));
        drawText(c, t, static_cast<float>(weekR), cy + 8.0f, 16, other ? kDim : kYellow, Align::Right);
        if (p.today <= 0.0f) snprintf(t, sizeof(t), "-");
        else if (p.today < 1.0f) snprintf(t, sizeof(t), "<1%%");
        else snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(p.today)));
        drawText(c, t, static_cast<float>(todayR), cy + 6.0f, 12, kDim, Align::Right);
        y += rowH;
    }
}

// The plan and extra usage, two label/value rows from (bx, by)
void planRows(M5Canvas& c, const std::string& plan, int planTone, bool havePlan,
              const std::string& extra, int extraTone, bool haveExtra, int bx, int by, int bw){
    const int labelW = 150;
    float y = static_cast<float>(by);
    auto row = [&](const char* label, const std::string& value, int tone) {
        drawText(c, label, static_cast<float>(bx), y, 10, kDim);
        const std::string v = fitText(upper(value), 13, static_cast<float>(bw - labelW));
        drawText(c, v.c_str(), static_cast<float>(bx + labelW), y + 1.0f, 13, toneColor(tone, kWhite));
        y += 30.0f;
    };
    if (havePlan) row("PLAN", plan, planTone);
    if (haveExtra) row("EXTRA USAGE", extra, extraTone);
}

}  // namespace

void usagePage(M5Canvas& c, const Usage& usage, int32_t elapsedSec){
    const int W = c.width(), H = c.height();
    c.fillScreen(kBg);

    // The title bar: the unit code, the title, the subtitle, dim slashes,
    // the age left of the theme switch
    const UsageSwitch sw = usageThemeSwitch(W);
    int x = titleBand(c, kUnitCode, 0, 0, 46);
    x += drawText(c, "// CLAUDE USAGE", static_cast<float>(x), 33.0f, 20, kWhite) + 16;
    x += dashes(c, x, 23, 4, 1.4f, kYellow, kLine) + 16;
    x += drawText(c, "PLAN TELEMETRY", static_cast<float>(x), 29.0f, 11, kDim) + 18;
    char age[40];
    usageAgeText(usage.ageSec + elapsedSec, age, sizeof(age));
    const int ageW = drawText(c, age, static_cast<float>(sw.x - 24), 29.0f, 11, kDim, Align::Right);
    const int hx1 = sw.x - 24 - ageW - 18;
    if (hx1 - x > 40) hazard(c, x, 18, hx1 - x, 10, mix(kBg, kLine, 0.7f), 12);
    c.drawFastHLine(0, 52, sw.x - 18, kLine);
    drawThemeSwitch(c, sw);

    const int M = 16, gap = 12;
    const int top = 84;
    const int rows = usageRowCount(usage);
    std::string plan, extra;
    int planTone = 0, extraTone = 0;
    const bool havePlan = usagePlanText(usage, plan, planTone);
    const bool haveExtra = usageExtraText(usage, extra, extraTone);
    const int planH = (havePlan || haveExtra) ? 40 + 30 * ((havePlan ? 1 : 0) + (haveExtra ? 1 : 0)) + 8 : 0;

    auto limitsPanel = [&](int px, int py, int pw, int ph) {
        const int ct = panel(c, px, py, pw, ph, "LIMITS");
        if (rows > 0) usageRows(c, px + 16, ct + 8, pw - 32, py + ph - ct - 20, usage, elapsedSec, true);
    };
    auto planPanel = [&](int px, int py, int pw, int ph) {
        const int ct = panel(c, px, py, pw, ph, "PLAN");
        planRows(c, plan, planTone, havePlan, extra, extraTone, haveExtra, px + 16, ct + 22, pw - 32);
    };
    auto weekPanel = [&](int px, int py, int pw, int ph) {
        const int ct = panel(c, px, py, pw, ph, "THIS WEEK");
        chartBox(c, usage, elapsedSec, px + 16, ct + 8, pw - 32, py + ph - ct - 18);
    };
    auto projectsPanel = [&](int px, int py, int pw, int ph) {
        const int ct = panel(c, px, py, pw, ph, "BY PROJECT", "EST. SHARE OF THE WEEKLY LIMIT");
        projectsBox(c, usage, px + 16, ct + 4, pw - 32, py + ph - ct - 14);
    };

    if (W > H){
        const int colW = (W - 2 * M - gap) / 2;
        const int rx = M + colW + gap;
        const int limitsH = H - M - top - (planH ? planH + gap : 0);
        limitsPanel(M, top, colW, limitsH);
        if (planH) planPanel(M, H - M - planH, colW, planH);
        const int weekH = 340;
        weekPanel(rx, top, colW, weekH);
        projectsPanel(rx, top + weekH + gap, colW, H - M - (top + weekH + gap));
    } else {
        const int w = W - 2 * M;
        int rowsH = usageRowsHeight(rows);
        if (rowsH > 460) rowsH = 460;
        int y = top;
        const int limitsH = rowsH + 31 + 28;
        limitsPanel(M, y, w, limitsH);
        y += limitsH + gap;
        if (planH){ planPanel(M, y, w, planH); y += planH + gap; }
        const int weekH = 360;
        weekPanel(M, y, w, weekH);
        y += weekH + gap;
        projectsPanel(M, y, w, H - M - y);
    }
}

// ---------------- the screensaver's card ----------------

void saverCard(M5Canvas& c, int x, int y, int w, int h, int pad, int header,
               const Usage& usage, int32_t elapsedSec, bool data){
    // Everything stays inside the card: its border must stay black, or the
    // drift leaves a trail
    c.fillScreen(TFT_BLACK);
    const int cut = 18;
    plate(c, x, y, w, h, cut, kPanel);
    edge(c, x, y, w, h, cut, kLine);
    tab(c, x + 1, y + 1, cut, 5, y + 50, kYellow);
    const int x1 = x + w - pad;
    drawText(c, "CLAUDE USAGE", static_cast<float>(x + pad), static_cast<float>(y + 38), 18, kWhite);
    if (!data){
        drawText(c, "NO USAGE DATA YET", static_cast<float>(x + pad), static_cast<float>(y + header + 30), 16, kWhite);
        drawText(c, "WAITING FOR THE HOST TO SEND IT", static_cast<float>(x + pad), static_cast<float>(y + header + 60), 11, kDim);
        return;
    }
    char age[40];
    usageAgeText(usage.ageSec + elapsedSec, age, sizeof(age));
    drawText(c, age, static_cast<float>(x1), static_cast<float>(y + 38), 11, kDim, Align::Right);
    const int dw = 60;
    c.drawFastHLine(x + pad, y + 52, x1 - x - pad - dw - 10, kLine);
    dashes(c, x1 - dw, y + 52, 4, 1.2f, kYellow, kLine);
    usageRows(c, x + pad, y + header, x1 - x - pad, usageRowsHeight(usageRowCount(usage)), usage, elapsedSec, false);
}

// ---------------- the hook inspector ----------------

void inspector(M5Canvas& c, const EventLog& log, const char* statusLine, int64_t nowMs){
    const int W = c.width(), H = c.height();
    const int M = 12;
    c.fillScreen(kBg);

    // The title bar: the unit code, what this is, the device's status
    int x = titleBand(c, kUnitCode, 0, 0, 40);
    x += drawText(c, "// HOOK MONITOR", static_cast<float>(x), 29.0f, 17, kWhite) + 16;
    if (statusLine && statusLine[0]){
        const std::string st = fitText(upper(statusLine), 10, static_cast<float>(W - M - x));
        drawText(c, st.c_str(), static_cast<float>(W - M), 26.0f, 10, kDim, Align::Right);
    }

    const int lineH = 26;
    const int historyRows = 8;
    const int historyH = 31 + 12 + historyRows * lineH + 10;
    const int logY = H - M - historyH;
    const int lastY = 52;
    const int lastH = logY - 10 - lastY;
    const int detailBottom = lastY + lastH - 12;

    const int ct = panel(c, M, lastY, W - 2 * M, lastH, "LAST EVENT");
    const int lx = M + 16, rx = W - M - 16;
    int y = ct + 8;
    const EventEntry* last = log.last();
    if (!last){
        drawText(c, "WAITING FOR THE FIRST HOOK EVENT", static_cast<float>(lx), static_cast<float>(y + 24), 13, kWarn);
    } else {
        // The last event: its number on a chip, its name, the project, its age
        char seq[16];
        snprintf(seq, sizeof(seq), "#%lu", static_cast<unsigned long>(last->seq));
        const int cw = chip(c, seq, lx, y + 14, 10, kYellow, kPanel, Align::Left);
        char age[32];
        ageText(last->receivedMs, nowMs, age, sizeof(age));
        const int ageW = glyphs::text(c, age, rx, y + 14, Face::Mono, 16, kDim, textdatum_t::middle_right);
        const int nx = lx + cw + 12;
        const std::string name = fitText(upper(last->event.name), 16, static_cast<float>(rx - ageW - 120 - nx));
        const int nw = drawText(c, name.c_str(), static_cast<float>(nx), y + 22.0f, 16, kWhite);
        const char* project = last->event.find("project");
        if (project && project[0]){
            const std::string p = fitText(upper(project), 16, static_cast<float>(rx - ageW - 20 - (nx + nw + 14)));
            drawText(c, p.c_str(), static_cast<float>(nx + nw + 14), y + 22.0f, 16, kYellow);
        }
        y += 44;

        // Every field: the key in the stroke font, the value wrapped in mono
        const int keyW = 190;
        const int valueX = lx + keyW;
        const int valueW = rx - valueX;
        for (const HookField& f : last->event.fields){
            if (y + lineH > detailBottom){
                drawText(c, "...", static_cast<float>(lx), static_cast<float>(y + 14), 10, kDim);
                break;
            }
            const std::string key = fitText(upper(f.key), 9.5f, static_cast<float>(keyW - 12));
            drawText(c, key.c_str(), static_cast<float>(lx), static_cast<float>(y + 16), 9.5f, kDim);
            const int room = (detailBottom - y) / lineH;
            const int maxLines = room < 3 ? (room < 1 ? 1 : room) : 3;
            const int lines = glyphs::wrap(c, f.value, valueX, y + 1, valueW, Face::Mono, 20, lineH, kWhite, maxLines, kDim);
            y += lines * lineH + 2;
        }
    }

    // History, newest first
    char head[48];
    snprintf(head, sizeof(head), "%lu RECEIVED SINCE BOOT", static_cast<unsigned long>(log.count()));
    y = panel(c, M, logY, W - 2 * M, historyH, "LOG", head) + 10;
    const auto& entries = log.entries();
    int rows = 0;
    for (auto it = entries.rbegin(); it != entries.rend() && rows < historyRows; ++it, ++rows){
        char age[32];
        ageText(it->receivedMs, nowMs, age, sizeof(age));
        const char* summary = it->event.find("summary");
        const char* project = it->event.find("project");
        const char* session = it->event.find("session_id");
        const uint16_t color = rows == 0 ? kWhite : kDim;
        char seq[16];
        snprintf(seq, sizeof(seq), "%lu", static_cast<unsigned long>(it->seq));
        glyphs::text(c, seq, lx + 44, y, Face::Mono, 16, rows == 0 ? kYellow : mix(kYellow, kPanel, 0.4f), textdatum_t::top_right);
        glyphs::text(c, age, lx + 58, y, Face::Mono, 16, color);
        std::string what = summary ? summary : it->event.name;
        if (project && project[0]) what += "   [" + std::string(project) + "]";
        else if (session) what += std::string("   ") + std::string(session).substr(0, 8);
        const int wx = lx + 150;
        glyphs::text(c, glyphs::fit(c, what, Face::Mono, 16, rx - wx).c_str(), wx, y, Face::Mono, 16, color);
        y += lineH;
    }
}

}  // namespace monitor::mech
