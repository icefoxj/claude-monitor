// The mech theme's icons, as Icons members: the HUD of the house AtomS3R
// monitors (HM-01) in 128-space, scaled to the canvas and drawn upright
// (the Tab5 turns the whole display). Every icon has the same frame:
// yellow corner brackets with hazard slashes (the livery), a ring of 36
// segments, a chip at the top with the state's word. Work in progress is
// a reticle with a comet of lit segments running round the ring; the
// states that want you light the whole ring around the stroke font's own
// "!", "?" or "X", or a check; the usage-limit wait drains the ring around
// a pause sign. The ground is kPanel, so the icon sits on its tile's panel.

#include "monitor/icons.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "monitor/mech.h"

namespace monitor {

using namespace mech;

namespace {

constexpr float kPi = 3.14159265f;

constexpr int   kSegments = 36;
constexpr float kRingIn   = 51.0f, kRingOut  = 58.0f;    // the segments
constexpr float kTrackIn  = 59.5f, kTrackOut = 61.5f;    // the work ring overlay, just outside them
constexpr float kChipY    = -38.0f, kChipCap = 6.0f;     // the chip with the state's word
constexpr float kDetailY  = 34.0f;                       // the line under the symbol (baseline)

inline int ip(float v){ return static_cast<int>(lroundf(v)); }

// 128-space to canvas pixels
struct Hud {
    LovyanGFX& g;
    float cx, cy, u;
    float X(float v) const { return cx + v * u; }
    float Y(float v) const { return cy + v * u; }
    float S(float v) const { return v * u; }

    void line(float x0, float y0, float x1, float y1, float t, uint16_t c) const {
        g.drawWideLine(ip(X(x0)), ip(Y(y0)), ip(X(x1)), ip(Y(y1)), S(t) * 0.5f, c);
    }

    // A band between radii r0 and r1 from a0 to a1 degrees (0 = right,
    // clockwise on screen), as quads of at most 4°
    void arc(float r0, float r1, float a0, float a1, uint16_t c) const {
        const int n = std::max(1, static_cast<int>(ceilf(fabsf(a1 - a0) / 4.0f)));
        for (int i = 0; i < n; i++){
            const float t0 = (a0 + (a1 - a0) * i / n) * kPi / 180.0f;
            const float t1 = (a0 + (a1 - a0) * (i + 1) / n) * kPi / 180.0f;
            const float c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
            const int ax = ip(X(r0 * c0)), ay = ip(Y(r0 * s0)), bx = ip(X(r1 * c0)), by = ip(Y(r1 * s0));
            const int qx = ip(X(r1 * c1)), qy = ip(Y(r1 * s1)), dx = ip(X(r0 * c1)), dy = ip(Y(r0 * s1));
            g.fillTriangle(ax, ay, bx, by, qx, qy, c);
            g.fillTriangle(ax, ay, qx, qy, dx, dy, c);
        }
    }

    // Segment i of the ring, clockwise from the top
    void segment(int i, uint16_t c) const {
        const float step = 360.0f / kSegments;
        const float a0 = -90.0f + i * step + 1.4f;
        arc(kRingIn, kRingOut, a0, a0 + step - 2.8f, c);
    }

    // The ground, the corner brackets and the hazard slashes in `livery`
    void frame(uint16_t livery) const {
        g.fillScreen(kPanel);
        const float e = 62.0f, arm = 15.0f, t = 2.6f;
        const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
        for (int k = 0; k < 4; k++){
            line(sx[k] * e, sy[k] * e, sx[k] * (e - arm), sy[k] * e, t, livery);
            line(sx[k] * e, sy[k] * e, sx[k] * e, sy[k] * (e - arm), t, livery);
        }
        for (int k = 0; k < 3; k++){
            const float o = 5.5f * k;
            line(-57 + o, -46, -46 + o, -57, 3.2f, livery);   // top left
            line(57 - o, 46, 46 - o, 57, 3.2f, livery);       // bottom right
        }
    }

    void chipAtTop(const char* word, uint16_t fill, uint16_t ink) const {
        chip(g, word, ip(X(0)), ip(Y(kChipY)), S(kChipCap), fill, ink, Align::Center);
    }

    void detail(const char* s, uint16_t c) const {
        if (!s || !s[0]) return;
        const float cap = fitCap(s, S(6.0f), S(68.0f));
        drawText(g, s, X(0), Y(kDetailY), cap, c, Align::Center);
    }

    // The reticle: a circle, a cross through it, a dot in the middle
    void reticle(uint16_t circle, uint16_t cross) const {
        g.fillSmoothCircle(ip(X(0)), ip(Y(0)), ip(S(20.3f)), circle);
        g.fillSmoothCircle(ip(X(0)), ip(Y(0)), ip(S(17.7f)), kPanel);
        line(-28, 0, -8, 0, 2.4f, cross);   // clear of the chip above
        line(8, 0, 28, 0, 2.4f, cross);
        line(0, -28, 0, -8, 2.4f, cross);
        line(0, 8, 0, 28, 2.4f, cross);
        g.fillSmoothCircle(ip(X(0)), ip(Y(0)), std::max(1, ip(S(2.6f))), cross);
    }
};

}  // namespace

void Icons::mechCore(float spin, GearStyle style){
    const Hud h{ canvas_, cx_, cy_, unit_ };
    const bool compacting = style == GearStyle::Compacting;
    const Look lk = look(compacting ? State::Compacting : State::Processing);
    h.frame(kYellow);

    // A comet of lit segments running clockwise: a lap per turn of the
    // spin (about two seconds), half that while compacting
    float p = spin * (compacting ? 0.5f : 1.0f) / (2.0f * kPi);
    p -= floorf(p);
    const int head = static_cast<int>(p * kSegments) % kSegments;
    constexpr int kTail = 12;
    for (int i = 0; i < kSegments; i++){
        const int behind = (head - i + kSegments) % kSegments;
        h.segment(i, behind < kTail ? mix(lk.color, kLine, static_cast<float>(behind) / kTail) : kLine);
    }

    // Two arcs turning the other way under the reticle
    const float a = -spin * 0.75f * 180.0f / kPi;
    h.arc(24.5f, 27.5f, a, a + 70.0f, lk.color);
    h.arc(24.5f, 27.5f, a + 180.0f, a + 250.0f, lk.color);
    h.reticle(lk.color, kWhite);

    // Beside it: chevrons lit in turn, outwards while working, inwards
    // while compacting; or, with subagents, two drones keeping station
    float q = spin * 1.5f / (2.0f * kPi);
    q -= floorf(q);
    if (style == GearStyle::Subagents){
        const float bob = sinf(q * 2.0f * kPi) * 2.0f;
        for (float side : { -40.0f, 40.0f }){
            const float y = side < 0 ? bob : -bob;
            for (float r = 6.7f; r < 7.6f; r += 0.4f) canvas_.drawCircle(ip(h.X(side)), ip(h.Y(y)), ip(h.S(r)), kCyan);
            diamond(canvas_, ip(h.X(side)), ip(h.Y(y)), ip(h.S(4.0f)), kCyan);
        }
    } else {
        const bool firstLit = compacting ? q >= 0.5f : q < 0.5f;   // the inner one
        for (float side : { -1.0f, 1.0f }){
            const bool outwards = !compacting;
            const bool right = outwards ? side > 0 : side < 0;
            chevron(canvas_, h.X(side * 37.0f), h.Y(0), h.S(5.0f), h.S(2.6f), right, firstLit ? lk.color : kLine);
            chevron(canvas_, h.X(side * 44.0f), h.Y(0), h.S(5.0f), h.S(2.6f), right, firstLit ? kLine : lk.color);
        }
    }

    h.chipAtTop(lk.label, lk.color, kPanel);

    // Speed dashes under it, one lit, running left to right
    const int lit = static_cast<int>(q * 5.0f) % 5;
    for (int i = 0; i < 5; i++){
        const float x = -23.5f + i * 10.0f, y = 40.0f;
        const uint16_t c = i == lit ? lk.color : kLine;
        canvas_.fillTriangle(ip(h.X(x + 2)), ip(h.Y(y - 1.5f)), ip(h.X(x + 9)), ip(h.Y(y - 1.5f)), ip(h.X(x + 7)), ip(h.Y(y + 1.5f)), c);
        canvas_.fillTriangle(ip(h.X(x + 2)), ip(h.Y(y - 1.5f)), ip(h.X(x + 7)), ip(h.Y(y + 1.5f)), ip(h.X(x)), ip(h.Y(y + 1.5f)), c);
    }
}

void Icons::mechPanel(Panel kind, float scale){
    const Hud h{ canvas_, cx_, cy_, unit_ };
    State st = State::Idle;
    const char* glyph = nullptr;
    switch (kind){
        case Panel::Warning:  st = State::WaitingUser; glyph = "!"; break;
        case Panel::Question: st = State::Question;    glyph = "?"; break;
        case Panel::Error:    st = State::Error;       glyph = "X"; break;
        case Panel::Done:     st = State::Idle;        break;
    }
    const Look lk = look(st);
    h.frame(kYellow);
    for (int i = 0; i < kSegments; i++) h.segment(i, lk.color);
    h.chipAtTop(lk.label, lk.color, kPanel);

    // The symbol, knocked out of a plate while the entry pulse flashes
    const bool flash = scale > 1.03f;
    if (flash) plate(canvas_, ip(h.X(-30)), ip(h.Y(-27)), ip(h.S(60)), ip(h.S(50)), ip(h.S(10)), lk.color);
    const uint16_t ink = flash ? kPanel : lk.color;
    if (glyph){
        drawText(canvas_, glyph, h.X(0), h.Y(18), h.S(40), ink, Align::Center, h.S(8));
    } else {
        h.line(-15, -3, -5, 8, 8, ink);
        h.line(-5, 8, 16, -15, 8, ink);
    }
    h.detail(lk.detail, kWhite);
}

void Icons::mechStandby(const Hourglass& hg){
    const Hud h{ canvas_, cx_, cy_, unit_ };
    const Look lk = look(State::Paused);
    h.frame(kYellow);

    // The ring drains with the sand; empty and blinking while the glass
    // would turn over, full again after it
    float left = 1.0f - hg.drained;
    bool shown = true;
    if (hg.flip > 0.0f){ left = 0.0f; shown = static_cast<int>(hg.flip * 6.0f) % 2 == 0; }
    else if (hg.settle < 1.0f) left = 1.0f;
    const int litN = ip(left * kSegments);
    for (int i = 0; i < kSegments; i++) h.segment(i, i < litN ? lk.color : kLine);
    h.chipAtTop(lk.label, lk.color, kPanel);

    if (shown){
        plate(canvas_, ip(h.X(-14)), ip(h.Y(-22)), ip(h.S(10)), ip(h.S(40)), ip(h.S(3.5f)), lk.color);
        plate(canvas_, ip(h.X(4)), ip(h.Y(-22)), ip(h.S(10)), ip(h.S(40)), ip(h.S(3.5f)), lk.color);
    }
    h.detail(lk.detail, kWhite);
}

void Icons::mechBlank(){
    // No session: the HUD with its lights out
    const Hud h{ canvas_, cx_, cy_, unit_ };
    h.frame(kLine);
    const uint16_t unlit = mix(kPanel, kLine, 0.5f);
    for (int i = 0; i < kSegments; i++) h.segment(i, unlit);
    h.reticle(kLine, kLine);
    h.chipAtTop(look(State::Off).label, kLine, kDim);
}

// ---------------- overlays ----------------

void Icons::mechRing(float fraction, int lap){
    // The work time on a thin track just outside the segments, clockwise
    // from the top: white (RUN's colour), then red-orange over it, then red
    static constexpr uint16_t kLap[] = { kWhite, kWarn, kRed };
    const Hud h{ canvas_, cx_, cy_, unit_ };
    h.arc(kTrackIn, kTrackOut, -90.0f, 270.0f, kLine);
    if (lap >= 3){ h.arc(kTrackIn, kTrackOut, -90.0f, 270.0f, kRed); return; }
    if (lap > 0) h.arc(kTrackIn, kTrackOut, -90.0f, 270.0f, kLap[lap - 1]);
    if (fraction > 0.003f) h.arc(kTrackIn, kTrackOut, -90.0f, -90.0f + 360.0f * fraction, kLap[lap]);
}

void Icons::mechDots(std::string_view codes){
    // A diamond lamp per session over the bottom of the ring
    const Hud h{ canvas_, cx_, cy_, unit_ };
    int n = static_cast<int>(codes.size());
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++){
        const int x = ip(h.X((i - (n - 1) * 0.5f) * 8.0f)), y = ip(h.Y(55.0f));
        diamond(canvas_, x, y, ip(h.S(5.2f)), kPanel);
        diamond(canvas_, x, y, ip(h.S(3.6f)), look(stateForCode(codes[i])).color);
    }
}

void Icons::mechLinkLost(){
    // A grey tag over the top of the ring: no heartbeat from the host
    const Hud h{ canvas_, cx_, cy_, unit_ };
    tag(canvas_, "NO LINK", ip(h.X(0)), ip(h.Y(-56.0f)), h.S(5.0f), kDim, Align::Center);
}

void Icons::mechToolMark(){
    // EXT on a cyan chip at the bottom right: a tool process runs
    const Hud h{ canvas_, cx_, cy_, unit_ };
    const float cap = h.S(6.5f);
    const int w = ip(textWidth("EXT", cap)) + 2 * std::max(4, ip(cap * 0.9f));
    const int ht = std::max(8, ip(cap * 2.0f));
    const int x = ip(h.X(46.0f)), y = ip(h.Y(52.0f));
    const int o = std::max(2, ip(h.S(1.5f)));
    plate(canvas_, x - w / 2 - o, y - ht / 2 - o, w + 2 * o, ht + 2 * o, ht / 3 + o, kPanel);
    chip(canvas_, "EXT", x, y, cap, kCyan, kPanel, Align::Center);
}

}  // namespace monitor
