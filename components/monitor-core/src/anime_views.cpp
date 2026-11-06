// The anime theme's views: a tile's chrome on the display, the usage rows
// (the usage tile, the screensaver's card, the usage page), the usage page,
// the screensaver's card and the hook inspector. The same data in the same
// boxes as the classic views; only the drawing differs.

#include "monitor/anime.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "monitor/usagepage.h"
#include "monitor/usageview.h"

namespace monitor::anime {

namespace {

constexpr int kMargin = 24;

// A line with ticks hanging under it, a longer one every fourth
void rule(LovyanGFX& g, int x, int y, int w, uint16_t color, uint16_t ticks, int step = 16){
    g.drawFastHLine(x, y, w, color);
    for (int k = 0; k <= w; k += step) g.drawFastVLine(x + k, y + 1, (k % (step * 4) == 0) ? 6 : 3, ticks);
}

// A section heading: the Japanese in an orange tag, the English after it
void heading(LovyanGFX& g, int x, int y, const char* jp, const char* en, int h = 30){
    const int w = tag(g, x, y, h, jp, Face::Gothic, h - 8, kBg, kOrange);
    text(g, en, x + w + 10, y + h / 2, Face::Cond, h - 6, kOrange, textdatum_t::middle_left, -1, 2);
}

// The dark shade that goes with one of the theme's colours (unlit segments, fills)
uint16_t darkOf(uint16_t color){
    if (color == kRed) return kRedDark;
    if (color == kAmber) return kAmberDark;
    if (color == kGreen) return kGreenDark;
    if (color == kCyan) return kCyanDark;
    if (color == kGrey) return kGreyDark;
    return kOrangeDark;
}

void ageText(int64_t receivedMs, int64_t nowMs, char* out, size_t size){
    long long s = (nowMs - receivedMs) / 1000;
    if (s < 0) s = 0;
    if (s < 60)        snprintf(out, size, "%llds AGO", s);
    else if (s < 3600) snprintf(out, size, "%lldm%02llds AGO", s / 60, s % 60);
    else               snprintf(out, size, "%lldh%02lldm AGO", s / 3600, (s % 3600) / 60);
}

}  // namespace

// ---------------- a tile's rows ----------------

void drawTile(LovyanGFX& g, const Tile& t){
    const int x0 = t.x + kTilePad, x1 = t.x + t.w - kTilePad;
    const int px = t.band * 60 / 100;
    const int tagH = t.band * 72 / 100;
    const int segH = t.band * 66 / 100;

    // The rows' old texts go first
    g.fillRect(t.x, t.headY - t.band / 2, t.w, t.band, kBg);
    g.fillRect(t.x, t.footY - t.band / 2, t.w, t.band, kBg);

    int nx = x0;
    if (t.index){
        nx += tag(g, x0, t.headY - tagH / 2, tagH, t.index, Face::Mono, tagH - 6, kBg, t.tagColor) + 10;
    }
    int jpW = 0;
    if (t.jp && t.jp[0]){
        jpW = text(g, t.jp, x1, t.headY, Face::Gothic, px, t.color, textdatum_t::middle_right, kBg);
    }
    const std::string name = fit(g, upper(t.name ? t.name : ""), Face::Cond, px, x1 - nx - jpW - 16);
    text(g, name.c_str(), nx, t.headY, Face::Cond, px, kText, textdatum_t::middle_left, kBg);

    int rightW = 0;
    if (t.clock){
        // Leaning digits reach a little right of where they are placed
        const int lean = segH / 10 + 1;
        rightW = sevenSeg(g, t.clock, x1 - lean, t.footY - segH / 2, segH, t.color, shade(t.color, 16), true) + lean;
    } else if (t.note){
        rightW = text(g, t.note, x1, t.footY, Face::Mono, t.band * 42 / 100, kOrangeDim, textdatum_t::middle_right, kBg);
    }
    if (t.en && t.en[0]){
        const int enPx = px * 85 / 100;
        const std::string en = fit(g, t.en, Face::Cond, enPx, x1 - x0 - rightW - 16);
        text(g, en.c_str(), x0, t.footY, Face::Cond, enPx, t.color, textdatum_t::middle_left, kBg, 2);
    }
}

// ---------------- usage rows ----------------

namespace {

// One size of row, as tall as the classic ones (the page and the card lay
// out with usageRowsHeight)
struct RowStyle {
    int labelPx, pctH, smallPx;   // smallPx 0: no reset line
    int rowH, labelY, barY, barH, resetY;
    int minWidth;
};

const RowStyle kRows[] = {
    { 26, 40, 18, 112, 4, 54, 22, 84, 400 },
    { 22, 26, 16,  72, 0, 30, 14, 50, 300 },
    { 18, 20, 14,  56, 0, 24, 12, 39,   0 },
    { 18, 18,  0,  40, 0, 22, 12,  0,   0 },
};

void splitRow(M5Canvas& c, int x, int top, int w, const RowStyle& st, const Usage& usage){
    c.fillRect(x, top + st.labelY + 4, 5, st.labelPx * 3 / 4, kOrange);
    text(c, "THIS WEEK BY APP", x + 12, top + st.labelY, Face::Cond, st.labelPx, kText);
    float shares[Usage::kMaxShares];
    uint16_t colors[Usage::kMaxShares];
    const int n = static_cast<int>(usage.split.size() < Usage::kMaxShares ? usage.split.size() : Usage::kMaxShares);
    for (int i = 0; i < n; i++){ shares[i] = usage.split[i].percent; colors[i] = splitColor(i); }
    splitBlocks(c, x, top + st.barY, w, st.barH, shares, colors, n, kOrangeDark);
    if (!st.smallPx) return;
    int lx = x;
    for (int i = 0; i < n; i++){
        char item[48];
        snprintf(item, sizeof(item), "%s%s %d%%", i ? "  " : "", upper(usage.split[i].label).c_str(),
                 static_cast<int>(lroundf(usage.split[i].percent)));
        const int iw = textWidth(c, item, Face::Mono, st.smallPx);
        if (lx + iw > x + w) break;
        text(c, item, lx, top + st.resetY, Face::Mono, st.smallPx, splitColor(i));
        lx += iw;
    }
}

}  // namespace

void usageRows(M5Canvas& c, int x, int y, int w, int h, const Usage& usage,
               int32_t elapsedSec, uint16_t bg, bool detailed){
    (void)bg;
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

        // The percentage in seven segments, the % sign after it
        char num[8];
        snprintf(num, sizeof(num), "%d", static_cast<int>(lroundf(l.percent)));
        const int signW = text(c, "%", x + w, top + st->pctH, Face::Cond, st->pctH * 60 / 100, level,
                               textdatum_t::baseline_right);
        // Unlit segments only on the big digits: on the small ones an unlit
        // 8 behind a 1 reads as 18 or 88
        const uint16_t unlit = st->pctH >= 30 ? shade(level, 16) : kBg;
        const int digitsW = sevenSeg(c, num, x + w - signW - 4, top, st->pctH, level, unlit, true);
        const int pctW = signW + 4 + digitsW;

        c.fillRect(x, top + st->labelY + 4, 5, st->labelPx * 3 / 4, level);
        const std::string label = fit(c, upper(l.label), Face::Cond, st->labelPx, w - pctW - 28);
        text(c, label.c_str(), x + 12, top + st->labelY, Face::Cond, st->labelPx, kText);

        const int by = top + st->barY;
        float f = l.percent / 100.0f;
        if (f > 1.0f) f = 1.0f;
        blocks(c, x, by, w, st->barH, f, level, kOrangeDark);
        if (detailed){
            // Where an even pace would have the bar by now
            const UsagePace pace = usagePace(l, elapsedSec);
            if (pace.known){
                const int tx = x + static_cast<int>((w - 2) * pace.elapsed);
                c.fillRect(tx, by - 4, 2, st->barH + 8, kWhite);
                c.fillTriangle(tx - 5, by - 10, tx + 6, by - 10, tx + 1, by - 4, kWhite);
            }
        }

        if (st->smallPx && detailed){
            UsageDetailText t;
            usageDetailText(l, usage, elapsedSec, t);
            const std::string pace = upper(t.pace);
            const int paceW = pace.empty() ? 0 : textWidth(c, pace.c_str(), Face::Mono, st->smallPx) + 24;
            std::string left = upper(t.full);
            if (!t.brief[0] || textWidth(c, left.c_str(), Face::Mono, st->smallPx) > w - paceW){
                if (t.brief[0]) left = upper(t.brief);
            }
            // The line in the bar's colour, but a warning in its own
            text(c, left.c_str(), x, top + st->resetY, Face::Mono, st->smallPx, level);
            if (!pace.empty()){
                const uint16_t pc = t.tone == 2 ? kRed : (t.tone == 1 ? kAmber : level);
                text(c, pace.c_str(), x + w, top + st->resetY, Face::Mono, st->smallPx, pc, textdatum_t::top_right);
            }
        } else if (st->smallPx){
            // In the bar's colour: dim orange was too dark to read
            char reset[48];
            usageResetText(l, elapsedSec, reset, sizeof(reset));
            if (reset[0]) text(c, upper(reset).c_str(), x, top + st->resetY, Face::Mono, st->smallPx, level);
        }
        top += st->rowH;
    }
}

// ---------------- the usage page ----------------

namespace {

void chartBox(M5Canvas& c, const Usage& usage, int32_t elapsedSec, int bx, int by, int bw, int bh){
    UsageChart ch;
    if (!usageChart(usage, elapsedSec, ch)){
        text(c, "NO WEEKLY DATA", bx, by + 8, Face::Mono, 20, kOrangeDim);
        return;
    }
    frame(c, bx, by, bw, bh, 14, 1, kOrangeDim);
    const int px = bx + 58, py = by + 16;
    const int pw = bw - 58 - 14, ph = bh - 16 - 38;
    auto X = [&](int64_t t) { return px + static_cast<int>(static_cast<double>(pw) * (t - ch.start) / (ch.end - ch.start)); };
    auto Y = [&](float v) { return py + ph - static_cast<int>(ph * v / ch.yMax); };

    for (int v = 25; v <= static_cast<int>(ch.yMax); v += 25){
        const int y = Y(static_cast<float>(v));
        c.drawFastHLine(px, y, pw, v == 100 ? kOrangeDim : kOrangeDark);
        char t[16];
        snprintf(t, sizeof(t), "%d%%", v);
        text(c, t, px - 8, y, Face::Mono, 16, kOrangeDim, textdatum_t::middle_right);
    }
    c.drawFastHLine(px, py + ph, pw, kOrangeDim);

    // Local midnights, the weekday under each day (today in white)
    const int32_t off = usage.utcOffset;
    const int todayWd = usage.nowUnix ? usageWeekday(ch.now, off) : -1;
    int64_t dayStart = ch.start;
    int64_t midnight = ch.start + (86400 - usageSecondsOfDay(ch.start, off));
    while (dayStart < ch.end){
        const int64_t dayEnd = midnight < ch.end ? midnight : ch.end;
        if (midnight < ch.end) c.drawFastVLine(X(midnight), py, ph, kOrangeDark);
        const int x0 = X(dayStart), x1 = X(dayEnd);
        if (x1 - x0 >= 40){
            const int wd = usageWeekday(dayStart, off);
            const bool today = wd == todayWd && ch.now >= dayStart && ch.now < dayEnd;
            text(c, upper(usageWeekdayName(wd)).c_str(), (x0 + x1) / 2, py + ph + 8, Face::Cond, 20,
                 today ? kText : kOrangeDim, textdatum_t::top_center, -1, 1);
        }
        dayStart = dayEnd;
        midnight += 86400;
    }

    // The even pace, then the usage so far: scan lines underneath, the line on top
    dashed(c, X(ch.start), Y(0.0f), X(ch.end), Y(100.0f), kGrey);
    const uint16_t curve = ch.week ? levelColor(ch.week->percent, ch.week->severity) : kOrange;
    const uint16_t fill = darkOf(curve);
    const int xNow = X(ch.points.back().t);
    int prev = -1;
    for (int x = px; x <= xNow; x++){
        const int64_t t = ch.start + static_cast<int64_t>(static_cast<double>(x - px) * (ch.end - ch.start) / pw);
        const int y = Y(usageChartValue(ch, t));
        for (int yy = py + ph - 2; yy > y; yy -= 3) c.drawPixel(x, yy, fill);
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
        const uint16_t pc = ch.pace.projected > 100.0f ? kRed : kAmber;
        dashed(c, xNow, yNow, x1, Y(v1), pc);
        char t[16];
        snprintf(t, sizeof(t), "~%d%%", static_cast<int>(lroundf(ch.pace.projected)));
        if (Y(v1) - 30 < py) text(c, t, x1 - 6, Y(v1) + 6, Face::Cond, 22, pc, textdatum_t::top_right);
        else                 text(c, t, x1 - 2, Y(v1) - 6, Face::Cond, 22, pc, textdatum_t::bottom_right);
    }
    if (ch.week){
        const int yNow = Y(ch.week->percent);
        hexagon(c, static_cast<float>(xNow), static_cast<float>(yNow), 10.0f, kBg);
        hexagon(c, static_cast<float>(xNow), static_cast<float>(yNow), 7.0f, curve);
    }
    if (ch.noHistory){
        text(c, "NO READINGS YET: THE LINE IS THE AVERAGE SO FAR", px + 8, py + 6, Face::Mono, 16, kOrangeDim);
    }
}

void projectsBox(M5Canvas& c, const Usage& usage, int bx, int by, int bw, int bh){
    if (usage.projects.empty()){
        text(c, "NO CLAUDE CODE TRANSCRIPTS READ THIS WEEK", bx, by + 8, Face::Mono, 18, kOrangeDim);
        return;
    }
    const int todayR = bx + bw;
    const int weekR  = todayR - 96;
    const int nameW  = bw * 30 / 100;
    const int barX   = bx + nameW + 12;
    const int barW   = weekR - 84 - barX;
    text(c, "WEEK", weekR, by, Face::Mono, 16, kOrangeDim, textdatum_t::top_right);
    text(c, "TODAY", todayR, by, Face::Mono, 16, kOrangeDim, textdatum_t::top_right);

    float maxWeek = 0.0f;
    for (const UsageProject& p : usage.projects) if (p.week > maxWeek) maxWeek = p.week;
    const int n = static_cast<int>(usage.projects.size());
    int rowH = (bh - 24) / (n > 0 ? n : 1);
    if (rowH > 42) rowH = 42;
    int y = by + 24;
    for (const UsageProject& p : usage.projects){
        const bool other = p.name == "other";
        const int cy = y + rowH / 2;
        const std::string name = fit(c, other ? "OTHER PROJECTS" : upper(p.name), Face::Cond, 22, nameW);
        text(c, name.c_str(), bx, cy, Face::Cond, 22, other ? kOrangeDim : kText, textdatum_t::middle_left);
        blocks(c, barX, cy - 7, barW, 14, maxWeek > 0.0f ? p.week / maxWeek : 0.0f,
               other ? kOrangeDim : kOrange, kOrangeDark);
        char t[16];
        if (p.week > 0.0f && p.week < 1.0f) snprintf(t, sizeof(t), "<1%%");
        else snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(p.week)));
        text(c, t, weekR, cy, Face::Cond, 28, kText, textdatum_t::middle_right);
        if (p.today <= 0.0f) snprintf(t, sizeof(t), "-");
        else if (p.today < 1.0f) snprintf(t, sizeof(t), "<1%%");
        else snprintf(t, sizeof(t), "%d%%", static_cast<int>(lroundf(p.today)));
        text(c, t, todayR, cy, Face::Mono, 18, kOrangeDim, textdatum_t::middle_right);
        y += rowH;
    }
}

void planBox(M5Canvas& c, const Usage& usage, int bx, int by, int bw){
    std::string plan, extra;
    int planTone = 0, extraTone = 0;
    const bool havePlan = usagePlanText(usage, plan, planTone);
    const bool haveExtra = usageExtraText(usage, extra, extraTone);
    if (!havePlan && !haveExtra) return;
    heading(c, bx, by, "契約", "PLAN", 26);
    const int labelW = 170;
    int y = by + 52;
    auto row = [&](const char* label, const std::string& value, int tone) {
        text(c, label, bx, y, Face::Mono, 16, kOrangeDim, textdatum_t::middle_left);
        const uint16_t color = tone < 0 ? kOrangeDim : (tone == 2 ? kRed : (tone == 1 ? kAmber : kText));
        const std::string v = fit(c, upper(value), Face::Cond, 26, bw - labelW);
        text(c, v.c_str(), bx + labelW, y, Face::Cond, 26, color, textdatum_t::middle_left);
        y += 36;
    };
    if (havePlan) row("PLAN", plan, planTone);
    if (haveExtra) row("EXTRA USAGE", extra, extraTone);
}

}  // namespace

void usagePage(M5Canvas& c, const Usage& usage, int32_t elapsedSec){
    const int W = c.width(), H = c.height();
    c.fillScreen(kBg);

    // Header: the title in mincho with its English, the age on the right,
    // a ruled line under it
    const int w1 = text(c, "使用状況", kMargin, 42, Face::Mincho, 50, kText, textdatum_t::middle_left);
    text(c, "CLAUDE USAGE MONITOR", kMargin + w1 + 18, 48, Face::Cond, 26, kOrange, textdatum_t::middle_left, -1, 3);
    const UsageSwitch sw = usageThemeSwitch(W);
    char age[40];
    usageAgeText(usage.ageSec + elapsedSec, age, sizeof(age));
    text(c, upper(age).c_str(), sw.x - 24, 48, Face::Mono, 20, kOrangeDim, textdatum_t::middle_right);
    c.fillRect(kMargin, 76, 220, 4, kOrange);
    rule(c, kMargin, 79, W - 2 * kMargin, kOrangeDim, kOrangeDark);
    drawThemeSwitch(c, sw);

    const int top = 100;
    const int rows = usageRowCount(usage);
    const int planH = 104;
    if (W > H){
        const int colW = (W - 3 * kMargin) / 2;
        const int rx = 2 * kMargin + colW;
        heading(c, kMargin, top, "制限", "LIMITS");
        int rowsH = usageRowsHeight(rows);
        if (rowsH > H - (top + 40) - planH - 16) rowsH = H - (top + 40) - planH - 16;
        if (rows > 0) usageRows(c, kMargin, top + 40, colW, rowsH, usage, elapsedSec, kBg, true);
        planBox(c, usage, kMargin, H - planH, colW);

        const int chartH = 290;
        heading(c, rx, top, "週間推移", "THIS WEEK");
        chartBox(c, usage, elapsedSec, rx, top + 40, colW, chartH);
        const int py = top + 40 + chartH + 18;
        heading(c, rx, py, "プロジェクト別", "BY PROJECT");
        text(c, "EST. SHARE OF THE WEEKLY LIMIT", rx + colW, py + 15, Face::Mono, 16, kOrangeDim, textdatum_t::middle_right);
        projectsBox(c, usage, rx, py + 40, colW, H - (py + 40) - 8);
    } else {
        const int w = W - 2 * kMargin;
        heading(c, kMargin, top, "制限", "LIMITS");
        int rowsH = usageRowsHeight(rows);
        if (rowsH > 460) rowsH = 460;
        if (rows > 0) usageRows(c, kMargin, top + 40, w, rowsH, usage, elapsedSec, kBg, true);
        int y = top + 40 + rowsH + 12;
        planBox(c, usage, kMargin, y, w);
        y += planH + 8;
        const int chartH = 300;
        heading(c, kMargin, y, "週間推移", "THIS WEEK");
        chartBox(c, usage, elapsedSec, kMargin, y + 40, w, chartH);
        y += 40 + chartH + 18;
        heading(c, kMargin, y, "プロジェクト別", "BY PROJECT");
        projectsBox(c, usage, kMargin, y + 40, w, H - (y + 40) - 8);
    }
}

// ---------------- the screensaver's card ----------------

void saverCard(M5Canvas& c, int x, int y, int w, int h, int pad, int header,
               const Usage& usage, int32_t elapsedSec, bool data){
    c.fillScreen(kBg);
    frame(c, x, y, w, h, 24, 2, kOrangeDim);
    c.fillRect(x + 24, y, 120, 4, kOrange);
    const int hy = y + 20;
    heading(c, x + pad, hy, "使用状況", "CLAUDE USAGE", 30);
    if (!data){
        text(c, "NO USAGE DATA YET", x + pad, y + header + 16, Face::Cond, 28, kText);
        text(c, "WAITING FOR THE HOST TO SEND IT", x + pad, y + header + 56, Face::Mono, 18, kOrangeDim);
        return;
    }
    char age[40];
    usageAgeText(usage.ageSec + elapsedSec, age, sizeof(age));
    text(c, upper(age).c_str(), x + w - pad, hy + 15, Face::Mono, 16, kOrangeDim, textdatum_t::middle_right);
    usageRows(c, x + pad, y + header, w - 2 * pad, usageRowsHeight(usageRowCount(usage)), usage, elapsedSec, kBg, false);
}

// ---------------- the hook inspector ----------------

void inspector(M5Canvas& c, const EventLog& log, const char* statusLine, int64_t nowMs){
    const int W = c.width(), H = c.height();
    const int m = 16;
    c.fillScreen(kBg);

    // Header bar: what this is, in black on orange, and the status
    fillFrame(c, m, 12, W - 2 * m, 36, 10, kOrange);
    const int jw = text(c, "フック監視", m + 14, 30, Face::Gothic, 22, kBg, textdatum_t::middle_left, kOrange);
    text(c, "HOOK MONITOR", m + 14 + jw + 10, 30, Face::Cond, 20, kBg, textdatum_t::middle_left, kOrange, 2);
    if (statusLine && statusLine[0]){
        text(c, upper(statusLine).c_str(), W - m - 12, 30, Face::Mono, 16, kBg, textdatum_t::middle_right, kOrange);
    }

    const int lineH = 25;
    const int historyRows = 8;
    const int historyH = (historyRows + 1) * lineH + 24;
    const int detailBottom = H - historyH;
    int y = 64;

    const EventEntry* last = log.last();
    if (!last){
        text(c, "WAITING FOR THE FIRST HOOK EVENT...", m, y, Face::Mono, 20, kOrangeDim);
    } else {
        // The last event: its number, name and project; its age
        char seq[16];
        snprintf(seq, sizeof(seq), "#%lu", static_cast<unsigned long>(last->seq));
        const int tw = tag(c, m, y, 34, seq, Face::Mono, 22, kBg, kAmber);
        char age[32];
        ageText(last->receivedMs, nowMs, age, sizeof(age));
        const int ageW = text(c, age, W - m, y + 17, Face::Mono, 16, kOrangeDim, textdatum_t::middle_right);
        const int nx = m + tw + 10;
        const int nw = text(c, fit(c, last->event.name, Face::Cond, 30, W - nx - ageW - 120).c_str(),
                            nx, y + 17, Face::Cond, 30, kText, textdatum_t::middle_left);
        const char* project = last->event.find("project");
        if (project && project[0]){
            const std::string p = "[" + upper(project) + "]";
            text(c, fit(c, p, Face::Cond, 30, W - nx - nw - ageW - 30).c_str(), nx + nw + 12, y + 17,
                 Face::Cond, 30, kOrange, textdatum_t::middle_left);
        }
        y += 50;

        // Every field: the key in dim orange capitals, the value wrapped
        const int keyW = 230;
        const int valueX = m + keyW;
        const int valueW = W - valueX - m;
        for (const HookField& f : last->event.fields){
            if (y + lineH > detailBottom){
                text(c, "...", m, y, Face::Mono, 20, kOrangeDim);
                break;
            }
            text(c, fit(c, upper(f.key), Face::Mono, 16, keyW - 10).c_str(), m, y + 2, Face::Mono, 16, kOrangeDim);
            int room = (detailBottom - y) / lineH;
            const int maxLines = room < 3 ? (room < 1 ? 1 : room) : 3;
            const int lines = glyphs::wrap(c, f.value, valueX, y, valueW, Face::Mono, 20, lineH, kText, maxLines, kOrangeDark);
            y += lines * lineH + 2;
        }
    }

    // History, newest first
    y = detailBottom;
    rule(c, m, y, W - 2 * m, kOrangeDim, kOrangeDark);
    y += 12;
    const int hw = text(c, "受信記録", m, y, Face::Gothic, 22, kOrange);
    char head[64];
    snprintf(head, sizeof(head), "RECENT  (%lu RECEIVED SINCE BOOT)", static_cast<unsigned long>(log.count()));
    text(c, head, m + hw + 12, y + 4, Face::Mono, 16, kOrangeDim);
    y += lineH + 4;
    const auto& entries = log.entries();
    int rows = 0;
    for (auto it = entries.rbegin(); it != entries.rend() && rows < historyRows; ++it, ++rows){
        char age[32];
        ageText(it->receivedMs, nowMs, age, sizeof(age));
        const char* summary = it->event.find("summary");
        const char* project = it->event.find("project");
        const char* session = it->event.find("session_id");
        char line[200];
        if (project && project[0]){
            snprintf(line, sizeof(line), "#%-4lu %-10s %s  [%s]", static_cast<unsigned long>(it->seq), age,
                     summary ? summary : it->event.name.c_str(), upper(project).c_str());
        } else {
            snprintf(line, sizeof(line), "#%-4lu %-10s %s  %.8s", static_cast<unsigned long>(it->seq), age,
                     summary ? summary : it->event.name.c_str(), session ? session : "");
        }
        text(c, fit(c, line, Face::Mono, 20, W - 2 * m).c_str(), m, y, Face::Mono, 20,
             rows == 0 ? kText : kOrangeDim);
        y += lineH;
    }
}

}  // namespace monitor::anime
