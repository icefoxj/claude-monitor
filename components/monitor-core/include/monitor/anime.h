#pragma once

#include <M5Unified.h>

#include <cstdint>
#include <string>

#include "monitor/eventlog.h"
#include "monitor/protocol.h"
#include "monitor/usage.h"

// The anime theme's look, in the manner of the control-room screens of
// 90s mecha anime: an orange HUD on black, chamfered frames, hazard
// stripes, seven-segment timers, heavy squeezed mincho captions in Japanese
// over condensed English. The helpers draw into any LovyanGFX target, a
// canvas or the display. The fonts are built by tools/make-fonts.py and are
// only in builds with CONFIG_MONITOR_ANIME_THEME.

namespace monitor::anime {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b){
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kBg         = rgb(0, 0, 0);
constexpr uint16_t kOrange     = rgb(255, 128, 0);    // the theme's colour: lines, labels
constexpr uint16_t kOrangeDim  = rgb(140, 66, 0);     // frames at rest, scales
constexpr uint16_t kOrangeDark = rgb(52, 22, 0);      // unlit segments, grid lines
constexpr uint16_t kAmber      = rgb(255, 188, 40);
constexpr uint16_t kAmberDark  = rgb(60, 38, 0);
constexpr uint16_t kRed        = rgb(240, 24, 24);
constexpr uint16_t kRedDark    = rgb(88, 0, 0);
constexpr uint16_t kGreen      = rgb(56, 232, 112);
constexpr uint16_t kGreenDark  = rgb(0, 46, 20);
constexpr uint16_t kCyan       = rgb(48, 200, 255);
constexpr uint16_t kCyanDark   = rgb(0, 36, 60);
constexpr uint16_t kGrey       = rgb(150, 150, 160);
constexpr uint16_t kGreyDark   = rgb(40, 40, 48);
constexpr uint16_t kWhite      = rgb(255, 255, 255);
constexpr uint16_t kText       = rgb(244, 238, 226);  // warm white

// Faces. Cond: condensed extra-bold Latin; Mono: condensed monospace for
// data; Gothic: bold Japanese for small captions; Mincho: black, squeezed
// Japanese for titles (44 px, every caption in the sources); Kanji: the
// same face larger, but above 60 px it only has the icons' kanji
enum class Face { Cond, Mono, Gothic, Mincho, Kanji };

// Draws `s` with `face` at `px` pixels (the nearest size built, scaled)
// and returns its width. `bg` < 0 blends into what is already there;
// `tracking` adds that many px between letters.
int text(LovyanGFX& g, const char* s, int x, int y, Face face, int px, uint16_t fg,
         textdatum_t datum = textdatum_t::top_left, int32_t bg = -1, int tracking = 0);
int textWidth(LovyanGFX& g, const char* s, Face face, int px, int tracking = 0);

// `s` cut with "..." to fit `room` px
std::string fit(LovyanGFX& g, const std::string& s, Face face, int px, int room);

// Upper case, for the English labels
std::string upper(const std::string& s);

// `color` at `percent` of its brightness: the unlit segments behind a
// seven-segment readout are its colour at about 15 %
uint16_t shade(uint16_t color, int percent);

// How a state looks: its colour, a dark shade of it for fills, its
// Japanese and English captions
struct Look { uint16_t color; uint16_t dark; const char* jp; const char* en; };
Look look(State s);

// Rectangle with its top-left and bottom-right corners cut by `cut` px:
// the outline `t` px thick, or filled
void frame(LovyanGFX& g, int x, int y, int w, int h, int cut, int t, uint16_t color);
void fillFrame(LovyanGFX& g, int x, int y, int w, int h, int cut, uint16_t color);

// Diagonal stripes `stripe` px wide, `a` over `b`, filling the rectangle
void hazard(LovyanGFX& g, int x, int y, int w, int h, int stripe, uint16_t a, uint16_t b);

// Flat-topped hexagon of circumradius `r`, filled or as an outline `t` thick
void hexagon(LovyanGFX& g, float cx, float cy, float r, uint16_t color);
void hexagonOutline(LovyanGFX& g, float cx, float cy, float r, float t, uint16_t color);

// Filled label `h` px tall with a slanted right end; returns its width
int tag(LovyanGFX& g, int x, int y, int h, const char* s, Face face, int px, uint16_t fg, uint16_t bg);

// Seven-segment text (0-9, ':', '.', '-', ' ') `h` px tall from (x, y),
// unlit segments in `off` (pass the background for none); `right`: x is
// the right edge. Returns the width.
int sevenSeg(LovyanGFX& g, const char* s, int x, int y, int h, uint16_t on, uint16_t off, bool right = false);
int sevenSegWidth(const char* s, int h);

// A row of slanted blocks across (x, y, w, h), `fraction` of them lit
void blocks(LovyanGFX& g, int x, int y, int w, int h, float fraction, uint16_t on, uint16_t off);

// The same row cut into shares (any total), block i in the colour of the
// share its middle falls in
void splitBlocks(LovyanGFX& g, int x, int y, int w, int h, const float* shares, const uint16_t* colors,
                 int n, uint16_t off);

// The colours of the week-by-app split, in the account's order
uint16_t splitColor(int i);

// Green, amber or red for a limit's percentage, as in the classic theme
uint16_t levelColor(float percent, const std::string& severity);

// Dashed line, 2 px thick
void dashed(LovyanGFX& g, int x0, int y0, int x1, int y1, uint16_t color, int dash = 9, int gap = 7);

// A tile's two rows on the board's display, with no frame around them: a
// header (number tag, name, Japanese caption) and a footer (English
// caption, clock or note). The board lays the rows out; the icon or the
// usage bars go between
struct Tile {
    int x = 0, y = 0, w = 0, h = 0;   // the whole tile
    int headY = 0, footY = 0;         // centre lines of the two rows
    int band = 40;                    // their height
    const char* index = nullptr;      // "01" in the tag; nullptr: no tag
    const char* name = "";
    const char* jp = "";              // header, right, in `color`
    const char* en = "";              // footer, left, in `color`
    uint16_t color = kOrange;
    uint16_t tagColor = kOrange;      // the tag's fill (the board blinks it for a red state)
    const char* clock = nullptr;      // footer, right, seven segments
    const char* note = nullptr;       // footer, right, when there is no clock
};
void drawTile(LovyanGFX& g, const Tile& t);

// Where the rows sit in a tile, for the board's layout
constexpr int kRowInset = 10;   // from the tile's top and bottom
constexpr int kTilePad  = 12;   // from its sides (the usage bars too)
constexpr int kRowGap   = 6;    // between a row and the icon

// The views' anime versions, called by the classic ones when the theme is on
void usageRows(M5Canvas& c, int x, int y, int w, int h, const Usage& usage,
               int32_t elapsedSec, uint16_t bg, bool detailed);
void usagePage(M5Canvas& c, const Usage& usage, int32_t elapsedSec);
void saverCard(M5Canvas& c, int x, int y, int w, int h, int pad, int header,
               const Usage& usage, int32_t elapsedSec, bool data);
void inspector(M5Canvas& c, const EventLog& log, const char* statusLine, int64_t nowMs);

}  // namespace monitor::anime
