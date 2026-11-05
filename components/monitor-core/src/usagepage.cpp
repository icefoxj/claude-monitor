#include "monitor/usagepage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "monitor/anime.h"
#include "monitor/theme.h"
#include "monitor/usageview.h"

namespace monitor {

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kBg     = rgb565(0, 0, 0);
constexpr uint16_t kText   = rgb565(235, 235, 235);
constexpr uint16_t kDim    = rgb565(140, 140, 140);
constexpr uint16_t kFaint  = rgb565(96, 96, 104);
constexpr uint16_t kRule   = rgb565(48, 48, 56);
constexpr uint16_t kAmber  = rgb565(240, 160, 32);
constexpr uint16_t kRed    = rgb565(224, 48, 48);
constexpr uint16_t kCode   = rgb565(255, 200, 0);    // Claude Code's colour in the week by app

constexpr int kMargin   = 24;
constexpr int kHeaderH  = 64;
constexpr int kPlanH    = 92;
constexpr int kChartH   = 330;
constexpr int kWeekSec  = 7 * 24 * 3600;
// The page goes to the display in bands of this many rows. The Tab5 is
// turned, so a push reads the canvas a column at a time; 720 rows 2560
// bytes apart do not stay in the cache together, and the whole page in
// one push took 755 ms (the loop missed taps), in 64-row bands 93 ms
constexpr int kPushBand = 64;

// The fill under the chart's curve: the curve's colour, darker
uint16_t darker(uint16_t c){
    const int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return static_cast<uint16_t>(((r * 3 / 8) << 11) | ((g * 3 / 8) << 5) | (b * 3 / 8));
}

// "$12.40", "EUR 12.40": the panel's fonts are ASCII only
std::string money(const std::string& amount, const std::string& currency){
    if (currency.empty() || currency == "USD") return "$" + amount;
    if (currency == "BRL") return "R$" + amount;
    return currency + " " + amount;
}

// Cuts `s` with "..." to fit `room` px in the current font
std::string fit(M5Canvas& c, const std::string& s, int room){
    if (c.textWidth(s.c_str()) <= room) return s;
    std::string t = s;
    while (!t.empty() && c.textWidth((t + "...").c_str()) > room) t.pop_back();
    return t + "...";
}

void percentText(float p, char* out, size_t size){
    if (p > 0.0f && p < 1.0f) snprintf(out, size, "<1%%");
    else snprintf(out, size, "%d%%", static_cast<int>(lroundf(p)));
}

}  // namespace

void UsagePage::draw(const Usage& usage, int32_t elapsedSec){
    const int W = c_.width(), H = c_.height();
    if (W <= 0 || H <= 0) return;
    if (animeTheme()){
        anime::usagePage(c_, usage, elapsedSec);
        push();
        return;
    }
    c_.fillScreen(kBg);

    const int rows = usageRowCount(usage);
    const Box header{ kMargin, 8, W - 2 * kMargin, kHeaderH };
    const int top = header.y + kHeaderH + 16;
    Box limits, plan, chart, projects;
    if (W > H){
        // The limits and the plan on the left, the week and the projects on the right
        const int colW = (W - 3 * kMargin) / 2;
        const int rx = 2 * kMargin + colW;
        int rowsH = usageRowsHeight(rows);
        if (rowsH > H - top - kPlanH - 24) rowsH = H - top - kPlanH - 24;
        limits   = { kMargin, top, colW, rowsH };
        plan     = { kMargin, H - kPlanH - 12, colW, kPlanH };
        chart    = { rx, top, colW, kChartH };
        projects = { rx, top + kChartH + 24, colW, H - (top + kChartH + 24) - 12 };
    } else {
        const int w = W - 2 * kMargin;
        int rowsH = usageRowsHeight(rows);
        if (rowsH > 460) rowsH = 460;
        limits   = { kMargin, top, w, rowsH };
        plan     = { kMargin, limits.y + rowsH + 16, w, kPlanH };
        chart    = { kMargin, plan.y + kPlanH + 24, w, kChartH };
        projects = { kMargin, chart.y + kChartH + 24, w, H - (chart.y + kChartH + 24) - 12 };
    }

    drawHeader(usage, elapsedSec, header);
    if (rows > 0) drawUsageRows(c_, limits.x, limits.y, limits.w, limits.h, usage, elapsedSec, kBg, true);
    drawPlan(usage, plan);
    drawChart(usage, elapsedSec, chart);
    drawProjects(usage, projects);
    push();
}

void UsagePage::push(){
    const int W = c_.width(), H = c_.height();
    LovyanGFX* dst = c_.getParent();
    for (int y = 0; y < H; y += kPushBand){
        dst->setClipRect(0, y, W, kPushBand < H - y ? kPushBand : H - y);
        c_.pushSprite(0, 0);
    }
    dst->clearClipRect();
}

void UsagePage::drawHeader(const Usage& usage, int32_t elapsedSec, const Box& b){
    const int cy = b.y + b.h / 2;
    c_.setTextDatum(textdatum_t::middle_left);
    c_.setFont(&fonts::DejaVu40);
    c_.setTextColor(kText, kBg);
    c_.drawString("Claude usage", b.x, cy);

    // The age left of the theme switch, which takes the corner
    const UsageSwitch sw = usageThemeSwitch(c_.width());
    const bool withSwitch = themeAvailable(Theme::Anime);
    char age[40];
    usageAgeText(usage.ageSec + elapsedSec, age, sizeof(age));
    c_.setTextDatum(textdatum_t::middle_right);
    c_.setFont(&fonts::DejaVu18);
    c_.setTextColor(kDim, kBg);
    c_.drawString(age, withSwitch ? sw.x - 24 : b.x + b.w, cy);
    c_.drawFastHLine(b.x, b.y + b.h + 4, b.w, kRule);
    drawThemeSwitch(c_, sw);
}

bool usagePlanText(const Usage& usage, std::string& out, int& tone){
    if (usage.plan.name.empty()) return false;
    out = usage.plan.name;
    std::string status = usage.plan.status;
    for (char& ch : status) if (ch == '_') ch = ' ';
    char since[32];
    usageDateText(usage, usage.plan.since, since, sizeof(since));
    if (!status.empty() && since[0]) out += ", " + status + " since " + since;
    else if (!status.empty())        out += ", " + status;
    tone = status.empty() || status == "active" ? 0 : 1;
    return true;
}

bool usageExtraText(const Usage& usage, std::string& out, int& tone){
    const UsageExtra& e = usage.extra;
    if (e.state.empty()) return false;
    tone = 0;
    if (e.state == "off"){
        out = "off";
        tone = -1;
    } else if (!e.limit.empty()){
        out = money(e.used, e.currency) + " of " + money(e.limit, e.currency) + " this month";
        if (e.state == "full"){ out += ", limit reached"; tone = 2; }
    } else {
        out = money(e.used.empty() ? "0" : e.used, e.currency) + " this month, no limit";
    }
    return true;
}

void UsagePage::drawPlan(const Usage& usage, const Box& b){
    std::string plan, extra;
    int planTone = 0, extraTone = 0;
    const bool havePlan = usagePlanText(usage, plan, planTone);
    const bool haveExtra = usageExtraText(usage, extra, extraTone);
    if (!havePlan && !haveExtra) return;
    c_.drawFastHLine(b.x, b.y, b.w, kRule);

    const int labelW = 150;
    int y = b.y + 14;
    auto row = [&](const char* label, const std::string& value, uint16_t color) {
        c_.setTextDatum(textdatum_t::middle_left);
        c_.setFont(&fonts::DejaVu18);
        c_.setTextColor(kDim, kBg);
        c_.drawString(label, b.x, y + 14);
        c_.setFont(&fonts::DejaVu24);   // a size smaller when it does not fit
        if (c_.textWidth(value.c_str()) > b.w - labelW) c_.setFont(&fonts::DejaVu18);
        c_.setTextColor(color, kBg);
        c_.drawString(fit(c_, value, b.w - labelW).c_str(), b.x + labelW, y + 14);
        y += 38;
    };
    auto toneColor = [](int tone) { return tone < 0 ? kDim : (tone == 2 ? kRed : (tone == 1 ? kAmber : kText)); };
    if (havePlan) row("Plan", plan, toneColor(planTone));
    if (haveExtra) row("Extra usage", extra, toneColor(extraTone));
}

void UsagePage::dashedLine(int x0, int y0, int x1, int y1, uint16_t color){
    const float dx = static_cast<float>(x1 - x0), dy = static_cast<float>(y1 - y0);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    for (float s = 0.0f; s < len; s += 16.0f){
        const float e = s + 9.0f < len ? s + 9.0f : len;
        for (int k = 0; k < 2; k++){   // two pixels thick
            c_.drawLine(x0 + static_cast<int>(dx * s / len), y0 + static_cast<int>(dy * s / len) + k,
                        x0 + static_cast<int>(dx * e / len), y0 + static_cast<int>(dy * e / len) + k, color);
        }
    }
}

UsageSwitch usageThemeSwitch(int pageWidth){
    UsageSwitch s;
    s.size = 56;
    s.x = pageWidth - kMargin - s.size;
    s.y = 12;
    s.hitW = 160;
    s.hitH = 110;
    s.hitX = pageWidth - s.hitW;
    s.hitY = 0;
    return s;
}

void drawThemeSwitch(LovyanGFX& g, const UsageSwitch& s){
    if (!themeAvailable(Theme::Anime)) return;
    const int x = s.x, y = s.y, n = s.size;
    // The two looks, one each side of the diagonal
    g.fillTriangle(x, y, x + n - 1, y, x, y + n - 1, rgb565(52, 52, 60));
    g.fillTriangle(x + n - 1, y, x + n - 1, y + n - 1, x, y + n - 1, anime::kOrange);
    g.fillCircle(x + n * 3 / 10, y + n * 3 / 10, n / 7, rgb565(56, 176, 72));
    anime::hexagonOutline(g, x + n * 0.70f, y + n * 0.70f, n * 0.17f, n / 18.0f, anime::kBg);
    g.drawLine(x + n - 1, y, x, y + n - 1, kText);
    g.drawLine(x + n - 1, y + 1, x + 1, y + n - 1, kText);
    // Its outline in the current look
    if (animeTheme()){
        anime::frame(g, x - 5, y - 5, n + 10, n + 10, 12, 2, anime::kOrange);
    } else {
        g.drawRoundRect(x - 5, y - 5, n + 10, n + 10, 12, kDim);
        g.drawRoundRect(x - 4, y - 4, n + 8, n + 8, 11, kDim);
    }
}

bool usageChart(const Usage& usage, int32_t elapsedSec, UsageChart& out){
    out = UsageChart{};
    // The weekly limit the history belongs to: the first 7-day window that
    // starts where the history does (else the first 7-day one; the per-model
    // weeks share the window and come after it)
    const UsageLimit* week = nullptr;
    const UsageLimit* firstWeek = nullptr;
    int64_t start = usage.history.start, end = 0;
    const int64_t sent = usage.nowUnix;   // the host's clock at the line, 0 = none
    for (const UsageLimit& l : usage.limits){
        if (l.windowSec != kWeekSec || l.resetsIn < 0 || sent == 0) continue;
        if (!firstWeek) firstWeek = &l;
        const int64_t s = sent + l.resetsIn - l.windowSec;
        if (!week && start != 0 && std::llabs(s - start) < 3 * 3600) week = &l;
    }
    if (!week) week = firstWeek;
    if (week){
        end = sent + week->resetsIn;
        if (start == 0) start = end - kWeekSec;
    } else if (start != 0){
        end = start + kWeekSec;
    } else {
        return false;
    }
    const int64_t now = sent ? sent + elapsedSec : start + static_cast<int64_t>(usage.history.values.size() > 0
                                                       ? (usage.history.values.size() - 1) * usage.history.step : 0);

    std::vector<UsageChart::Point>& pts = out.points;
    pts.push_back({ start, 0.0f });
    for (size_t i = 0; i < usage.history.values.size(); i++){
        const int16_t v = usage.history.values[i];
        const int64_t t = usage.history.start + static_cast<int64_t>(i) * usage.history.step;
        if (v < 0 || t <= pts.back().t || t > now) continue;
        pts.push_back({ t, static_cast<float>(v) });
    }
    if (week && now > pts.back().t) pts.push_back({ now, week->percent });

    float maxV = 100.0f;
    for (const UsageChart::Point& p : pts) if (p.v > maxV) maxV = p.v;
    out.week = week;
    out.start = start;
    out.end = end;
    out.now = now;
    if (week) out.pace = usagePace(*week, elapsedSec);
    out.yMax = ceilf(maxV / 25.0f) * 25.0f;
    out.noHistory = usage.history.values.size() < 2;
    return true;
}

float usageChartValue(const UsageChart& chart, int64_t t){
    const std::vector<UsageChart::Point>& pts = chart.points;
    if (pts.empty()) return 0.0f;
    if (t <= pts.front().t) return pts.front().v;
    for (size_t i = 0; i + 1 < pts.size(); i++){
        const UsageChart::Point& a = pts[i];
        const UsageChart::Point& b = pts[i + 1];
        if (t <= b.t){
            const float f = b.t > a.t ? static_cast<float>(t - a.t) / static_cast<float>(b.t - a.t) : 1.0f;
            return a.v + (b.v - a.v) * f;
        }
    }
    return pts.back().v;
}

void UsagePage::drawChart(const Usage& usage, int32_t elapsedSec, const Box& b){
    UsageChart chart;
    if (!usageChart(usage, elapsedSec, chart)) return;   // nothing to draw a week from
    const UsageLimit* week = chart.week;
    const int64_t start = chart.start, end = chart.end, now = chart.now;
    const std::vector<UsageChart::Point>& pts = chart.points;
    const UsagePace& pace = chart.pace;
    const float yMax = chart.yMax;
    const int64_t sent = usage.nowUnix;

    // Title and legend
    c_.setTextDatum(textdatum_t::top_left);
    c_.setFont(&fonts::DejaVu24);
    c_.setTextColor(kText, kBg);
    c_.drawString("This week", b.x, b.y);
    const uint16_t curveColor = week ? usageLevelColor(week->percent, week->severity) : kCode;
    c_.setFont(&fonts::DejaVu18);
    c_.setTextDatum(textdatum_t::top_right);
    c_.setTextColor(kDim, kBg);
    int lx = b.x + b.w;
    c_.drawString("even pace", lx, b.y + 4);
    lx -= c_.textWidth("even pace") + 8;
    dashedLine(lx - 30, b.y + 14, lx, b.y + 14, kFaint);
    lx -= 30 + 20;
    c_.drawString("used", lx, b.y + 4);
    lx -= c_.textWidth("used") + 8;
    c_.fillRect(lx - 30, b.y + 13, 30, 3, curveColor);

    // The plot: percentages up, the week's days across
    const int px = b.x + 52, py = b.y + 44;
    const int pw = b.w - 52 - 6, ph = b.h - 44 - 34;
    auto X = [&](int64_t t) { return px + static_cast<int>(static_cast<double>(pw) * (t - start) / (end - start)); };
    auto Y = [&](float v) { return py + ph - static_cast<int>(ph * v / yMax); };

    c_.setFont(&fonts::DejaVu12);
    c_.setTextDatum(textdatum_t::middle_right);
    for (int v = 25; v <= static_cast<int>(yMax); v += 25){
        const int y = Y(static_cast<float>(v));
        c_.drawFastHLine(px, y, pw, v == 100 ? kFaint : kRule);
        char t[16];
        snprintf(t, sizeof(t), "%d%%", v);
        c_.setTextColor(kDim, kBg);
        c_.drawString(t, px - 8, y);
    }
    c_.drawFastHLine(px, py + ph, pw, kFaint);

    // Local midnights, and the weekday under each day (today in white)
    const int32_t off = usage.utcOffset;
    const int todayWd = sent ? usageWeekday(now, off) : -1;
    int64_t dayStart = start;
    int64_t midnight = start + (86400 - usageSecondsOfDay(start, off));
    c_.setFont(&fonts::DejaVu18);
    c_.setTextDatum(textdatum_t::top_center);
    while (dayStart < end){
        const int64_t dayEnd = midnight < end ? midnight : end;
        if (midnight < end) c_.drawFastVLine(X(midnight), py, ph, kRule);
        const int x0 = X(dayStart), x1 = X(dayEnd);
        if (x1 - x0 >= 40){
            const int wd = usageWeekday(dayStart, off);
            c_.setTextColor(wd == todayWd && now >= dayStart && now < dayEnd ? kText : kDim, kBg);
            c_.drawString(usageWeekdayName(wd), (x0 + x1) / 2, py + ph + 8);
        }
        dayStart = dayEnd;
        midnight += 86400;
    }

    // The even pace: 0 at the start, 100 % at the reset
    dashedLine(X(start), Y(0.0f), X(end), Y(100.0f), kFaint);

    // The usage so far: filled underneath, then the line on top
    const int xNow = X(pts.back().t);
    std::vector<int> ys;
    for (int x = px; x <= xNow; x++){
        const int64_t t = start + static_cast<int64_t>(static_cast<double>(x - px) * (end - start) / pw);
        ys.push_back(Y(usageChartValue(chart, t)));
    }
    const uint16_t fill = darker(curveColor);
    for (size_t i = 0; i < ys.size(); i++){
        if (py + ph - ys[i] > 0) c_.drawFastVLine(px + static_cast<int>(i), ys[i], py + ph - ys[i], fill);
    }
    for (size_t i = 0; i < ys.size(); i++){
        const int prev = i ? ys[i - 1] : ys[i];
        const int lo = prev < ys[i] ? prev : ys[i], hi = prev < ys[i] ? ys[i] : prev;
        c_.fillRect(px + static_cast<int>(i), lo - 1, 1, hi - lo + 3, curveColor);
    }

    // Where it ends up at the reset at this pace, and now
    if (week && pace.valid && pace.projected > week->percent){
        const int yNow = Y(week->percent);
        int x1 = X(end);
        float v1 = pace.projected;
        if (v1 > yMax){   // off the top: stop at the top edge
            v1 = yMax;
            const float f = (yMax - week->percent) / (pace.projected - week->percent);
            x1 = xNow + static_cast<int>((X(end) - xNow) * f);
        }
        const uint16_t pc = pace.projected > 100.0f ? kAmber : kDim;
        dashedLine(xNow, yNow, x1, Y(v1), pc);
        char t[16];
        snprintf(t, sizeof(t), "~%d%%", static_cast<int>(lroundf(pace.projected)));
        c_.setFont(&fonts::DejaVu18);
        c_.setTextColor(pc, kBg);
        if (Y(v1) - 6 - c_.fontHeight() < py){   // at the top edge: under it, inside the plot
            c_.setTextDatum(textdatum_t::top_right);
            c_.drawString(t, x1 - 6, Y(v1) + 6);
        } else {
            c_.setTextDatum(textdatum_t::bottom_right);
            c_.drawString(t, x1 - 2, Y(v1) - 6);
        }
    }
    if (week){
        const int yNow = Y(week->percent);
        c_.fillCircle(xNow, yNow, 8, kBg);
        c_.fillCircle(xNow, yNow, 6, curveColor);
    }
    if (chart.noHistory){
        c_.setFont(&fonts::DejaVu12);
        c_.setTextDatum(textdatum_t::top_left);
        c_.setTextColor(kDim, kBg);
        c_.drawString("no readings yet: the line is the average so far", px + 8, py + 6);
    }
}

void UsagePage::drawProjects(const Usage& usage, const Box& b){
    c_.setTextDatum(textdatum_t::top_left);
    c_.setFont(&fonts::DejaVu24);
    c_.setTextColor(kText, kBg);
    c_.drawString("Claude Code by project", b.x, b.y);
    if (usage.projects.empty()){
        c_.setFont(&fonts::DejaVu18);
        c_.setTextColor(kDim, kBg);
        c_.drawString("no Claude Code transcripts read this week", b.x, b.y + 44);
        return;
    }

    // Columns: name, bar (scaled to the largest), share of the weekly
    // limit this week, and today
    const int todayR = b.x + b.w;
    const int weekR  = todayR - 100;
    const int nameW  = b.w * 34 / 100;
    const int barX   = b.x + nameW + 12;
    const int barW   = weekR - 76 - barX;
    c_.setFont(&fonts::DejaVu12);
    c_.setTextDatum(textdatum_t::bottom_right);
    c_.setTextColor(kDim, kBg);
    c_.drawString("share of the weekly limit, estimated", todayR, b.y + 22);
    c_.setTextDatum(textdatum_t::top_right);
    c_.drawString("week", weekR, b.y + 34);
    c_.drawString("today", todayR, b.y + 34);

    float maxWeek = 0.0f;
    for (const UsageProject& p : usage.projects) if (p.week > maxWeek) maxWeek = p.week;
    const int n = static_cast<int>(usage.projects.size());
    int rowH = (b.h - 50) / (n > 0 ? n : 1);
    if (rowH > 44) rowH = 44;
    int y = b.y + 50;
    for (const UsageProject& p : usage.projects){
        const bool other = p.name == "other";
        const int cy = y + rowH / 2;
        c_.setFont(&fonts::DejaVu18);
        c_.setTextDatum(textdatum_t::middle_left);
        c_.setTextColor(other ? kDim : kText, kBg);
        c_.drawString(fit(c_, other ? "other projects" : p.name, nameW).c_str(), b.x, cy);

        const int bh = 12;
        c_.fillRoundRect(barX, cy - bh / 2, barW, bh, bh / 2, kRule);
        int fw = maxWeek > 0.0f ? static_cast<int>(barW * p.week / maxWeek) : 0;
        if (fw > 0){
            if (fw < bh) fw = bh;
            c_.fillRoundRect(barX, cy - bh / 2, fw, bh, bh / 2, other ? kFaint : kCode);
        }

        char t[16];
        percentText(p.week, t, sizeof(t));
        c_.setTextDatum(textdatum_t::middle_right);
        c_.setFont(&fonts::DejaVu24);
        c_.setTextColor(kText, kBg);
        c_.drawString(t, weekR, cy);
        c_.setFont(&fonts::DejaVu18);
        c_.setTextColor(kDim, kBg);
        if (p.today > 0.0f) percentText(p.today, t, sizeof(t));
        else snprintf(t, sizeof(t), "-");
        c_.drawString(t, todayR, cy);
        y += rowH;
    }
}

}  // namespace monitor
