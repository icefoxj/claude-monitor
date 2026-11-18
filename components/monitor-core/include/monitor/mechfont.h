#pragma once

#include <M5Unified.h>

#include <cstdint>
#include <string>

// The house mech stroke font: the chamfered, stroked capitals on the
// liveries and HUDs of the other projects (the same glyphs as the WP-01,
// EG-01 and HM-01 firmware, the Studio apps' MechText and the CAD's
// mech.py), drawn with anti-aliased wide lines into any LovyanGFX target.
// No font files: each glyph is a few polylines on a 6-unit cap grid.
//
// Upper case, digits, - _ / . : < > + % = # ( ) [ ] , ' " ! ? $ and the
// degree sign and middle dot; lower case is drawn as upper case, accented
// Latin-1 letters as their base letter, anything else as a space. Input is
// UTF-8.

namespace monitor::mech {

enum class Align { Left, Center, Right };

// The house stroke for a cap height: 15 % of it, at least 1.2 px
float strokeWeight(float cap);

// `cap`: the capitals' height in px; `weight`: the stroke in px, 0 for
// strokeWeight(cap)
float textWidth(const char* s, float cap, float weight = 0.0f);

// Draws `s` standing on `baseline` (the capitals fill baseline - cap to
// baseline) and returns its width
int drawText(LovyanGFX& g, const char* s, float x, float baseline, float cap, uint16_t color,
             Align align = Align::Left, float weight = 0.0f);

// `s` cut with "..." to fit `room` px
std::string fitText(const std::string& s, float cap, float room, float weight = 0.0f);

// The largest cap height, at most `cap` (and not under 5 px), at which `s` fits `room` px
float fitCap(const char* s, float cap, float room);

}  // namespace monitor::mech
