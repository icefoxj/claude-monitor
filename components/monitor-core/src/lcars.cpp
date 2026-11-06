#include "monitor/lcars.h"

#include <cmath>
#include <cstdio>

namespace monitor::lcars {

Look look(State s){
    switch (s){
        case State::Processing:  return { kBlue,   "PROCESSING",    "PROCESSING", "" };
        case State::Compacting:  return { kPeach,  "COMPACTING",    "COMPACTING", "" };
        case State::WaitingUser: return { kRed,    "AUTHORIZATION", "ALERT",      "AUTHORIZATION REQUIRED" };
        case State::Question:    return { kViolet, "QUERY",         "QUERY",      "INPUT REQUESTED" };
        case State::Error:       return { kRed,    "FAILURE",       "FAILURE",    "UNABLE TO COMPLY" };
        case State::Paused:      return { kGold,   "STANDBY",       "STANDBY",    "USAGE LIMIT" };
        case State::Idle:        return { kGreen,  "COMPLETE",      "COMPLETE",   "AWAITING INSTRUCTIONS" };
        case State::Off:         return { kGrey,   "OFFLINE",       "OFFLINE",    "" };
    }
    return { kGrey, "", "", "" };
}

// ---------------- shapes ----------------

void capsule(LovyanGFX& g, int x, int y, int w, int h, uint16_t color){
    if (w <= 0 || h <= 0) return;
    g.fillRoundRect(x, y, w, h, (w < h ? w : h) / 2, color);
}

void endCap(LovyanGFX& g, int x, int y, int w, int h, bool roundLeft, uint16_t color){
    if (w <= 0 || h <= 0) return;
    const int r = (w < h ? w : h) / 2;
    g.fillRoundRect(x, y, w, h, r, color);
    // Square off the other end
    if (roundLeft) g.fillRect(x + w - r, y, r, h, color);
    else           g.fillRect(x, y, r, h, color);
}

void elbow(LovyanGFX& g, int x, int y, int sideW, int sideH, int barW, int barH, bool top, uint16_t color){
    if (sideW <= 0 || sideH <= 0 || barH <= 0) return;
    if (barW < sideW) barW = sideW;
    int R = sideW * 3 / 4;                  // outer corner
    if (R > sideH / 2) R = sideH / 2;
    int r = barH * 2 / 3;                   // inner fillet
    if (r > barW - sideW) r = barW - sideW;
    if (r > sideH - barH) r = sideH - barH;

    const int barY = top ? y : y + sideH - barH;
    g.fillRect(x + R, barY, barW - R, barH, color);                       // the bar
    g.fillRect(x, top ? y + R : y, sideW, sideH - R, color);              // the side bar
    if (sideW > R) g.fillRect(x + R, top ? y : y + sideH - R, sideW - R, R, color);
    for (int dy = 0; dy < R; dy++){
        // The round outer corner, a row at a time
        const float yy = R - dy - 0.5f;
        const int hw = static_cast<int>(lroundf(sqrtf(static_cast<float>(R * R) - yy * yy)));
        g.drawFastHLine(x + R - hw, top ? y + dy : y + sideH - 1 - dy, hw, color);
    }
    for (int dy = 0; dy < r; dy++){
        // The fillet where the bar leaves the side bar
        const float yy = r - dy - 0.5f;
        const int w = static_cast<int>(lroundf(r - sqrtf(static_cast<float>(r * r) - yy * yy)));
        if (w > 0) g.drawFastHLine(x + sideW, top ? barY + barH + dy : barY - 1 - dy, w, color);
    }
}

// ---------------- a tile's frame and rows ----------------

namespace {

// A block number that stays with a name
unsigned codeFor(const char* s){
    unsigned h = 5381;
    for (; s && *s; ++s) h = h * 33 + static_cast<unsigned char>(*s);
    return h % 1000;
}

}  // namespace

void drawTile(LovyanGFX& g, const Tile& t){
    const int band = t.band;
    const int sw = sideWidth(band);
    const int bh = band * 45 / 100;
    const int gap = band / 8 > 3 ? band / 8 : 3;
    const int x0 = t.x + kTilePad, x1 = t.x + t.w - kTilePad;
    const int px = band * 66 / 100;

    // What was there goes: both rows across the tile, the side bar's column
    const int barTop = t.headY - bh / 2;
    const int barBot = t.footY + bh / 2;
    g.fillRect(t.x, t.headY - band / 2, t.w, band, kBg);
    g.fillRect(t.x, t.footY - band / 2, t.w, band, kBg);
    g.fillRect(x0, barTop, sw, barBot - barTop, kBg);

    // Header: the name at the right, the elbow's bar running up to it
    const std::string name = fit(g, upper(t.name ? t.name : ""), Face::Lcars, px, (x1 - x0 - sw) * 3 / 4);
    const int nameW = text(g, name.c_str(), x1, t.headY, Face::Lcars, px, kOrange, textdatum_t::middle_right, kBg);
    const int topEnd = t.headY + band;      // where the top elbow's side bar stops
    elbow(g, x0, barTop, sw, topEnd - barTop, x1 - nameW - 2 * gap - x0, bh, true, t.frame);

    // Footer: the clock in a capsule at the right, the state left of it,
    // the bottom elbow's bar running up to that
    int rightW = 0;
    if (t.clock){
        const int ch = band * 66 / 100;
        const int cpx = band * 58 / 100;
        const int cw = textWidth(g, t.clock, Face::Lcars, cpx) + ch * 3 / 4;
        endCap(g, x1 - cw, t.footY - ch / 2, cw, ch, false, t.color);
        text(g, t.clock, x1 - cw + ch / 4, t.footY, Face::Lcars, cpx, kBg, textdatum_t::middle_left, t.color);
        rightW = cw + 2 * gap;
    } else if (t.note){
        rightW = text(g, t.note, x1, t.footY, Face::Lcars, band * 48 / 100, kTan, textdatum_t::middle_right, kBg) + 2 * gap;
    }
    int labelW = 0;
    if (t.label && t.label[0]){
        const std::string lb = fit(g, t.label, Face::Lcars, px, x1 - rightW - x0 - sw - band);
        labelW = text(g, lb.c_str(), x1 - rightW, t.footY, Face::Lcars, px, t.color, textdatum_t::middle_right, kBg) + 2 * gap;
    }
    const int botStart = t.footY - band;    // where the bottom elbow's side bar starts
    elbow(g, x0, botStart, sw, barBot - botStart, x1 - rightW - labelW - x0, bh, false, t.frame);

    // The side bar between the elbows, cut into two numbered blocks
    const int midTop = topEnd + gap, midBot = botStart - gap;
    const int npx = band / 2;
    if (midBot - midTop > 3 * gap + 2 * npx){
        const int split = midTop + (midBot - midTop) * 3 / 5;
        g.fillRect(x0, midTop, sw, split - midTop, kPeriwinkle);
        g.fillRect(x0, split + gap, sw, midBot - split - gap, kTan);
        if (t.index){
            text(g, t.index, x0 + sw - 3, split - 2, Face::Lcars, fitSize(g, t.index, Face::Lcars, npx, sw - 6),
                 kBg, textdatum_t::bottom_right, kPeriwinkle);
        }
        char code[8];
        snprintf(code, sizeof(code), "%03u", codeFor(t.name));
        text(g, code, x0 + sw - 3, midBot - 2, Face::Lcars, fitSize(g, code, Face::Lcars, npx, sw - 6),
             kBg, textdatum_t::bottom_right, kTan);
    } else if (midBot > midTop){
        g.fillRect(x0, midTop, sw, midBot - midTop, kPeriwinkle);
    }
}

}  // namespace monitor::lcars
