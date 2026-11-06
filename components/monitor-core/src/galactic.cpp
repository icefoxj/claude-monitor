#include "monitor/galactic.h"

#include <cstdio>

namespace monitor::galactic {

Look look(State s){
    switch (s){
        case State::Processing:  return { kYellow, "PROCESSING", "PROCESSING", "" };
        case State::Compacting:  return { kSteel,  "COMPACTING", "COMPACTING", "" };
        case State::WaitingUser: return { kRed,    "CLEARANCE",  "CLEARANCE",  "REQUIRED" };
        case State::Question:    return { kBlue,   "QUERY",      "QUERY",      "AWAITING REPLY" };
        case State::Error:       return { kRed,    "FAILURE",    "FAILURE",    "SYSTEM MALFUNCTION" };
        case State::Paused:      return { kAmber,  "HOLD",       "HOLD",       "USAGE LIMIT" };
        case State::Idle:        return { kGreen,  "COMPLETE",   "COMPLETE",   "ALL CLEAR" };
        case State::Off:         return { kGrey,   "OFFLINE",    "OFFLINE",    "" };
    }
    return { kGrey, "", "", "" };
}

// ---------------- shapes ----------------

void brackets(LovyanGFX& g, int x, int y, int w, int h, int arm, int t, uint16_t color){
    if (arm > w / 2) arm = w / 2;
    if (arm > h / 2) arm = h / 2;
    const int r = x + w - t, b = y + h - t;
    g.fillRect(x, y, arm, t, color);      g.fillRect(x, y, t, arm, color);              // top left
    g.fillRect(x + w - arm, y, arm, t, color); g.fillRect(r, y, t, arm, color);         // top right
    g.fillRect(x, b, arm, t, color);      g.fillRect(x, y + h - arm, t, arm, color);    // bottom left
    g.fillRect(x + w - arm, b, arm, t, color); g.fillRect(r, y + h - arm, t, arm, color);   // bottom right
}

void circle(LovyanGFX& g, int cx, int cy, int r, int t, uint16_t color){
    for (int i = 0; i < t; i++) g.drawCircle(cx, cy, r - i, color);
}

int boxed(LovyanGFX& g, const char* s, int x, int y, Face face, int px, uint16_t color, bool right, int pad){
    const int w = textWidth(g, s, face, px) + 2 * pad;
    const int h = px + pad;
    const int left = right ? x - w : x;
    g.drawRect(left, y - h / 2, w, h, color);
    text(g, s, left + w / 2, y, face, px, color, textdatum_t::middle_center, kBg);
    return w;
}

// ---------------- a tile's corners and rows ----------------

void drawTile(LovyanGFX& g, const Tile& t){
    const int band = t.band;
    const int x0 = t.x + kTilePad, x1 = t.x + t.w - kTilePad;
    const int px = band * 56 / 100;

    // What was there goes, then the brackets over the corners
    g.fillRect(t.x, t.headY - band / 2, t.w, band, kBg);
    g.fillRect(t.x, t.footY - band / 2, t.w, band, kBg);
    brackets(g, t.x + 4, t.y + 4, t.w - 8, t.h - 8, band * 70 / 100, 3, t.frame);

    // Header: the name, the state boxed at the right, a scale under them
    int labelW = 0;
    if (t.label && t.label[0]){
        labelW = boxed(g, t.label, x1, t.headY, Face::Galactic, band * 44 / 100, t.color, true, band / 6) + 12;
    }
    const std::string name = fit(g, upper(t.name ? t.name : ""), Face::Galactic, px, x1 - x0 - labelW);
    text(g, name.c_str(), x0, t.headY, Face::Galactic, px, kWhite, textdatum_t::middle_left, kBg);
    const int lineY = t.headY + band / 2 - 2;
    g.drawFastHLine(x0, lineY, x1 - x0, kDim);
    for (int k = x0; k <= x1; k += 24) g.drawFastVLine(k, lineY - 3, 3, kDim);

    // Footer: a scale over it, the callsign, the clock
    const int lineF = t.footY - band / 2 + 1;
    g.drawFastHLine(x0, lineF, x1 - x0, kDim);
    for (int k = x0; k <= x1; k += 24) g.drawFastVLine(k, lineF + 1, 3, kDim);
    int rightW = 0;
    if (t.clock){
        rightW = text(g, t.clock, x1, t.footY, Face::Galactic, band * 60 / 100, t.color, textdatum_t::middle_right, kBg);
    } else if (t.note){
        rightW = text(g, t.note, x1, t.footY, Face::Galactic, band * 44 / 100, kGreen, textdatum_t::middle_right, kBg);
    }
    if (t.index){
        char call[16];
        snprintf(call, sizeof(call), "RED %s", t.index);
        const std::string c = fit(g, call, Face::Galactic, band * 44 / 100, x1 - x0 - rightW - 12);
        text(g, c.c_str(), x0, t.footY, Face::Galactic, band * 44 / 100, kAmber, textdatum_t::middle_left, kBg);
    }
}

}  // namespace monitor::galactic
