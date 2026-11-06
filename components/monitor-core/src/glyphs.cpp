#include "monitor/glyphs.h"

#include <sdkconfig.h>

namespace monitor::glyphs {

namespace {

struct Built {
    Face face;
    int px;
    const uint8_t* start;
    const uint8_t* end;
    lgfx::PointerWrapper data{};
    lgfx::VLWfont vlw{};
    bool tried = false;
    bool ok = false;
};

#define MONITOR_FONT(name) \
    extern const uint8_t name##_start[] asm("_binary_" #name "_vlw_start"); \
    extern const uint8_t name##_end[]   asm("_binary_" #name "_vlw_end");

#if defined(CONFIG_MONITOR_ANIME_THEME)
MONITOR_FONT(cond18)
MONITOR_FONT(cond22)
MONITOR_FONT(cond26)
MONITOR_FONT(cond32)
MONITOR_FONT(cond40)
MONITOR_FONT(cond56)
MONITOR_FONT(mono14)
MONITOR_FONT(mono16)
MONITOR_FONT(mono20)
MONITOR_FONT(gothic22)
MONITOR_FONT(gothic32)
MONITOR_FONT(mincho44)
MONITOR_FONT(mincho96)
MONITOR_FONT(mincho192)
#endif
#if defined(CONFIG_MONITOR_LCARS_THEME)
MONITOR_FONT(lcars16)
MONITOR_FONT(lcars20)
MONITOR_FONT(lcars26)
MONITOR_FONT(lcars34)
MONITOR_FONT(lcars44)
MONITOR_FONT(lcars60)
MONITOR_FONT(lcars90)
MONITOR_FONT(lcars140)
#endif
#if defined(CONFIG_MONITOR_GALACTIC_THEME)
MONITOR_FONT(gal16)
MONITOR_FONT(gal20)
MONITOR_FONT(gal26)
MONITOR_FONT(gal34)
MONITOR_FONT(gal44)
MONITOR_FONT(gal60)
MONITOR_FONT(gal90)
#endif
#undef MONITOR_FONT

// Smallest first within a face. The first entry only keeps the array from
// being empty in a build with no theme fonts
Built kBuilt[] = {
    { Face::Cond, 0, nullptr, nullptr },
#if defined(CONFIG_MONITOR_ANIME_THEME)
    { Face::Cond,     18,  cond18_start,    cond18_end },
    { Face::Cond,     22,  cond22_start,    cond22_end },
    { Face::Cond,     26,  cond26_start,    cond26_end },
    { Face::Cond,     32,  cond32_start,    cond32_end },
    { Face::Cond,     40,  cond40_start,    cond40_end },
    { Face::Cond,     56,  cond56_start,    cond56_end },
    { Face::Mono,     14,  mono14_start,    mono14_end },
    { Face::Mono,     16,  mono16_start,    mono16_end },
    { Face::Mono,     20,  mono20_start,    mono20_end },
    { Face::Gothic,   22,  gothic22_start,  gothic22_end },
    { Face::Gothic,   32,  gothic32_start,  gothic32_end },
    { Face::Mincho,   44,  mincho44_start,  mincho44_end },
    { Face::Kanji,    44,  mincho44_start,  mincho44_end },
    { Face::Kanji,    96,  mincho96_start,  mincho96_end },
    { Face::Kanji,    192, mincho192_start, mincho192_end },
#endif
#if defined(CONFIG_MONITOR_LCARS_THEME)
    { Face::Lcars,    16,  lcars16_start,   lcars16_end },
    { Face::Lcars,    20,  lcars20_start,   lcars20_end },
    { Face::Lcars,    26,  lcars26_start,   lcars26_end },
    { Face::Lcars,    34,  lcars34_start,   lcars34_end },
    { Face::Lcars,    44,  lcars44_start,   lcars44_end },
    { Face::Lcars,    60,  lcars60_start,   lcars60_end },
    { Face::LcarsBig, 60,  lcars60_start,   lcars60_end },
    { Face::LcarsBig, 90,  lcars90_start,   lcars90_end },
    { Face::LcarsBig, 140, lcars140_start,  lcars140_end },
#endif
#if defined(CONFIG_MONITOR_GALACTIC_THEME)
    { Face::Galactic,    16, gal16_start, gal16_end },
    { Face::Galactic,    20, gal20_start, gal20_end },
    { Face::Galactic,    26, gal26_start, gal26_end },
    { Face::Galactic,    34, gal34_start, gal34_end },
    { Face::Galactic,    44, gal44_start, gal44_end },
    { Face::Galactic,    60, gal60_start, gal60_end },
    { Face::GalacticBig, 60, gal60_start, gal60_end },
    { Face::GalacticBig, 90, gal90_start, gal90_end },
#endif
};

const lgfx::IFont* load(Built& b){
    if (!b.tried){
        b.tried = true;
        if (b.start){
            b.data.set(b.start, static_cast<uint32_t>(b.end - b.start));
            b.ok = b.vlw.loadFont(&b.data);
        }
    }
    return b.ok ? &b.vlw : nullptr;
}

// The size to scale from. M5GFX scales a VLW glyph by repeating or
// skipping whole pixels, and skipping loses strokes (an O without its
// right side), so: the largest size at or under `px` when it needs at
// most 30 % more, else the next one up, scaled down
void pick(LovyanGFX& g, Face face, int px){
    Built* below = nullptr;
    Built* above = nullptr;
    for (Built& b : kBuilt){
        if (b.face != face || !b.start) continue;
        if (b.px <= px) below = &b;
        else if (!above) above = &b;
    }
    Built* best = (below && px * 10 <= below->px * 13) ? below : (above ? above : below);
    const lgfx::IFont* f = best ? load(*best) : nullptr;
    if (!f){   // a build without the fonts: something readable all the same
        g.setFont(&fonts::DejaVu18);
        g.setTextSize(px > 0 ? px / 18.0f : 1.0f);
        return;
    }
    g.setFont(f);
    g.setTextSize(px > 0 ? static_cast<float>(px) / best->px : 1.0f);
}

// Next UTF-8 character of `s` from `i`, its bytes copied to `out`
size_t nextChar(const char* s, size_t i, char* out){
    size_t n = 1;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c >= 0xF0) n = 4; else if (c >= 0xE0) n = 3; else if (c >= 0xC0) n = 2;
    size_t k = 0;
    while (k < n && s[i + k]){ out[k] = s[i + k]; k++; }
    out[k] = '\0';
    return i + k;
}

}  // namespace

int textWidth(LovyanGFX& g, const char* s, Face face, int px, int tracking){
    pick(g, face, px);
    int w = g.textWidth(s);
    if (tracking){
        int n = 0;
        char ch[5];
        for (size_t i = 0; s[i]; n++) i = nextChar(s, i, ch);
        if (n > 1) w += tracking * (n - 1);
    }
    g.setTextSize(1);
    return w;
}

int text(LovyanGFX& g, const char* s, int x, int y, Face face, int px, uint16_t fg,
         textdatum_t datum, int32_t bg, int tracking){
    if (tracking){
        // Letter by letter from the left, once the whole run is placed
        const int w = textWidth(g, s, face, px, tracking);
        const int hAlign = datum & 3;   // 0 left, 1 centre, 2 right
        int left = hAlign == 1 ? x - w / 2 : (hAlign == 2 ? x - w : x);
        pick(g, face, px);
        if (bg < 0) g.setTextColor(fg); else g.setTextColor(fg, static_cast<uint16_t>(bg));
        g.setTextDatum(static_cast<textdatum_t>(datum & ~3));
        char ch[5];
        for (size_t i = 0; s[i];){
            i = nextChar(s, i, ch);
            g.drawString(ch, left, y);
            left += g.textWidth(ch) + tracking;
        }
        g.setTextSize(1);
        return w;
    }
    pick(g, face, px);
    if (bg < 0) g.setTextColor(fg); else g.setTextColor(fg, static_cast<uint16_t>(bg));
    g.setTextDatum(datum);
    const int w = g.drawString(s, x, y);
    g.setTextSize(1);
    return w;
}

std::string fit(LovyanGFX& g, const std::string& s, Face face, int px, int room){
    if (textWidth(g, s.c_str(), face, px) <= room) return s;
    std::string t = s;
    while (!t.empty() && textWidth(g, (t + "...").c_str(), face, px) > room) t.pop_back();
    return t + "...";
}

int wrap(LovyanGFX& g, const std::string& s, int x, int y, int width, Face face, int px,
         int lineH, uint16_t color, int maxLines, uint16_t emptyColor){
    if (s.empty()){
        text(g, "(EMPTY)", x, y, face, px, emptyColor);
        return 1;
    }
    // The longest start of `rest` (in bytes) that fits, by halving
    auto fitting = [&](const std::string& rest){
        size_t lo = 1, hi = rest.size();
        while (lo < hi){
            const size_t mid = (lo + hi + 1) / 2;
            if (textWidth(g, rest.substr(0, mid).c_str(), face, px) <= width) lo = mid; else hi = mid - 1;
        }
        return lo;
    };
    std::string rest = s;
    int lines = 0;
    while (!rest.empty() && lines < maxLines){
        size_t cut = fitting(rest);
        if (cut < rest.size()){
            const size_t sp = rest.rfind(' ', cut);
            if (sp != std::string::npos && sp > cut / 2) cut = sp + 1;
        }
        std::string piece = rest.substr(0, cut);
        if (lines == maxLines - 1 && cut < rest.size()){
            piece = fit(g, rest, face, px, width);
            rest.clear();
        } else {
            rest.erase(0, cut);
        }
        text(g, piece.c_str(), x, y + lines * lineH, face, px, color);
        lines++;
    }
    return lines;
}

int fitSize(LovyanGFX& g, const char* s, Face face, int px, int room){
    const int w = textWidth(g, s, face, px);
    if (w <= room || w <= 0) return px;
    const int scaled = px * room / w;
    return scaled > 4 ? scaled : 4;
}

uint16_t shade(uint16_t color, int percent){
    const int r = ((color >> 11) & 0x1F) * percent / 100;
    const int g = ((color >> 5) & 0x3F) * percent / 100;
    const int b = (color & 0x1F) * percent / 100;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

std::string upper(const std::string& s){
    std::string u = s;
    for (char& c : u) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return u;
}

}  // namespace monitor::glyphs
