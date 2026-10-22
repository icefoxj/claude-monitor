#include "monitor/screensaver.h"

#include <cmath>
#include <cstdio>

#include "monitor/usageview.h"

namespace monitor {

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kCardBg = rgb565(22, 22, 26);
constexpr uint16_t kText   = rgb565(235, 235, 235);
constexpr uint16_t kDim    = rgb565(140, 140, 140);

constexpr int   kEdge    = 4;      // black border: wipes the trail of a step of up to this many px
constexpr int   kWidth   = 600;    // the card itself
constexpr int   kPad     = 28;
constexpr int   kHeader  = 64;
constexpr int   kFooter  = 16;
constexpr float kMaxStep = 3.0f;   // px per tick, under kEdge even after a slow frame

const lgfx::IFont* const kFontSmall = &fonts::DejaVu18;
const lgfx::IFont* const kFontLabel = &fonts::DejaVu24;

int cardHeight(int rows){
    return kHeader + usageRowsHeight(rows) + kFooter;
}

}  // namespace

UsageSaver::UsageSaver(M5GFX& display) : display_(display), card_(&display) {}

void UsageSaver::setUsage(const Usage& usage, int64_t nowMs){
    usage_ = usage;
    have_ = usage.source != "none";
    receivedMs_ = nowMs;
    if (active_){
        render(nowMs);
        push();
    }
}

void UsageSaver::enter(int64_t nowMs){
    active_ = true;
    px_ = py_ = -1;
    display_.fillScreen(TFT_BLACK);
    ensureCanvas();
    // Start somewhere else each time, heading somewhere else
    const int maxX = display_.width() - card_.width();
    const int maxY = display_.height() - card_.height();
    const uint32_t seed = static_cast<uint32_t>(nowMs);
    x_ = maxX > 0 ? static_cast<float>((seed / 7) % static_cast<uint32_t>(maxX + 1)) : 0.0f;
    y_ = maxY > 0 ? static_cast<float>((seed / 13) % static_cast<uint32_t>(maxY + 1)) : 0.0f;
    vx_ = (seed & 1) ? 0.7f : -0.7f;
    vy_ = (seed & 2) ? 0.5f : -0.5f;
    render(nowMs);
    push();
}

void UsageSaver::leave(){
    active_ = false;
    px_ = py_ = -1;
}

void UsageSaver::tick(int frames, int64_t nowMs){
    if (!active_ || card_.width() == 0) return;
    const int maxX = display_.width() - card_.width();
    const int maxY = display_.height() - card_.height();

    float dx = vx_ * frames, dy = vy_ * frames;
    if (dx > kMaxStep) dx = kMaxStep;
    if (dx < -kMaxStep) dx = -kMaxStep;
    if (dy > kMaxStep) dy = kMaxStep;
    if (dy < -kMaxStep) dy = -kMaxStep;
    x_ += dx;
    y_ += dy;
    if (x_ < 0.0f){ x_ = 0.0f; vx_ = std::fabs(vx_); }
    if (y_ < 0.0f){ y_ = 0.0f; vy_ = std::fabs(vy_); }
    if (maxX <= 0) x_ = 0.0f;
    else if (x_ > maxX){ x_ = static_cast<float>(maxX); vx_ = -std::fabs(vx_); }
    if (maxY <= 0) y_ = 0.0f;
    else if (y_ > maxY){ y_ = static_cast<float>(maxY); vy_ = -std::fabs(vy_); }

    bool dirty = false;
    if (nowMs - renderedMs_ >= 1000){   // countdowns and the age
        render(nowMs);
        dirty = true;
    }
    if (dirty || static_cast<int>(x_) != px_ || static_cast<int>(y_) != py_){
        push();
    }
}

void UsageSaver::summary(char* out, size_t size) const {
    if (!have_ || usage_.limits.empty() || size < 2){
        snprintf(out, size, "-");
        return;
    }
    size_t n = 0;
    out[0] = '\0';
    for (const UsageLimit& l : usage_.limits){
        int w = snprintf(out + n, size - n, "%s%d", n ? "," : "", static_cast<int>(lroundf(l.percent)));
        if (w < 0 || static_cast<size_t>(w) >= size - n) break;
        n += static_cast<size_t>(w);
    }
}

void UsageSaver::ensureCanvas(){
    const int rows = (have_ && !usage_.limits.empty()) ? usageRowCount(usage_) : 1;
    if (rows == rows_ && card_.width() > 0) return;
    // A card of another height leaves its old outline on screen
    if (active_ && px_ >= 0 && card_.width() > 0){
        display_.fillRect(px_, py_, card_.width(), card_.height(), TFT_BLACK);
        px_ = py_ = -1;
    }
    card_.deleteSprite();
    card_.setPsram(true);
    card_.setColorDepth(16);
    if (!card_.createSprite(kWidth + 2 * kEdge, cardHeight(rows) + 2 * kEdge)){
        rows_ = -1;
        return;
    }
    rows_ = rows;
}

void UsageSaver::render(int64_t nowMs){
    ensureCanvas();
    renderedMs_ = nowMs;
    if (card_.width() == 0) return;

    card_.fillScreen(TFT_BLACK);
    card_.fillRoundRect(kEdge, kEdge, kWidth, cardHeight(rows_), 20, kCardBg);
    const int x0 = kEdge + kPad;
    const int x1 = kEdge + kWidth - kPad;
    const int32_t elapsed = static_cast<int32_t>((nowMs - receivedMs_) / 1000);

    int y = kEdge + 22;
    card_.setTextDatum(textdatum_t::top_left);
    card_.setFont(kFontLabel);
    card_.setTextColor(kDim, kCardBg);
    card_.drawString("Claude usage", x0, y);
    const bool data = have_ && !usage_.limits.empty();
    if (data){
        char age[40];
        usageAgeText(usage_.ageSec + elapsed, age, sizeof(age));
        card_.setFont(kFontSmall);
        card_.setTextDatum(textdatum_t::top_right);
        card_.drawString(age, x1, y + 4);
    }

    y = kEdge + kHeader;
    if (!data){
        card_.setTextDatum(textdatum_t::top_left);
        card_.setFont(kFontLabel);
        card_.setTextColor(kText, kCardBg);
        card_.drawString("No usage data yet", x0, y + 16);
        card_.setFont(kFontSmall);
        card_.setTextColor(kDim, kCardBg);
        card_.drawString("waiting for the host to send it", x0, y + 56);
        return;
    }
    drawUsageRows(card_, x0, y, x1 - x0, usageRowsHeight(rows_), usage_, elapsed, kCardBg);
}

void UsageSaver::push(){
    if (card_.width() == 0) return;
    px_ = static_cast<int>(x_);
    py_ = static_cast<int>(y_);
    card_.pushSprite(px_, py_);
}

}  // namespace monitor
