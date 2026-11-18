#pragma once

#include <M5Unified.h>

#include <cstdint>
#include <string>

#include "monitor/eventlog.h"
#include "monitor/glyphs.h"
#include "monitor/mechfont.h"
#include "monitor/protocol.h"
#include "monitor/usage.h"

// The mech theme: the house look of the other projects' Windows apps (the
// "Studio" apps), their CoreS3/AtomS3R HUDs and the printed liveries, on
// black: panels outlined with 45° cuts, an orange tab on each panel's
// corner, a title band with the unit code knocked out of orange, hazard
// stripes, speed dashes, corner brackets, reticles, diamond lamps and the
// chamfered stroke font (mechfont.h), upper case only. Orange is the
// livery; cyan, green, amber and red are states. Everything is procedural,
// so the theme needs no font files: CONFIG_MONITOR_MECH_THEME only makes it
// selectable. Values that are not words (hook data) use the anime theme's
// monospace face when the build has it (glyphs.h), DejaVu otherwise.

namespace monitor::mech {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b){
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// The house palette (the Studio apps' Theme class, the HUDs' constants),
// but for the ground: black like the other themes, where the apps have a
// graphite screen (#15171A) and grey panels (#1F2328)
constexpr uint16_t kBg      = rgb(0, 0, 0);            // behind everything
constexpr uint16_t kPanel   = rgb(0, 0, 0);            // inside a panel, the icons' ground
constexpr uint16_t kPanelHi = rgb(0x27, 0x2C, 0x32);   // a plate on a panel
constexpr uint16_t kField   = rgb(0x11, 0x13, 0x16);   // tracks, fields
constexpr uint16_t kLine    = rgb(0x3A, 0x40, 0x48);   // edges, rules, unlit segments
constexpr uint16_t kDim     = rgb(0x8A, 0x92, 0x9C);   // labels
constexpr uint16_t kWhite   = rgb(0xF2, 0xF2, 0xF2);
constexpr uint16_t kOrange  = rgb(0xFF, 0x7A, 0x1A);   // the livery
constexpr uint16_t kCyan    = rgb(0x3F, 0xC8, 0xFF);
constexpr uint16_t kGreen   = rgb(0x38, 0xD4, 0x7A);
constexpr uint16_t kRed     = rgb(0xFF, 0x40, 0x40);
constexpr uint16_t kAmber   = rgb(0xFF, 0xC2, 0x3A);
constexpr uint16_t kViolet  = rgb(0xB4, 0x8C, 0xFF);

// The unit code on the title bands, as EG-01, HM-01 and the others carry theirs
constexpr const char* kUnitCode = "CM-01";

using glyphs::Face;
using glyphs::shade;
using glyphs::upper;

// `a` and `b` mixed, `t` (0..1) of the way to `b`
uint16_t mix(uint16_t a, uint16_t b, float t);

// How a state looks: its colour, the word on its chip (tile header, icon)
// and the line under the icon
struct Look { uint16_t color; const char* label; const char* detail; };
Look look(State s);
State stateForCode(char code);   // a "sessions" letter

// ---------------- shapes ----------------

// Which corners get a 45° cut; the house default is top left and bottom right
constexpr int kTL = 1, kTR = 2, kBR = 4, kBL = 8, kTLBR = kTL | kBR, kAllCorners = 15;

// A filled rectangle with 45° cuts of `cut` px
void plate(LovyanGFX& g, int x, int y, int w, int h, int cut, uint16_t fill, int corners = kTLBR);
// Its outline, `t` px thick (inwards)
void edge(LovyanGFX& g, int x, int y, int w, int h, int cut, uint16_t color, int corners = kTLBR, int t = 1);

// Diagonal stripes ("/") in the rectangle, half of every `pitch` px
void hazard(LovyanGFX& g, int x, int y, int w, int h, uint16_t color, int pitch);

// Four corner brackets of `arm` px, `t` px thick
void brackets(LovyanGFX& g, int x, int y, int w, int h, int arm, int t, uint16_t color);

// A row of slanted speed dashes from x, centred on cy, `s` px per house px
// (a long first one in `lead`, the others in `rest`); returns the width
int dashes(LovyanGFX& g, int x, int cy, int n, float s, uint16_t lead, uint16_t rest);

// A chevron (">" when `right`, "<" otherwise), `size` px from tip to back
void chevron(LovyanGFX& g, float cx, float cy, float size, float t, bool right, uint16_t color);

// A diamond lamp of radius r
void diamond(LovyanGFX& g, int cx, int cy, int r, uint16_t color);

// A chamfered plate in `fill` with `s` knocked out of it in `ink`, centred
// on cy; x is its left edge, centre or right edge by `align`. Returns its width
int chip(LovyanGFX& g, const char* s, int x, int cy, float cap, uint16_t fill, uint16_t ink, Align align);
// The same as an outline in `color` on kPanelHi
int tag(LovyanGFX& g, const char* s, int x, int cy, float cap, uint16_t color, Align align);

// The title band: `code` knocked out of an orange band from (x, y), `h`
// tall, its end cut at 45°, then two thin slanted bars. Returns where
// the next thing can start
int titleBand(LovyanGFX& g, const char* code, int x, int y, int h);

// The house panel: a chamfered plate with an outline, the orange tab on
// its cut corner, the title in the stroke font, an optional subtitle, a
// rule and speed dashes. Returns the y where its content starts
int panel(LovyanGFX& g, int x, int y, int w, int h, const char* title, const char* sub = nullptr,
          uint16_t accent = kOrange, float cap = 13.0f);

// The stripe along a panel's cut corner and down its side to `bottom`
void tab(LovyanGFX& g, int x, int y, int cut, int s, int bottom, uint16_t color);

// ---------------- a tile ----------------

// A tile on the board's display: a panel the size of the tile, the tab on
// its corner, the session's name with the state's chip in the header, the
// tile's number on a small title band and the clock in the footer. The
// icon sits on the panel between the rows (its ground is kPanel too)
struct Tile {
    int x = 0, y = 0, w = 0, h = 0;   // the whole tile
    int headY = 0, footY = 0;         // centre lines of the two rows
    int band = 40;                    // their height
    const char* index = nullptr;      // "01", on the footer's band; none without
    const char* name = "";
    const char* label = "";           // the chip, in `color`
    uint16_t color = kOrange;         // the state's: chip, clock, first dash
    uint16_t frame = kLine;           // the panel's outline now (the board blinks it for a red state)
    bool alarm = false;               // hazard stripes in the header, in `frame`
    const char* clock = nullptr;      // footer, right
    const char* note = nullptr;       // footer, right, when there is no clock
};
void drawTile(LovyanGFX& g, const Tile& t);

// Where things sit in a tile, for the board's layout
constexpr int kTilePad  = 18;   // the rows from the tile's sides
constexpr int kRowInset = 10;   // and from its top and bottom
constexpr int kRowGap   = 4;    // a row to the icon

// ---------------- the views ----------------

// Called by the classic ones when the theme is on
void usageRows(M5Canvas& c, int x, int y, int w, int h, const Usage& usage,
               int32_t elapsedSec, bool detailed);
void usagePage(M5Canvas& c, const Usage& usage, int32_t elapsedSec);
void saverCard(M5Canvas& c, int x, int y, int w, int h, int pad, int header,
               const Usage& usage, int32_t elapsedSec, bool data);
void inspector(M5Canvas& c, const EventLog& log, const char* statusLine, int64_t nowMs);

}  // namespace monitor::mech
