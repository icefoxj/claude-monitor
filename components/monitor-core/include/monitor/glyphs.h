#pragma once

#include <M5Unified.h>

#include <cstdint>
#include <string>

// Text in the themes' own fonts: smooth VLW faces built by
// tools/make-fonts.py, embedded only in builds that enable the theme they
// belong to (Kconfig), loaded on first use and shared by every canvas. The
// classic look does not use them.

namespace monitor::glyphs {

// Faces. Anime theme: Cond, condensed extra-bold Latin; Mono, condensed
// monospace for data; Gothic, bold Japanese for small captions; Mincho,
// black squeezed Japanese for titles (44 px, every caption in the sources);
// Kanji, the same larger, but above 60 px only the icons' kanji. LCARS
// theme: Lcars, tall condensed Latin; LcarsBig, the same larger, capitals,
// digits and a few signs only. Galactic theme: Galactic, angular Latin;
// GalacticBig, the same larger, capitals, digits and a few signs only
enum class Face { Cond, Mono, Gothic, Mincho, Kanji, Lcars, LcarsBig, Galactic, GalacticBig };

// Draws `s` with `face` at `px` pixels (the nearest size built, scaled)
// and returns its width. `bg` < 0 blends into what is already there;
// `tracking` adds that many px between letters. The text size is back to
// 1 afterwards (the classic code never sets it).
int text(LovyanGFX& g, const char* s, int x, int y, Face face, int px, uint16_t fg,
         textdatum_t datum = textdatum_t::top_left, int32_t bg = -1, int tracking = 0);
int textWidth(LovyanGFX& g, const char* s, Face face, int px, int tracking = 0);

// `s` cut with "..." to fit `room` px
std::string fit(LovyanGFX& g, const std::string& s, Face face, int px, int room);

// The largest size (at most `px`) at which `s` fits `room` px
int fitSize(LovyanGFX& g, const char* s, Face face, int px, int room);

// `s` wrapped into `width` px from (x, y), at most `maxLines` lines `lineH`
// apart, the last one ending in "..." when cut, breaking after a space when
// there is one in the second half of the line; returns the lines drawn.
// An empty `s` draws "(EMPTY)" in `emptyColor`.
int wrap(LovyanGFX& g, const std::string& s, int x, int y, int width, Face face, int px,
         int lineH, uint16_t color, int maxLines, uint16_t emptyColor);

// Upper case, for the English labels
std::string upper(const std::string& s);

// An RGB565 colour at `percent` of its brightness
uint16_t shade(uint16_t color, int percent);

}  // namespace monitor::glyphs
