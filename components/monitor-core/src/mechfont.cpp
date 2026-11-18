#include "monitor/mechfont.h"

#include <cmath>

namespace monitor::mech {

namespace {

constexpr float kGrid = 6.0f;   // grid units from the baseline to the cap line

// Each glyph: polylines as {count, x0, y0, x1, y1, ...}, then 0; y is up
// from the baseline. The house set (WP-01, EG-01 and HM-01 firmware,
// mech.py); "=" is EG-01's CAD addition; # ( ) [ ] , ' " ! ? $ and the
// middle dot are this project's, for session labels ("project #2"), hook
// data and the extra usage's amounts.
struct Glyph {
    uint32_t ch;
    float cols;
    const float* lines;
};

const float G_space[] = {0};
const float G_dash[]  = {2, 0, 3, 3, 3, 0};
const float G_slash[] = {2, 0, 0, 3, 6, 0};
const float G_under[] = {2, 0, 0, 3, 0, 0};
const float G_dot[]   = {2, 0, 0, 0, 0.6f, 0};
const float G_colon[] = {2, 0, 0.6f, 0, 1.4f, 2, 0, 4.0f, 0, 4.8f, 0};
const float G_gt[]    = {3, 0, 6, 3, 3, 0, 0, 0};
const float G_lt[]    = {3, 3, 6, 0, 3, 3, 0, 0};
const float G_plus[]  = {2, 0, 3, 4, 3, 2, 2, 1, 2, 5, 0};
const float G_pct[]   = {2, 0.4f, 0, 3.6f, 6,
                         5, 0, 4.2f, 1.6f, 4.2f, 1.6f, 6, 0, 6, 0, 4.2f,
                         5, 2.4f, 0, 4, 0, 4, 1.8f, 2.4f, 1.8f, 2.4f, 0, 0};
const float G_deg[]   = {5, 0, 4.5f, 1.5f, 4.5f, 1.5f, 6, 0, 6, 0, 4.5f, 0};
const float G_eq[]    = {2, 0, 1.8f, 4, 1.8f, 2, 0, 4.2f, 4, 4.2f, 0};
const float G_hash[]  = {2, 1, 0, 1.6f, 6, 2, 2.4f, 0, 3, 6, 2, 0, 2, 4, 2, 2, 0, 4, 4, 4, 0};
const float G_lpar[]  = {4, 2, 6, 0, 4, 0, 2, 2, 0, 0};
const float G_rpar[]  = {4, 0, 6, 2, 4, 2, 2, 0, 0, 0};
const float G_lbr[]   = {4, 2, 6, 0, 6, 0, 0, 2, 0, 0};
const float G_rbr[]   = {4, 0, 6, 2, 6, 2, 0, 0, 0, 0};
const float G_comma[] = {2, 0.6f, 0.8f, 0, -1, 0};
const float G_apos[]  = {2, 0, 6, 0, 4.4f, 0};
const float G_quot[]  = {2, 0, 6, 0, 4.4f, 2, 1.5f, 6, 1.5f, 4.4f, 0};
const float G_excl[]  = {2, 0, 6, 0, 2, 2, 0, 0, 0, 0.6f, 0};
const float G_quest[] = {7, 0, 5, 1, 6, 3, 6, 4, 5, 4, 4, 2, 2.6f, 2, 1.8f, 2, 2, 0, 2, 0.6f, 0};
const float G_mid[]   = {2, 0, 2.6f, 0, 3.4f, 0};
const float G_dollar[] = {10, 4, 6, 1, 6, 0, 5, 0, 4, 1, 3, 3, 3, 4, 2, 4, 1, 3, 0, 0, 0,
                          2, 2, -0.8f, 2, 6.8f, 0};
const float G_0[] = {7, 1, 0, 4, 0, 4, 5, 3, 6, 0, 6, 0, 1, 1, 0, 0};
const float G_1[] = {3, 0, 4.5f, 2, 6, 2, 0, 0};
const float G_2[] = {5, 0, 6, 3, 6, 4, 5, 4, 4, 0, 0, 2, 0, 0, 4, 0, 0};
const float G_3[] = {6, 0, 6, 3, 6, 4, 5, 4, 1, 3, 0, 0, 0, 2, 1.5f, 3, 4, 3, 0};
const float G_4[] = {3, 3, 0, 3, 6, 0, 2, 2, 0, 2, 4, 2, 0};
const float G_5[] = {8, 4, 6, 0, 6, 0, 3, 3, 3, 4, 2, 4, 1, 3, 0, 0, 0, 0};
const float G_6[] = {6, 4, 6, 1, 6, 0, 5, 0, 0, 4, 0, 4, 3, 2, 4, 3, 0, 3, 0};
const float G_7[] = {4, 0, 6, 4, 6, 4, 5, 1.5f, 0, 0};
const float G_8[] = {9, 1, 0, 3, 0, 4, 1, 4, 5, 3, 6, 1, 6, 0, 5, 0, 1, 1, 0, 2, 0, 3, 4, 3, 0};
const float G_9[] = {6, 0, 0, 3, 0, 4, 1, 4, 6, 0, 6, 0, 3, 2, 0, 3, 4, 3, 0};
const float G_A[] = {6, 0, 0, 0, 5, 1, 6, 3, 6, 4, 5, 4, 0, 2, 0, 2.5f, 4, 2.5f, 0};
const float G_B[] = {2, 0, 0, 0, 6,
                     6, 0, 6, 3, 6, 4, 5, 4, 4, 3, 3, 0, 3,
                     5, 3, 3, 4, 2, 4, 1, 3, 0, 0, 0, 0};
const float G_C[] = {6, 4, 6, 1, 6, 0, 5, 0, 1, 1, 0, 4, 0, 0};
const float G_D[] = {7, 0, 0, 3, 0, 4, 1, 4, 5, 3, 6, 0, 6, 0, 0, 0};
const float G_E[] = {4, 4, 6, 0, 6, 0, 0, 4, 0, 2, 0, 3, 3, 3, 0};
const float G_F[] = {3, 4, 6, 0, 6, 0, 0, 2, 0, 3, 3, 3, 0};
const float G_G[] = {9, 4, 5, 3, 6, 1, 6, 0, 5, 0, 1, 1, 0, 4, 0, 4, 3, 2, 3, 0};
const float G_H[] = {2, 0, 0, 0, 6, 2, 4, 0, 4, 6, 2, 0, 3, 4, 3, 0};
const float G_I[] = {2, 0, 0, 0, 6, 0};
const float G_J[] = {5, 4, 6, 4, 1, 3, 0, 1, 0, 0, 1, 0};
const float G_K[] = {2, 0, 0, 0, 6, 3, 4, 6, 1, 3, 4, 0, 2, 0, 3, 1, 3, 0};
const float G_L[] = {3, 0, 6, 0, 0, 4, 0, 0};
const float G_M[] = {5, 0, 0, 0, 6, 2, 3.5f, 4, 6, 4, 0, 0};
const float G_N[] = {4, 0, 0, 0, 6, 4, 0, 4, 6, 0};
const float G_O[] = {9, 1, 0, 3, 0, 4, 1, 4, 5, 3, 6, 1, 6, 0, 5, 0, 1, 1, 0, 0};
const float G_P[] = {2, 0, 0, 0, 6, 6, 0, 6, 3, 6, 4, 5, 4, 4, 3, 3, 0, 3, 0};
const float G_Q[] = {9, 1, 0, 3, 0, 4, 1, 4, 5, 3, 6, 1, 6, 0, 5, 0, 1, 1, 0, 2, 2.5f, 1.5f, 4, 0, 0};
const float G_R[] = {2, 0, 0, 0, 6,
                     6, 0, 6, 3, 6, 4, 5, 4, 4, 3, 3, 0, 3,
                     3, 3, 3, 4, 2, 4, 0, 0};
const float G_S[] = {10, 4, 6, 1, 6, 0, 5, 0, 4, 1, 3, 3, 3, 4, 2, 4, 1, 3, 0, 0, 0, 0};
const float G_T[] = {2, 0, 6, 4, 6, 2, 2, 6, 2, 0, 0};
const float G_U[] = {6, 0, 6, 0, 1, 1, 0, 3, 0, 4, 1, 4, 6, 0};
const float G_V[] = {5, 0, 6, 0, 2, 2, 0, 4, 2, 4, 6, 0};
const float G_W[] = {5, 0, 6, 0, 0, 2, 2, 4, 0, 4, 6, 0};
const float G_X[] = {2, 0, 0, 4, 6, 2, 0, 6, 4, 0, 0};
const float G_Y[] = {3, 0, 6, 2, 3, 4, 6, 2, 2, 3, 2, 0, 0};
const float G_Z[] = {4, 0, 6, 4, 6, 0, 0, 4, 0, 0};

const Glyph kGlyphs[] = {
    {' ', 3, G_space}, {'-', 3, G_dash}, {'/', 3, G_slash}, {'_', 3, G_under}, {'.', 0, G_dot},
    {':', 0, G_colon}, {'>', 3, G_gt},   {'<', 3, G_lt},    {'+', 4, G_plus},  {'%', 4, G_pct},
    {'=', 4, G_eq},    {'#', 4, G_hash}, {'(', 2, G_lpar},  {')', 2, G_rpar},  {'[', 2, G_lbr},
    {']', 2, G_rbr},   {',', 0.6f, G_comma}, {'\'', 0, G_apos}, {'"', 1.5f, G_quot},
    {'!', 0, G_excl},  {'?', 4, G_quest}, {'$', 4, G_dollar}, {0xB0, 1.5f, G_deg}, {0xB7, 0, G_mid},
    {'0', 4, G_0}, {'1', 2, G_1}, {'2', 4, G_2}, {'3', 4, G_3}, {'4', 4, G_4},
    {'5', 4, G_5}, {'6', 4, G_6}, {'7', 4, G_7}, {'8', 4, G_8}, {'9', 4, G_9},
    {'A', 4, G_A}, {'B', 4, G_B}, {'C', 4, G_C}, {'D', 4, G_D}, {'E', 4, G_E},
    {'F', 4, G_F}, {'G', 4, G_G}, {'H', 4, G_H}, {'I', 0, G_I}, {'J', 4, G_J},
    {'K', 4, G_K}, {'L', 4, G_L}, {'M', 4, G_M}, {'N', 4, G_N}, {'O', 4, G_O},
    {'P', 4, G_P}, {'Q', 4, G_Q}, {'R', 4, G_R}, {'S', 4, G_S}, {'T', 4, G_T},
    {'U', 4, G_U}, {'V', 4, G_V}, {'W', 4, G_W}, {'X', 4, G_X}, {'Y', 4, G_Y},
    {'Z', 4, G_Z},
};

// U+00C0..U+00FF folded to what the font has: À..Å to A, Ç to C, × to X, ...
const char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOOXOUUUUYPSAAAAAAACEEEEIIIIDNOOOOO/OUUUUYPY";

const Glyph& glyphFor(uint32_t cp){
    if (cp >= 'a' && cp <= 'z') cp -= 'a' - 'A';
    else if (cp >= 0xC0 && cp <= 0xFF) cp = static_cast<unsigned char>(kLatin1[cp - 0xC0]);
    for (const Glyph& g : kGlyphs){
        if (g.ch == cp) return g;
    }
    return kGlyphs[0];
}

// The next code point of UTF-8 `p`, which moves past it. A byte that does
// not start a character is skipped
bool next(const unsigned char*& p, uint32_t& cp){
    while (*p && (*p & 0xC0) == 0x80) ++p;
    if (!*p) return false;
    const unsigned char c = *p++;
    if (c < 0x80){ cp = c; return true; }
    const int extra = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : 1);
    cp = c & (0x3F >> extra);
    for (int i = 0; i < extra && (*p & 0xC0) == 0x80; ++i) cp = (cp << 6) | (*p++ & 0x3F);
    return true;
}

float advance(const Glyph& g, float u, float weight){
    return g.cols * u + weight + weight;   // the glyph, its stroke, a gap as wide as the stroke
}

}  // namespace

float strokeWeight(float cap){
    const float w = cap * 0.15f;
    return w > 1.2f ? w : 1.2f;
}

float textWidth(const char* s, float cap, float weight){
    if (!s || !*s || cap <= 0.0f) return 0.0f;
    const float w = weight > 0.0f ? weight : strokeWeight(cap);
    const float u = (cap - w) / kGrid;
    float total = 0.0f;
    bool any = false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    uint32_t cp;
    while (next(p, cp)){
        total += advance(glyphFor(cp), u, w);
        any = true;
    }
    return any ? total - w : 0.0f;
}

int drawText(LovyanGFX& g, const char* s, float x, float baseline, float cap, uint16_t color,
             Align align, float weight){
    if (!s || !*s || cap <= 0.0f) return 0;
    const float w = weight > 0.0f ? weight : strokeWeight(cap);
    const float width = textWidth(s, cap, w);
    if (align == Align::Center) x -= 0.5f * width;
    else if (align == Align::Right) x -= width;
    const float u = (cap - w) / kGrid;
    const float h = 0.5f * w;   // the stroke's centre line sits half a stroke inside the box
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    uint32_t cp;
    while (next(p, cp)){
        const Glyph& gl = glyphFor(cp);
        const float* d = gl.lines;
        while (*d > 0.0f){
            const int n = static_cast<int>(*d++);
            for (int i = 0; i + 1 < n; i++){
                const float x0 = x + d[2 * i] * u + h;
                const float y0 = baseline - (d[2 * i + 1] * u + h);
                const float x1 = x + d[2 * i + 2] * u + h;
                const float y1 = baseline - (d[2 * i + 3] * u + h);
                g.drawWideLine(static_cast<int32_t>(lroundf(x0)), static_cast<int32_t>(lroundf(y0)),
                               static_cast<int32_t>(lroundf(x1)), static_cast<int32_t>(lroundf(y1)), h, color);
            }
            d += 2 * n;
        }
        x += advance(gl, u, w);
    }
    return static_cast<int>(lroundf(width));
}

std::string fitText(const std::string& s, float cap, float room, float weight){
    if (textWidth(s.c_str(), cap, weight) <= room) return s;
    std::string t = s;
    while (!t.empty()){
        // One whole character off the end: its continuation bytes, then its first
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (textWidth((t + "...").c_str(), cap, weight) <= room) break;
    }
    return t + "...";
}

float fitCap(const char* s, float cap, float room){
    const float w = textWidth(s, cap);
    if (w <= room || w <= 0.0f) return cap;
    const float scaled = cap * room / w;
    return scaled > 5.0f ? scaled : 5.0f;
}

}  // namespace monitor::mech
