// The LCARS theme's icons, as Icons members: the same 128-space geometry
// scaled to the canvas, drawn upright (the Tab5 turns the whole display).
// Work in progress is a reactor column whose light runs from both ends to
// the chamber in the middle; the states that want you are a capsule with
// the word in black and a line under it, between rows of bars.

#include "monitor/icons.h"

#include <cmath>

#include "monitor/lcars.h"

namespace monitor {

using namespace lcars;

namespace {

constexpr float kPi = 3.14159265f;

inline int ip(float v){ return static_cast<int>(lroundf(v)); }

// The column's side bars: x from, x to (128-space), and where they are cut
constexpr float kBarTop = -52.0f, kBarBottom = 52.0f, kBarCut = 18.0f;

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

void Icons::lcarsCore(float spin, GearStyle style){
    canvas_.fillSprite(kBg);
    const float u = unit_;
    auto X = [&](float v){ return ip(cx_ + v * u); };
    auto Y = [&](float v){ return ip(cy_ + v * u); };

    const bool slow = style == GearStyle::Compacting;
    const uint16_t dim  = slow ? rgb(72, 52, 36) : rgb(36, 52, 104);
    const uint16_t mid  = slow ? kPeach : kViolet;
    const uint16_t hot  = slow ? kTan : kBlue;
    const uint16_t peak = rgb(235, 245, 255);

    // The light runs from both ends to the chamber: 3 pulses a turn of the
    // spin (about one a second), half that while compacting
    float p = spin * (slow ? 1.5f : 3.0f) / (2.0f * kPi);
    p -= floorf(p);
    const int k = 5 - static_cast<int>(p * 6.0f);   // 5 (the ends) .. 0 (the chamber)

    for (int i = -5; i <= 5; i++){
        const int a = i < 0 ? -i : i;
        const bool chamber = i == 0;
        const float w = chamber ? 48.0f : 30.0f, h = chamber ? 10.0f : 7.5f;
        uint16_t c = a == k ? hot : (a == k + 1 ? mid : dim);
        if (chamber) c = k == 0 ? peak : mid;
        const float y = i * 10.5f;
        capsule(canvas_, X(-w / 2), Y(y - h / 2), X(w / 2) - X(-w / 2), Y(y + h / 2) - Y(y - h / 2), c);
    }

    if (style == GearStyle::Subagents){
        // Two smaller columns beside it, out of step: work running in parallel
        float q = p + 0.5f;
        q -= floorf(q);
        const int kq = 2 - static_cast<int>(q * 3.0f);
        for (float side : { -36.0f, 36.0f }){
            for (int i = -2; i <= 2; i++){
                const int a = i < 0 ? -i : i;
                const uint16_t c = a == kq ? kLilac : rgb(64, 44, 72);
                const float y = i * 10.5f;
                capsule(canvas_, X(side - 6.0f), Y(y - 3.0f), X(side + 6.0f) - X(side - 6.0f), Y(y + 3.0f) - Y(y - 3.0f), c);
            }
        }
    }

    // Side bars: lilac on the left; on the right the track the work ring fills
    for (int s = 0; s < 2; s++){
        const float x0 = s == 0 ? -56.0f : 48.0f;
        capsule(canvas_, X(x0), Y(kBarTop), X(x0 + 8.0f) - X(x0), Y(kBarBottom) - Y(kBarTop), s == 0 ? kLilac : kDim);
        canvas_.fillRect(X(x0), Y(-kBarCut - 1.5f), X(x0 + 8.0f) - X(x0), ip(3.0f * u), kBg);
        canvas_.fillRect(X(x0), Y(kBarCut - 1.5f), X(x0 + 8.0f) - X(x0), ip(3.0f * u), kBg);
    }
}

void Icons::lcarsPanel(Panel kind, float scale){
    canvas_.fillSprite(kBg);
    const float u = unit_;
    auto X = [&](float v){ return ip(cx_ + v * u); };
    auto Y = [&](float v){ return ip(cy_ + v * u); };

    State st = State::Idle;
    switch (kind){
        case Panel::Warning:  st = State::WaitingUser; break;
        case Panel::Question: st = State::Question;    break;
        case Panel::Error:    st = State::Error;       break;
        case Panel::Done:     st = State::Idle;        break;
    }
    const Look lk = look(st);
    const bool alarm = kind == Panel::Error;
    const bool flash = scale > 1.03f;   // the entry pulse: lit, dark, lit...

    // A row of bars above and below; all red for a failure
    auto row = [&](float y, float h, uint16_t a, uint16_t b, uint16_t c){
        endCap(canvas_, X(-58), Y(y), X(-12) - X(-58), Y(y + h) - Y(y), true, a);
        canvas_.fillRect(X(-8), Y(y), X(14) - X(-8), Y(y + h) - Y(y), b);
        endCap(canvas_, X(18), Y(y), X(58) - X(18), Y(y + h) - Y(y), false, c);
    };
    if (alarm){
        row(-54, 9, kRed, kRed, kRed);
        row(41, 9, kRed, kRed, kRed);
    } else {
        row(-54, 9, lk.color, kTan, kPeriwinkle);
        row(41, 9, kPeriwinkle, lk.color, kTan);
    }

    // The capsule with the word
    const int bx = X(-58), by = Y(-36), bw = X(58) - bx, bh = Y(8) - by;
    if (flash){
        capsule(canvas_, bx, by, bw, bh, lk.color);
        capsule(canvas_, bx + ip(3 * u), by + ip(3 * u), bw - ip(6 * u), bh - ip(6 * u), kBg);
    } else {
        capsule(canvas_, bx, by, bw, bh, lk.color);
    }
    const int wpx = fitSize(canvas_, lk.word, Face::LcarsBig, ip(34 * u), bw - bh);
    text(canvas_, lk.word, X(0), Y(-14), Face::LcarsBig, wpx, flash ? lk.color : kBg, textdatum_t::middle_center);

    const int dpx = fitSize(canvas_, lk.detail, Face::Lcars, ip(11 * u), bw);
    text(canvas_, lk.detail, X(0), Y(24), Face::Lcars, dpx, lk.color, textdatum_t::middle_center);
}

void Icons::lcarsStandby(const Hourglass& h){
    canvas_.fillSprite(kBg);
    const float u = unit_;
    auto X = [&](float v){ return ip(cx_ + v * u); };
    auto Y = [&](float v){ return ip(cy_ + v * u); };
    const Look lk = look(State::Paused);

    endCap(canvas_, X(-58), Y(-54), X(-12) - X(-58), Y(-45) - Y(-54), true, kGold);
    canvas_.fillRect(X(-8), Y(-54), X(14) - X(-8), Y(-45) - Y(-54), kTan);
    endCap(canvas_, X(18), Y(-54), X(58) - X(18), Y(-45) - Y(-54), false, kPeriwinkle);

    const int bx = X(-58), by = Y(-36), bw = X(58) - bx, bh = Y(8) - by;
    capsule(canvas_, bx, by, bw, bh, lk.color);
    const int wpx = fitSize(canvas_, lk.word, Face::LcarsBig, ip(34 * u), bw - bh);
    text(canvas_, lk.word, X(0), Y(-14), Face::LcarsBig, wpx, kBg, textdatum_t::middle_center);
    const int dpx = fitSize(canvas_, lk.detail, Face::Lcars, ip(11 * u), bw);
    text(canvas_, lk.detail, X(0), Y(23), Face::Lcars, dpx, lk.color, textdatum_t::middle_center);

    // What is left of the wait: twelve bars running down, blinking while the
    // glass would turn over, full again after it
    float left = 1.0f - h.drained;
    if (h.flip > 0.0f) left = (static_cast<int>(h.flip * 6.0f) % 2 == 0) ? 1.0f : 0.0f;
    else if (h.settle < 1.0f) left = 1.0f;
    int lit = static_cast<int>(lroundf(left * 12.0f));
    if (lit == 0 && left > 0.0f) lit = 1;
    for (int i = 0; i < 12; i++){
        const float x0 = -58.0f + i * (116.0f / 12.0f);
        const float x1 = x0 + 116.0f / 12.0f - 2.0f;
        const uint16_t c = i < lit ? kGold : kDim;
        if (i == 0)       endCap(canvas_, X(x0), Y(41), X(x1) - X(x0), Y(50) - Y(41), true, c);
        else if (i == 11) endCap(canvas_, X(x0), Y(41), X(x1) - X(x0), Y(50) - Y(41), false, c);
        else              canvas_.fillRect(X(x0), Y(41), X(x1) - X(x0), Y(50) - Y(41), c);
    }
}

void Icons::lcarsRing(float fraction, int lap){
    // The right-hand side bar of the column fills from the bottom
    static constexpr uint16_t kLap[] = { kBlue, kGold, kRed };
    const float u = unit_;
    auto X = [&](float v){ return ip(cx_ + v * u); };
    auto Y = [&](float v){ return ip(cy_ + v * u); };
    const int x = X(48.0f), w = X(56.0f) - x;
    const int top = Y(kBarTop), bottom = Y(kBarBottom);
    auto fill = [&](float f, uint16_t c){
        const int y = bottom - static_cast<int>((bottom - top) * f);
        if (bottom - y <= 0) return;
        // Clip the track's capsule to the part filled
        int32_t cx, cy, cw, ch;
        canvas_.getClipRect(&cx, &cy, &cw, &ch);
        canvas_.setClipRect(x, y, w, bottom - y);
        capsule(canvas_, x, top, w, bottom - top, c);
        canvas_.setClipRect(cx, cy, cw, ch);
    };
    if (lap >= 3){
        fill(1.0f, kRed);
    } else {
        if (lap > 0) fill(1.0f, kLap[lap - 1]);
        fill(fraction, kLap[lap]);
    }
    canvas_.fillRect(x, Y(-kBarCut - 1.5f), w, ip(3.0f * u), kBg);
    canvas_.fillRect(x, Y(kBarCut - 1.5f), w, ip(3.0f * u), kBg);
}

void Icons::lcarsDots(std::string_view codes){
    int n = static_cast<int>(codes.size());
    if (n > 8) n = 8;
    const float u = unit_;
    for (int i = 0; i < n; i++){
        const float x = cx_ + (i - (n - 1) * 0.5f) * 10.0f * u;
        const float y = cy_ + 60.0f * u;
        capsule(canvas_, ip(x - 4.0f * u), ip(y - 2.5f * u), ip(8.0f * u), ip(5.0f * u), look(stateForCode(codes[i])).color);
    }
}

void Icons::lcarsLinkLost(){
    // A grey capsule across the top: no heartbeat from the host
    const float u = unit_;
    const int px = ip(9.0f * u), h = ip(11.0f * u);
    const int w = textWidth(canvas_, "NO LINK", Face::Lcars, px) + h;
    const int x = ip(cx_) - w / 2, y = ip(cy_ - 63.0f * u);
    const int o = ip(1.5f * u) > 2 ? ip(1.5f * u) : 2;
    capsule(canvas_, x - o, y - o, w + 2 * o, h + 2 * o, kBg);
    capsule(canvas_, x, y, w, h, kGrey);
    text(canvas_, "NO LINK", ip(cx_), y + h / 2, Face::Lcars, px, kBg, textdatum_t::middle_center, kGrey);
}

void Icons::lcarsToolMark(){
    // An orange capsule reading EXT at the bottom right: a tool process runs
    const float u = unit_;
    const int x = ip(cx_ + 28.0f * u), y = ip(cy_ + 47.0f * u);
    const int w = ip(cx_ + 63.0f * u) - x, h = ip(cy_ + 63.0f * u) - y;
    const int o = ip(1.5f * u) > 2 ? ip(1.5f * u) : 2;
    capsule(canvas_, x - o, y - o, w + 2 * o, h + 2 * o, kBg);
    capsule(canvas_, x, y, w, h, kOrange);
    text(canvas_, "EXT", x + w / 2, y + h / 2, Face::Lcars, ip(12.0f * u), kBg, textdatum_t::middle_center, kOrange);
}

}  // namespace monitor
