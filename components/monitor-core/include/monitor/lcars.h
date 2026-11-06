#pragma once

#include <M5Unified.h>

#include <cstdint>
#include <string>

#include "monitor/eventlog.h"
#include "monitor/glyphs.h"
#include "monitor/protocol.h"
#include "monitor/usage.h"

// The LCARS theme's look, in the manner of the consoles of 90s TV
// starships: pastel bars on black that turn corners in rounded "elbows",
// capsule buttons with black text in them, tall condensed capitals and
// block numbers. The helpers draw into any LovyanGFX target. The fonts
// (Antonio, OFL) are only in builds with CONFIG_MONITOR_LCARS_THEME.

namespace monitor::lcars {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b){
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kBg         = rgb(0, 0, 0);
constexpr uint16_t kOrange     = rgb(255, 153, 0);     // titles, the theme's own colour
constexpr uint16_t kPeach      = rgb(255, 153, 102);
constexpr uint16_t kTan        = rgb(255, 204, 153);   // text on black
constexpr uint16_t kGold       = rgb(255, 204, 102);
constexpr uint16_t kLilac      = rgb(204, 153, 204);
constexpr uint16_t kViolet     = rgb(153, 153, 255);
constexpr uint16_t kPeriwinkle = rgb(153, 153, 204);
constexpr uint16_t kBlue       = rgb(153, 204, 255);
constexpr uint16_t kRed        = rgb(255, 80, 72);
constexpr uint16_t kGreen      = rgb(136, 221, 136);
constexpr uint16_t kGrey       = rgb(140, 140, 152);
constexpr uint16_t kDim        = rgb(44, 44, 84);      // unlit bars and tracks

using glyphs::Face;
using glyphs::fit;
using glyphs::fitSize;
using glyphs::shade;
using glyphs::text;
using glyphs::textWidth;
using glyphs::upper;

// How a state looks: its colour, the caption in the tile's footer, and the
// word and the line under it in the icon's capsule
struct Look { uint16_t color; const char* label; const char* word; const char* detail; };
Look look(State s);

// A bar with both ends round (a capsule), or only its left or right end
void capsule(LovyanGFX& g, int x, int y, int w, int h, uint16_t color);
void endCap(LovyanGFX& g, int x, int y, int w, int h, bool roundLeft, uint16_t color);

// An elbow: a side bar `sideW` wide and `sideH` tall meeting a bar `barH`
// tall that runs to `barW` from the elbow's left edge, the outer corner
// round and the inner one filleted. `top`: the bar runs along the top
// (the corner is top-left), else along the bottom (bottom-left)
void elbow(LovyanGFX& g, int x, int y, int sideW, int sideH, int barW, int barH, bool top, uint16_t color);

// A tile's frame and rows on the board's display: an elbow at the top with
// the name after its bar, a side bar cut into numbered blocks, an elbow at
// the bottom with the state and a capsule holding the clock. The board
// lays the rows out; the icon or the usage bars go right of the side bar
struct Tile {
    int x = 0, y = 0, w = 0, h = 0;   // the whole tile
    int headY = 0, footY = 0;         // centre lines of the two rows
    int band = 40;                    // their height
    const char* index = nullptr;      // "01" in the side bar; nullptr: none
    const char* name = "";
    const char* label = "";           // footer, in `color`
    uint16_t color = kOrange;         // the state's: label, clock capsule
    uint16_t frame = kOrange;         // the elbows now (the board blinks it for a red state)
    const char* clock = nullptr;      // footer, right, in a capsule
    const char* note = nullptr;       // footer, right, when there is no clock
};
void drawTile(LovyanGFX& g, const Tile& t);

// Where things sit in a tile, for the board's layout
constexpr int kTilePad  = 10;   // from the tile's sides
constexpr int kRowInset = 10;   // the rows from its top and bottom
constexpr int kRowGap   = 6;    // a row or the side bar to the icon
inline int sideWidth(int band){ return band; }

// The views' LCARS versions, called by the classic ones when the theme is on
void usageRows(M5Canvas& c, int x, int y, int w, int h, const Usage& usage,
               int32_t elapsedSec, bool detailed);
void usagePage(M5Canvas& c, const Usage& usage, int32_t elapsedSec);
void saverCard(M5Canvas& c, int x, int y, int w, int h, int pad, int header,
               const Usage& usage, int32_t elapsedSec, bool data);
void inspector(M5Canvas& c, const EventLog& log, const char* statusLine, int64_t nowMs);

}  // namespace monitor::lcars
