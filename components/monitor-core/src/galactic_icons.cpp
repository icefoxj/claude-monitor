// The galactic theme's icons, as Icons members: the same 128-space geometry
// scaled to the canvas, drawn upright (the Tab5 turns the whole display).
// Work in progress is the trench run: a trench in perspective whose ribs
// rush at you under a targeting cross; the states that want you are a
// reticle (locked on, searching, struck out, clear) over the word; the
// usage-limit wait is a range countdown.

#include "monitor/icons.h"

#include <cmath>
#include <cstdio>

#include "monitor/galactic.h"

namespace monitor {

using namespace galactic;

namespace {

constexpr float kPi = 3.14159265f;

inline int ip(float v){ return static_cast<int>(lroundf(v)); }

// A line `t` px thick (t parallel lines, offset across its direction)
void line(LovyanGFX& g, float x0, float y0, float x1, float y1, int t, uint16_t color){
    const bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
    for (int i = 0; i < t; i++){
        const float o = i - (t - 1) * 0.5f;
        if (steep) g.drawLine(ip(x0 + o), ip(y0), ip(x1 + o), ip(y1), color);
        else       g.drawLine(ip(x0), ip(y0 + o), ip(x1), ip(y1 + o), color);
    }
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

void Icons::galacticTrench(float spin, GearStyle style){
    canvas_.fillSprite(kBg);
    const float u = unit_;
    auto X = [&](float v){ return cx_ + v * u; };
    auto Y = [&](float v){ return cy_ + v * u; };
    const bool slow = style == GearStyle::Compacting;
    const uint16_t lit = slow ? kSteel : kYellow;

    // The trench: walls 60 units out, their tops 40 above the vanishing
    // point, the floor 56 below, seen at depth z as (x / z, y / z)
    const float vx = 0.0f, vy = -6.0f;
    const float wx = 60.0f, top = -40.0f, floor = 56.0f;
    const int t = u >= 3.0f ? 2 : 1;
    for (float ex : { -wx, wx }){
        for (float ey : { top, floor }){
            line(canvas_, X(vx + ex), Y(vy + ey), X(vx), Y(vy), t, shade(lit, 55));
        }
    }

    // The ribs rush at you: one gap every third of a turn of the spin (a
    // little over half a second), half that while compacting
    float p = spin * (slow ? 1.5f : 3.0f) / (2.0f * kPi);
    p -= floorf(p);
    for (int i = 0; i < 8; i++){
        const float z = 1.0f + (i + 1 - p) * 1.0f;
        int bright = static_cast<int>(130.0f / z);
        if (bright > 100) bright = 100;
        const uint16_t c = shade(lit, bright);
        const float l = vx - wx / z, r = vx + wx / z, tt = vy + top / z, f = vy + floor / z;
        const int lt = z < 2.5f ? t : 1;
        line(canvas_, X(l), Y(tt), X(l), Y(f), lt, c);
        line(canvas_, X(l), Y(f), X(r), Y(f), lt, c);
        line(canvas_, X(r), Y(f), X(r), Y(tt), lt, c);
    }

    if (style == GearStyle::Subagents){
        // Wingmen in the trench: two diamonds keeping station, bobbing
        const float bob = sinf(p * 2.0f * kPi) * 3.0f;
        for (float side : { -24.0f, 24.0f }){
            const float x = vx + side, y = vy + 22.0f + (side < 0 ? bob : -bob);
            const float d = 5.0f;
            line(canvas_, X(x - d), Y(y), X(x), Y(y - d), t, kGreen);
            line(canvas_, X(x), Y(y - d), X(x + d), Y(y), t, kGreen);
            line(canvas_, X(x + d), Y(y), X(x), Y(y + d), t, kGreen);
            line(canvas_, X(x), Y(y + d), X(x - d), Y(y), t, kGreen);
        }
    }

    // The targeting cross on the vanishing point
    line(canvas_, X(vx - 24), Y(vy), X(vx - 9), Y(vy), t, kGreen);
    line(canvas_, X(vx + 9), Y(vy), X(vx + 24), Y(vy), t, kGreen);
    line(canvas_, X(vx - 24), Y(vy - 4), X(vx - 24), Y(vy + 4), t, kGreen);
    line(canvas_, X(vx + 24), Y(vy - 4), X(vx + 24), Y(vy + 4), t, kGreen);
    line(canvas_, X(vx), Y(vy - 9), X(vx), Y(vy - 4), t, kGreen);
    line(canvas_, X(vx), Y(vy + 4), X(vx), Y(vy + 9), t, kGreen);
}

void Icons::galacticPanel(Panel kind, float scale){
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
    const bool flash = scale > 1.03f;   // the entry pulse: lit, dark, lit...
    const int t = ip(2.5f * u) > 2 ? ip(2.5f * u) : 2;

    // The reticle, and what it says: locked on (brackets close in), searching
    // (dashed, brackets wide), struck out (a cross, the canvas bracketed in
    // red), clear (a dot in the middle)
    const int rcx = X(0), rcy = Y(-22), r = ip(22 * u);
    if (kind == Panel::Question){
        for (int a = 0; a < 360; a += 30){
            for (int k = 0; k < t; k++){
                canvas_.drawArc(rcx, rcy, r - k, r - k, a, a + 18, lk.color);
            }
        }
    } else {
        circle(canvas_, rcx, rcy, r, t, lk.color);
    }
    for (int k = 0; k < 4; k++){
        const float a = k * kPi / 2.0f;
        const float c = cosf(a), s = sinf(a);
        line(canvas_, rcx + c * (r + 3 * u), rcy + s * (r + 3 * u), rcx + c * (r + 10 * u), rcy + s * (r + 10 * u), t, lk.color);
        line(canvas_, rcx + c * (r - 6 * u), rcy + s * (r - 6 * u), rcx + c * (r - 2 * u), rcy + s * (r - 2 * u), t, lk.color);
    }
    switch (kind){
        case Panel::Warning:
            brackets(canvas_, X(-32), Y(-54), X(32) - X(-32), Y(10) - Y(-54), ip(11 * u), t, lk.color);
            canvas_.fillCircle(rcx, rcy, ip(4 * u), lk.color);
            break;
        case Panel::Question:
            brackets(canvas_, X(-40), Y(-60), X(40) - X(-40), Y(16) - Y(-60), ip(8 * u), t, lk.color);
            break;
        case Panel::Error:
            line(canvas_, rcx - r * 0.7f, rcy - r * 0.7f, rcx + r * 0.7f, rcy + r * 0.7f, t + 1, lk.color);
            line(canvas_, rcx - r * 0.7f, rcy + r * 0.7f, rcx + r * 0.7f, rcy - r * 0.7f, t + 1, lk.color);
            brackets(canvas_, X(-62), Y(-62), X(62) - X(-62), Y(62) - Y(-62), ip(16 * u), t, lk.color);
            break;
        case Panel::Done:
            canvas_.fillCircle(rcx, rcy, ip(6 * u), lk.color);
            break;
    }

    // The word, on a bar of its colour while the entry pulse flashes
    const int wpx = fitSize(canvas_, lk.word, Face::GalacticBig, ip(16 * u), X(56) - X(-56));
    if (flash){
        canvas_.fillRect(X(-58), Y(22), X(58) - X(-58), Y(40) - Y(22), lk.color);
        text(canvas_, lk.word, X(0), Y(31), Face::GalacticBig, wpx, kBg, textdatum_t::middle_center, lk.color);
    } else {
        text(canvas_, lk.word, X(0), Y(31), Face::GalacticBig, wpx, lk.color, textdatum_t::middle_center);
    }
    const int dpx = fitSize(canvas_, lk.detail, Face::Galactic, ip(8.5f * u), X(58) - X(-58));
    text(canvas_, lk.detail, X(0), Y(48), Face::Galactic, dpx, kWhite, textdatum_t::middle_center);
}

void Icons::galacticStandby(const Hourglass& h){
    canvas_.fillSprite(kBg);
    const float u = unit_;
    auto X = [&](float v){ return ip(cx_ + v * u); };
    auto Y = [&](float v){ return ip(cy_ + v * u); };
    const Look lk = look(State::Paused);
    const int t = ip(2.0f * u) > 2 ? ip(2.0f * u) : 2;

    // The range to the end of the wait, counting down; zeroes blinking while
    // the glass would turn over, full again after it
    float left = 1.0f - h.drained;
    bool shown = true;
    if (h.flip > 0.0f){ left = 0.0f; shown = static_cast<int>(h.flip * 6.0f) % 2 == 0; }
    else if (h.settle < 1.0f) left = 1.0f;
    text(canvas_, "RANGE", X(0), Y(-46), Face::Galactic, ip(9 * u), kAmber, textdatum_t::middle_center);
    char range[12];
    snprintf(range, sizeof(range), "%04.1f", left * 99.9f);
    if (shown) text(canvas_, range, X(0), Y(-20), Face::GalacticBig, ip(28 * u), lk.color, textdatum_t::middle_center);

    // A scale under it with a marker at the range
    canvas_.fillRect(X(-50), Y(4), X(50) - X(-50), t, kDim);
    for (int k = 0; k <= 10; k++) canvas_.fillRect(X(-50 + k * 10.0f), Y(4) - ip(3 * u), t, ip(3 * u), kDim);
    const int mx = X(-50 + 100.0f * left);
    canvas_.fillTriangle(mx, Y(4) + t, mx - ip(4 * u), Y(11), mx + ip(4 * u), Y(11), lk.color);

    text(canvas_, lk.word, X(0), Y(28), Face::GalacticBig, ip(17 * u), lk.color, textdatum_t::middle_center);
    text(canvas_, lk.detail, X(0), Y(45), Face::Galactic, ip(9 * u), kWhite, textdatum_t::middle_center);
}

void Icons::galacticRing(float fraction, int lap){
    // The work time as a line along the bottom, filling from the left
    static constexpr uint16_t kLap[] = { kYellow, kAmber, kRed };
    const float u = unit_;
    const int x0 = ip(cx_ - 58 * u), x1 = ip(cx_ + 58 * u), y = ip(cy_ + 58 * u);
    const int h = ip(2.5f * u) > 2 ? ip(2.5f * u) : 2;
    canvas_.fillRect(x0, y, x1 - x0, h, kDim);
    if (lap >= 3){ canvas_.fillRect(x0, y, x1 - x0, h, kRed); return; }
    if (lap > 0) canvas_.fillRect(x0, y, x1 - x0, h, kLap[lap - 1]);
    canvas_.fillRect(x0, y, static_cast<int>((x1 - x0) * fraction), h, kLap[lap]);
}

void Icons::galacticDots(std::string_view codes){
    int n = static_cast<int>(codes.size());
    if (n > 8) n = 8;
    const float u = unit_;
    const int s = ip(4.0f * u);
    for (int i = 0; i < n; i++){
        const int x = ip(cx_ + (i - (n - 1) * 0.5f) * 9.0f * u);
        const int y = ip(cy_ - 60.0f * u);
        canvas_.fillRect(x - s / 2, y - s / 2, s, s, look(stateForCode(codes[i])).color);
    }
}

void Icons::galacticLinkLost(){
    // A grey box across the top: no heartbeat from the host
    const float u = unit_;
    const int px = ip(8.0f * u), pad = ip(3.0f * u);
    const int w = textWidth(canvas_, "NO SIGNAL", Face::Galactic, px) + 2 * pad;
    const int h = px + 2 * pad;
    const int x = ip(cx_) - w / 2, y = ip(cy_ - 63.0f * u);
    canvas_.fillRect(x, y, w, h, kBg);
    canvas_.drawRect(x, y, w, h, kGrey);
    text(canvas_, "NO SIGNAL", ip(cx_), y + h / 2, Face::Galactic, px, kGrey, textdatum_t::middle_center, kBg);
}

void Icons::galacticToolMark(){
    // EXT in a yellow box at the bottom right: a tool process runs
    const float u = unit_;
    const int x = ip(cx_ + 30.0f * u), y = ip(cy_ + 40.0f * u);
    const int w = ip(cx_ + 63.0f * u) - x, h = ip(cy_ + 55.0f * u) - y;
    canvas_.fillRect(x - 2, y - 2, w + 4, h + 4, kBg);
    for (int k = 0; k < (u >= 3.0f ? 2 : 1); k++) canvas_.drawRect(x + k, y + k, w - 2 * k, h - 2 * k, kYellow);
    text(canvas_, "EXT", x + w / 2, y + h / 2, Face::Galactic, ip(10.0f * u), kYellow, textdatum_t::middle_center, kBg);
}

}  // namespace monitor
