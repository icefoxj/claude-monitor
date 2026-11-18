#include "monitor/mech.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace monitor::mech {

namespace {

inline int ip(float v){ return static_cast<int>(lroundf(v)); }

// Text on a coloured plate: dark, but grey on the dark grey of "off"
uint16_t inkOn(uint16_t fill){ return fill == kLine ? kDim : kPanel; }

int dashesWidth(int n, float s){
    if (n <= 0) return 0;
    const float h = std::max(2.0f, 3.0f * s);
    return ip(10.0f * s + (n - 1) * 9.0f * s + 4.0f * s - 3.0f * s + h * 0.67f);
}

}  // namespace

uint16_t mix(uint16_t a, uint16_t b, float t){
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int r = ar + ip((br - ar) * t), g = ag + ip((bg - ag) * t), bl = ab + ip((bb - ab) * t);
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

Look look(State s){
    switch (s){
        // White, not the yellow livery: yellow was too close to the amber
        // HOLD had then (the livery was orange up to 1.6.2)
        case State::Processing:  return { kWhite,  "RUN",     "AT WORK" };
        case State::Compacting:  return { kDim,    "COMPACT", "CONTEXT" };
        case State::WaitingUser: return { kRed,    "APPROVE", "OPERATOR INPUT" };
        case State::Question:    return { kCyan,   "QUERY",   "ANSWER REQUIRED" };
        case State::Error:       return { kRed,    "FAULT",   "SEE TERMINAL" };
        case State::Paused:      return { kWarn,   "HOLD",    "USAGE LIMIT" };
        case State::Idle:        return { kGreen,  "READY",   "TURN COMPLETE" };
        case State::Off:         return { kLine,   "OFFLINE", "" };
    }
    return { kLine, "", "" };
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

// ---------------- shapes ----------------

void plate(LovyanGFX& g, int x, int y, int w, int h, int cut, uint16_t fill, int corners){
    if (w <= 0 || h <= 0) return;
    const int c = std::clamp(cut, 0, std::min(w, h) / 2);
    g.fillRect(x, y + c, w, h - 2 * c, fill);
    for (int i = 0; i < c; i++){
        // Row i from the top and from the bottom: the cut takes c - i px
        const int k = c - i;
        int l = (corners & kTL) ? k : 0, r = (corners & kTR) ? k : 0;
        g.drawFastHLine(x + l, y + i, w - l - r, fill);
        l = (corners & kBL) ? k : 0;
        r = (corners & kBR) ? k : 0;
        g.drawFastHLine(x + l, y + h - 1 - i, w - l - r, fill);
    }
}

void edge(LovyanGFX& g, int x, int y, int w, int h, int cut, uint16_t color, int corners, int t){
    for (int i = 0; i < t; i++){
        const int xx = x + i, yy = y + i, ww = w - 2 * i, hh = h - 2 * i;
        if (ww <= 2 || hh <= 2) break;
        const int c = std::clamp(cut - (i * 41) / 100, 0, std::min(ww, hh) / 2);
        const int tl = (corners & kTL) ? c : 0, tr = (corners & kTR) ? c : 0;
        const int br = (corners & kBR) ? c : 0, bl = (corners & kBL) ? c : 0;
        const int r = xx + ww - 1, b = yy + hh - 1;
        g.drawFastHLine(xx + tl, yy, ww - tl - tr, color);
        g.drawFastHLine(xx + bl, b, ww - bl - br, color);
        g.drawFastVLine(xx, yy + tl, hh - tl - bl, color);
        g.drawFastVLine(r, yy + tr, hh - tr - br, color);
        if (tl) g.drawLine(xx, yy + tl, xx + tl, yy, color);
        if (tr) g.drawLine(r - tr, yy, r, yy + tr, color);
        if (br) g.drawLine(r, b - br, r - br, b, color);
        if (bl) g.drawLine(xx + bl, b, xx, b - bl, color);
    }
}

void hazard(LovyanGFX& g, int x, int y, int w, int h, uint16_t color, int pitch){
    if (w <= 0 || h <= 0 || pitch < 2) return;
    int32_t cx, cy, cw, ch;
    g.getClipRect(&cx, &cy, &cw, &ch);
    const int x0 = std::max<int>(x, cx), y0 = std::max<int>(y, cy);
    const int x1 = std::min<int>(x + w, cx + cw), y1 = std::min<int>(y + h, cy + ch);
    if (x1 <= x0 || y1 <= y0) return;
    g.setClipRect(x0, y0, x1 - x0, y1 - y0);
    const int half = pitch / 2;
    for (int i = -h; i < w; i += pitch){
        g.fillTriangle(x + i, y + h, x + i + h, y, x + i + h + half, y, color);
        g.fillTriangle(x + i, y + h, x + i + half, y + h, x + i + h + half, y, color);
    }
    g.setClipRect(cx, cy, cw, ch);
}

void brackets(LovyanGFX& g, int x, int y, int w, int h, int arm, int t, uint16_t color){
    if (arm > w / 2) arm = w / 2;
    if (arm > h / 2) arm = h / 2;
    const int r = x + w - t, b = y + h - t;
    g.fillRect(x, y, arm, t, color);           g.fillRect(x, y, t, arm, color);
    g.fillRect(x + w - arm, y, arm, t, color); g.fillRect(r, y, t, arm, color);
    g.fillRect(x, b, arm, t, color);           g.fillRect(x, y + h - arm, t, arm, color);
    g.fillRect(x + w - arm, b, arm, t, color); g.fillRect(r, y + h - arm, t, arm, color);
}

int dashes(LovyanGFX& g, int x, int cy, int n, float s, uint16_t lead, uint16_t rest){
    if (n <= 0) return 0;
    const float h = std::max(2.0f, 3.0f * s);
    const float sl = h * 0.67f;
    const float top = cy - h * 0.5f, bot = top + h - 1.0f;
    float xx = static_cast<float>(x);
    for (int i = 0; i < n; i++){
        const float w = (i == 0 ? 10.0f : 6.0f) * s;
        const uint16_t c = i == 0 ? lead : rest;
        g.fillTriangle(ip(xx + sl), ip(top), ip(xx + sl + w), ip(top), ip(xx + w), ip(bot), c);
        g.fillTriangle(ip(xx + sl), ip(top), ip(xx + w), ip(bot), ip(xx), ip(bot), c);
        xx += w + (i == 0 ? 4.0f : 3.0f) * s;
    }
    return dashesWidth(n, s);
}

void chevron(LovyanGFX& g, float cx, float cy, float size, float t, bool right, uint16_t color){
    const float d = right ? 1.0f : -1.0f;
    const float back = cx - d * size * 0.4f, tip = cx + d * size * 0.4f;
    g.drawWideLine(ip(back), ip(cy - size), ip(tip), ip(cy), t * 0.5f, color);
    g.drawWideLine(ip(tip), ip(cy), ip(back), ip(cy + size), t * 0.5f, color);
}

void diamond(LovyanGFX& g, int cx, int cy, int r, uint16_t color){
    g.fillTriangle(cx, cy - r, cx + r, cy, cx - r, cy, color);
    g.fillTriangle(cx, cy + r, cx + r, cy, cx - r, cy, color);
}

int chip(LovyanGFX& g, const char* s, int x, int cy, float cap, uint16_t fill, uint16_t ink, Align align){
    const int tw = ip(textWidth(s, cap));
    const int padX = std::max(4, ip(cap * 0.9f));
    const int h = std::max(8, ip(cap * 2.0f));
    const int w = tw + 2 * padX;
    const int left = align == Align::Right ? x - w : (align == Align::Center ? x - w / 2 : x);
    plate(g, left, cy - h / 2, w, h, h / 3, fill);
    drawText(g, s, static_cast<float>(left + padX), cy + cap * 0.5f, cap, ink);
    return w;
}

int tag(LovyanGFX& g, const char* s, int x, int cy, float cap, uint16_t color, Align align){
    const int tw = ip(textWidth(s, cap));
    const int padX = std::max(4, ip(cap * 0.9f));
    const int h = std::max(8, ip(cap * 2.0f));
    const int w = tw + 2 * padX;
    const int left = align == Align::Right ? x - w : (align == Align::Center ? x - w / 2 : x);
    plate(g, left, cy - h / 2, w, h, h / 3, kPanelHi);
    edge(g, left, cy - h / 2, w, h, h / 3, color);
    drawText(g, s, static_cast<float>(left + padX), cy + cap * 0.5f, cap, color);
    return w;
}

int titleBand(LovyanGFX& g, const char* code, int x, int y, int h){
    const float cap = h * 0.56f;
    const float weight = cap * 0.19f;
    const int pad = std::max(4, h * 3 / 8);
    const int end = x + pad + ip(textWidth(code, cap, weight)) + pad / 2;
    g.fillRect(x, y, end - x, h, kYellow);
    g.fillTriangle(end, y, end + h, y, end, y + h - 1, kYellow);   // the 45° end, longer at the top
    drawText(g, code, static_cast<float>(x + pad), y + h * 0.5f + cap * 0.5f, cap, kBg, Align::Left, weight);
    // Two thin bars after it, parallel to the cut
    const int gap = std::max(3, h / 8);
    int bx = end + h + gap;
    for (int k = 0; k < 2; k++){
        const int bw = k == 0 ? std::max(3, h / 7) : std::max(2, h / 12);
        g.fillTriangle(bx, y, bx + bw, y, bx - h + bw, y + h - 1, kYellow);
        g.fillTriangle(bx, y, bx - h + bw, y + h - 1, bx - h, y + h - 1, kYellow);
        bx += bw + gap;
    }
    return bx + std::max(6, h / 4);
}

void tab(LovyanGFX& g, int x, int y, int cut, int s, int bottom, uint16_t color){
    g.fillTriangle(x, y + cut, x + cut, y, x + cut + s, y, color);
    g.fillTriangle(x, y + cut, x + cut + s, y, x + s, y + cut + s, color);
    if (bottom > y + cut) g.fillRect(x, y + cut, s, bottom - (y + cut), color);
}

int panel(LovyanGFX& g, int x, int y, int w, int h, const char* title, const char* sub, uint16_t accent, float cap){
    const int cut = std::max(8, ip(cap * 0.9f));
    const int headH = ip(cap * 2.4f);
    plate(g, x, y, w, h, cut, kPanel);
    edge(g, x, y, w, h, cut, kLine);
    const int s = std::max(3, ip(cap * 0.3f));
    tab(g, x + 1, y + 1, cut, s, y + headH - 4, accent);
    const float base = y + headH * 0.5f + cap * 0.5f;
    int cx = x + s + std::max(10, ip(cap));
    if (title && title[0]) cx += drawText(g, title, static_cast<float>(cx), base, cap, kWhite) + ip(cap * 0.9f);
    if (sub && sub[0]) cx += drawText(g, sub, static_cast<float>(cx), base, cap * 0.72f, kDim) + ip(cap * 0.9f);
    const float ds = cap / 12.0f;
    const int dw = dashesWidth(4, ds);
    const int dx = x + w - ip(cap) - dw;
    if (dx - 10 > cx){
        g.drawFastHLine(cx, y + headH / 2, dx - 10 - cx, kLine);
        dashes(g, dx, y + headH / 2, 4, ds, accent, kLine);
    }
    return y + headH;
}

// ---------------- a tile ----------------

void drawTile(LovyanGFX& g, const Tile& t){
    const int band = t.band;
    const int px = t.x + 4, py = t.y + 4, pw = t.w - 8, ph = t.h - 8;
    const int cut = std::max(10, band / 2);
    plate(g, px, py, pw, ph, cut, kPanel);
    edge(g, px, py, pw, ph, cut, t.frame, kTLBR, band >= 48 ? 2 : 1);
    const int s = std::max(3, band / 10);
    tab(g, px + 1, py + 1, cut, s, t.headY + band / 2 - 4, kYellow);

    // Header: the name, a rule (hazard stripes for a red state), the dashes,
    // the state's chip
    const float cap = band * 0.36f;
    const int x0 = px + s + std::max(8, band / 4);
    const int x1 = px + pw - std::max(8, band / 4);
    int right = x1;
    if (t.label && t.label[0]){
        right -= chip(g, t.label, x1, t.headY, cap * 0.8f, t.color, inkOn(t.color), Align::Right) + band / 4;
    }
    const float ds = band / 26.0f;
    const int dw = dashesWidth(4, ds);
    const bool withDashes = right - x0 >= dw + band * 4;
    const int dx = right - dw;
    const int nameRoom = (withDashes ? dx - band / 2 : right - band / 4) - x0;
    const std::string name = fitText(upper(t.name ? t.name : ""), cap, static_cast<float>(nameRoom));
    const int nw = drawText(g, name.c_str(), static_cast<float>(x0), t.headY + cap * 0.5f, cap, kWhite);
    const int rx0 = x0 + nw + band / 3;
    const int rx1 = withDashes ? dx - band / 4 : right - band / 4;
    if (rx1 > rx0){
        if (t.alarm){
            const int hh = std::max(4, band / 5);
            hazard(g, rx0, t.headY - hh / 2, rx1 - rx0, hh, t.frame, std::max(6, band / 4));
        } else {
            g.drawFastHLine(rx0, t.headY, rx1 - rx0, kLine);
        }
    }
    if (withDashes) dashes(g, dx, t.headY, 4, ds, t.color, kLine);

    // Footer: the tile's number on a small title band, dashes, the clock
    int fx = px + std::max(8, band / 4);
    if (t.index && t.index[0]){
        const int hb = std::max(12, band * 56 / 100);
        fx = titleBand(g, t.index, fx, t.footY - hb / 2, hb);
    }
    int rightW = 0;
    if (t.clock){
        const float capC = band * 0.42f;
        rightW = drawText(g, t.clock, static_cast<float>(x1), t.footY + capC * 0.5f, capC, t.color, Align::Right);
        const float capL = band * 0.22f;
        rightW += band / 5 + drawText(g, "TIME", static_cast<float>(x1 - rightW - band / 5), t.footY + capC * 0.5f,
                                      capL, kDim, Align::Right);
    } else if (t.note){
        const float capN = band * 0.28f;
        rightW = drawText(g, t.note, static_cast<float>(x1), t.footY + capN * 0.5f, capN, kDim, Align::Right);
    }
    const int fdw = dashesWidth(5, ds);
    if (x1 - rightW - band / 2 - fx >= fdw) dashes(g, fx, t.footY, 5, ds, t.color, kLine);
}

}  // namespace monitor::mech
