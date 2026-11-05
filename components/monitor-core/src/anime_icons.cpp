// The anime theme's icons, as Icons members: the same 128-space geometry
// scaled to the canvas, drawn upright (the Tab5 turns the whole display).
// Work in progress is a turning reticle; the states that want you are
// panels with a big kanji, the English under it and hazard stripes when it
// is red; the usage-limit wait is a panel whose blocks run down.

#include "monitor/icons.h"

#include <cmath>

#include "monitor/anime.h"

namespace monitor {

using namespace anime;

namespace {

constexpr float kPi = 3.14159265f;

inline int ip(float v){ return static_cast<int>(lroundf(v)); }

// Ring slice between radii r0 and r1 (px) from a0 to a1 degrees (0 = up,
// clockwise), in steps of at most 6 degrees
void sector(LovyanGFX& g, float cx, float cy, float r0, float r1, float a0, float a1, uint16_t color){
    int steps = static_cast<int>(ceilf(fabsf(a1 - a0) / 6.0f));
    if (steps < 1) steps = 1;
    for (int i = 0; i < steps; i++){
        const float t0 = (a0 + (a1 - a0) * i / steps - 90.0f) * kPi / 180.0f;
        const float t1 = (a0 + (a1 - a0) * (i + 1) / steps - 90.0f) * kPi / 180.0f;
        const int x00 = ip(cx + r0 * cosf(t0)), y00 = ip(cy + r0 * sinf(t0));
        const int x10 = ip(cx + r1 * cosf(t0)), y10 = ip(cy + r1 * sinf(t0));
        const int x01 = ip(cx + r0 * cosf(t1)), y01 = ip(cy + r0 * sinf(t1));
        const int x11 = ip(cx + r1 * cosf(t1)), y11 = ip(cy + r1 * sinf(t1));
        g.fillTriangle(x00, y00, x10, y10, x11, y11, color);
        g.fillTriangle(x00, y00, x11, y11, x01, y01, color);
    }
}

// The reticle's scale: a tick every 5 degrees, longer every 30
constexpr int kTicks = 72;

void tick(LovyanGFX& g, float cx, float cy, float u, int i, uint16_t color){
    const bool major = i % 6 == 0;
    const float a = i * 5.0f, hw = major ? 1.0f : 0.6f;
    sector(g, cx, cy, (major ? 49.0f : 53.0f) * u, 57.0f * u, a - hw, a + hw, color);
}

State stateForCode(char c){
    switch (c){
        case 'p': return State::Processing;
        case 'w': return State::WaitingUser;
        case 'q': return State::Question;
        case 'e': return State::Error;
        case 'h': return State::Paused;
        case 'c': return State::Compacting;
        case 'i': return State::Idle;
        default:  return State::Off;
    }
}

}  // namespace

void Icons::animeReticle(float spin, GearStyle style){
    canvas_.fillSprite(kBg);
    const bool grey = style == GearStyle::Compacting;
    const uint16_t main = grey ? kGrey : kOrange;
    const uint16_t mid  = grey ? rgb(84, 84, 96) : kOrangeDim;
    const uint16_t dark = grey ? kGreyDark : kOrangeDark;
    const float u = unit_;

    for (int i = 0; i < kTicks; i++) tick(canvas_, cx_, cy_, u, i, i % 6 == 0 ? mid : dark);

    // Three arcs of six blocks turning with the work, and an inner ring
    // turning the other way
    const float deg = spin * 180.0f / kPi;
    for (int k = 0; k < 3; k++){
        for (int b = 0; b < 6; b++){
            const float a0 = deg + k * 120.0f + b * 12.0f;
            sector(canvas_, cx_, cy_, 40.0f * u, 47.0f * u, a0, a0 + 10.0f, main);
        }
    }
    for (int b = 0; b < 24; b++){
        const float a0 = -deg * 1.6f + b * 15.0f;
        sector(canvas_, cx_, cy_, 31.0f * u, 34.0f * u, a0, a0 + 9.0f, mid);
    }
    if (style == GearStyle::Subagents){
        // Satellites in the gaps between the arcs: work running in parallel
        for (int k = 0; k < 3; k++){
            const float a = (deg + k * 120.0f + 96.0f - 90.0f) * kPi / 180.0f;
            hexagon(canvas_, cx_ + 43.5f * u * cosf(a), cy_ + 43.5f * u * sinf(a), 6.0f * u, kAmber);
        }
    }

    hexagonOutline(canvas_, cx_, cy_, 24.0f * u, 2.2f * u, main);
    const char* word = grey ? "圧縮" : (style == GearStyle::Subagents ? "並列" : "処理");
    text(canvas_, word, ip(cx_), ip(cy_ + u), Face::Kanji, ip(19.0f * u), kText, textdatum_t::middle_center);
}

void Icons::animePanel(Panel kind, float scale){
    canvas_.fillSprite(kBg);
    struct Spec { uint16_t color, fill, ink; const char* jp; const char* en; bool stripes; };
    Spec sp{};
    switch (kind){
        case Panel::Warning:  sp = { kRed,   kRedDark,   kText,  "警告", "WARNING",  true };  break;
        case Panel::Question: sp = { kCyan,  kCyanDark,  kText,  "質問", "QUESTION", false }; break;
        case Panel::Error:    sp = { kRed,   kRed,       kText,  "異常", "ERROR",    true };  break;
        case Panel::Done:     sp = { kGreen, kGreenDark, kGreen, "完了", "COMPLETE", false }; break;
    }
    // The entry pulse flashes the panel: lit, dark, lit, ...
    const bool flash = scale > 1.03f;
    const uint16_t fill  = flash ? sp.color : sp.fill;
    const uint16_t ink   = flash ? kBg : sp.ink;
    const uint16_t label = flash ? kBg : (kind == Panel::Error ? kText : sp.color);

    const float u = unit_;
    auto X = [&](float v){ return ip(cx_ + v * u); };
    auto Y = [&](float v){ return ip(cy_ + v * u); };
    const int line = ip(2.2f * u) > 2 ? ip(2.2f * u) : 2;

    const int bx = X(-58), by = Y(-45), bw = X(58) - bx, bh = Y(45) - by;
    const int cut = ip(12.0f * u);
    fillFrame(canvas_, bx, by, bw, bh, cut, fill);
    frame(canvas_, bx, by, bw, bh, cut, line, sp.color);

    if (sp.stripes){
        hazard(canvas_, X(-58), Y(-58), X(58) - X(-58), Y(-50) - Y(-58), ip(5.0f * u), sp.color, kBg);
        hazard(canvas_, X(-58), Y(50), X(58) - X(-58), Y(58) - Y(50), ip(5.0f * u), sp.color, kBg);
    } else {
        // A scale above and below: a line with a tick every 8 units
        const int t = ip(u) > 1 ? ip(u) : 1;
        for (float v : { -55.0f, 55.0f }){
            canvas_.fillRect(X(-58), Y(v), X(58) - X(-58), t, sp.color);
            for (int k = -56; k <= 56; k += 8){
                canvas_.fillRect(X(static_cast<float>(k)), v < 0 ? Y(v) - ip(3.0f * u) : Y(v), t, ip(3.0f * u), sp.color);
            }
        }
    }

    text(canvas_, sp.jp, X(0), Y(-7), Face::Kanji, ip(50.0f * u), ink, textdatum_t::middle_center);
    text(canvas_, sp.en, X(0), Y(31), Face::Cond, ip(12.0f * u), label, textdatum_t::middle_center, -1, ip(2.5f * u));
}

void Icons::animeStandby(const Hourglass& h){
    canvas_.fillSprite(kBg);
    const float u = unit_;
    auto X = [&](float v){ return ip(cx_ + v * u); };
    auto Y = [&](float v){ return ip(cy_ + v * u); };
    const int line = ip(2.2f * u) > 2 ? ip(2.2f * u) : 2;

    const int bx = X(-58), by = Y(-45), bw = X(58) - bx, bh = Y(45) - by;
    const int cut = ip(12.0f * u);
    fillFrame(canvas_, bx, by, bw, bh, cut, kAmberDark);
    frame(canvas_, bx, by, bw, bh, cut, line, kAmber);
    hazard(canvas_, X(-58), Y(-58), X(58) - X(-58), Y(-50) - Y(-58), ip(5.0f * u), kAmber, kBg);
    hazard(canvas_, X(-58), Y(50), X(58) - X(-58), Y(58) - Y(50), ip(5.0f * u), kAmber, kBg);

    text(canvas_, "待機", X(0), Y(-14), Face::Kanji, ip(40.0f * u), kText, textdatum_t::middle_center);

    // What is left of the wait runs down, blinks while the glass would
    // turn over, and is full again after it
    float left = 1.0f - h.drained;
    if (h.flip > 0.0f) left = (static_cast<int>(h.flip * 6.0f) % 2 == 0) ? 1.0f : 0.0f;
    else if (h.settle < 1.0f) left = 1.0f;
    blocks(canvas_, X(-42), Y(15), X(42) - X(-42), Y(25) - Y(15), left, kAmber, rgb(90, 56, 0));
    text(canvas_, "STANDBY", X(0), Y(35), Face::Cond, ip(9.0f * u), kAmber, textdatum_t::middle_center, -1, ip(2.0f * u));
}

void Icons::animeRing(float fraction, int lap){
    static constexpr uint16_t kLap[] = { kAmber, kOrange, kRed };
    const float u = unit_;
    if (lap >= 3){
        for (int i = 0; i < kTicks; i++) tick(canvas_, cx_, cy_, u, i, kRed);
        return;
    }
    if (lap > 0){
        for (int i = 0; i < kTicks; i++) tick(canvas_, cx_, cy_, u, i, kLap[lap - 1]);
    }
    const int lit = static_cast<int>(fraction * kTicks);
    for (int i = 0; i < lit; i++) tick(canvas_, cx_, cy_, u, i, kLap[lap]);
}

void Icons::animeDots(std::string_view codes){
    int n = static_cast<int>(codes.size());
    if (n > 8) n = 8;
    const float u = unit_;
    for (int i = 0; i < n; i++){
        const float x = cx_ + (i - (n - 1) * 0.5f) * 10.0f * u;
        const float y = cy_ + 61.0f * u;
        hexagon(canvas_, x, y, 4.6f * u, kBg);
        hexagon(canvas_, x, y, 3.6f * u, look(stateForCode(codes[i])).color);
    }
}

void Icons::animeLinkLost(){
    // A grey tag across the top: no heartbeat from the host
    const float u = unit_;
    const int px = ip(9.0f * u), track = ip(1.5f * u), pad = ip(5.0f * u), h = ip(11.0f * u);
    const int w = textWidth(canvas_, "NO LINK", Face::Cond, px, track) + 2 * pad;
    const int x = ip(cx_) - w / 2, y = ip(cy_ - 63.0f * u);
    const int cut = ip(3.0f * u), o = ip(1.5f * u) > 2 ? ip(1.5f * u) : 2;
    fillFrame(canvas_, x - o, y - o, w + 2 * o, h + 2 * o, cut, kBg);
    fillFrame(canvas_, x, y, w, h, cut, kGrey);
    text(canvas_, "NO LINK", ip(cx_), y + h / 2, Face::Cond, px, kBg, textdatum_t::middle_center, kGrey, track);
}

void Icons::animeToolMark(){
    // An orange EXT tag at the bottom right: a tool process is running
    const float u = unit_;
    const int x = ip(cx_ + 30.0f * u), y = ip(cy_ + 46.0f * u);
    const int w = ip(cx_ + 63.0f * u) - x, h = ip(cy_ + 63.0f * u) - y;
    const int cut = ip(4.0f * u), o = ip(1.5f * u) > 2 ? ip(1.5f * u) : 2;
    fillFrame(canvas_, x - o, y - o, w + 2 * o, h + 2 * o, cut, kBg);
    fillFrame(canvas_, x, y, w, h, cut, kOrange);
    text(canvas_, "EXT", x + w / 2, y + h / 2, Face::Cond, ip(12.0f * u), kBg, textdatum_t::middle_center, kOrange, ip(1.0f * u));
}

}  // namespace monitor
