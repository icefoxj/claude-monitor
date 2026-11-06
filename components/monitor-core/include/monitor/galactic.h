#pragma once

#include <M5Unified.h>

#include <cstdint>
#include <string>

#include "monitor/eventlog.h"
#include "monitor/glyphs.h"
#include "monitor/protocol.h"
#include "monitor/usage.h"

// The galactic theme's look, in the manner of the vector targeting
// computers of 70s space-opera films: thin yellow lines on black, corner
// brackets instead of frames, reticles, a trench in perspective rushing at
// you, green readouts and red alarms, angular capitals. The helpers draw
// into any LovyanGFX target. The font (Oxanium, OFL) is only in builds with
// CONFIG_MONITOR_GALACTIC_THEME.

namespace monitor::galactic {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b){
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kBg     = rgb(0, 0, 0);
constexpr uint16_t kYellow = rgb(255, 214, 0);     // the vector lines, titles
constexpr uint16_t kAmber  = rgb(255, 150, 0);
constexpr uint16_t kGreen  = rgb(64, 255, 128);    // readouts
constexpr uint16_t kRed    = rgb(255, 52, 52);
constexpr uint16_t kBlue   = rgb(80, 180, 255);
constexpr uint16_t kWhite  = rgb(236, 240, 224);
constexpr uint16_t kSteel  = rgb(130, 150, 172);
constexpr uint16_t kDim    = rgb(84, 70, 0);       // grids, tracks, scales
constexpr uint16_t kGrey   = rgb(110, 110, 110);

using glyphs::Face;
using glyphs::fit;
using glyphs::fitSize;
using glyphs::shade;
using glyphs::text;
using glyphs::textWidth;
using glyphs::upper;

// How a state looks: its colour, the caption in the tile's header, and the
// word and the line under it in the icon
struct Look { uint16_t color; const char* label; const char* word; const char* detail; };
Look look(State s);

// Four corner brackets of `arm` px, `t` px thick, around the rectangle
void brackets(LovyanGFX& g, int x, int y, int w, int h, int arm, int t, uint16_t color);

// A circle `t` px thick
void circle(LovyanGFX& g, int cx, int cy, int r, int t, uint16_t color);

// A text in a thin box `pad` px around it; x is its right edge when `right`.
// Returns the box's width
int boxed(LovyanGFX& g, const char* s, int x, int y, Face face, int px, uint16_t color, bool right, int pad);

// A tile's corners and rows on the board's display: brackets at the four
// corners, the name and the state (boxed) in the header, a callsign and
// the clock in the footer, a thin scale under and over them. The board
// lays the rows out; the icon or the usage bars go between
struct Tile {
    int x = 0, y = 0, w = 0, h = 0;   // the whole tile
    int headY = 0, footY = 0;         // centre lines of the two rows
    int band = 40;                    // their height
    const char* index = nullptr;      // "01", the callsign's number
    const char* name = "";
    const char* label = "";           // header, right, boxed, in `color`
    uint16_t color = kYellow;         // the state's: label, clock
    uint16_t frame = kYellow;         // the brackets now (the board blinks them for a red state)
    const char* clock = nullptr;      // footer, right
    const char* note = nullptr;       // footer, right, when there is no clock
};
void drawTile(LovyanGFX& g, const Tile& t);

// Where things sit in a tile, for the board's layout
constexpr int kTilePad  = 14;   // the rows from the tile's sides
constexpr int kRowInset = 10;   // and from its top and bottom
constexpr int kRowGap   = 6;    // a row to the icon

// The views' galactic versions, called by the classic ones when the theme is on
void usageRows(M5Canvas& c, int x, int y, int w, int h, const Usage& usage,
               int32_t elapsedSec, bool detailed);
void usagePage(M5Canvas& c, const Usage& usage, int32_t elapsedSec);
void saverCard(M5Canvas& c, int x, int y, int w, int h, int pad, int header,
               const Usage& usage, int32_t elapsedSec, bool data);
void inspector(M5Canvas& c, const EventLog& log, const char* statusLine, int64_t nowMs);

}  // namespace monitor::galactic
