#include "monitor/anime.h"

#include <sdkconfig.h>

#include <cmath>
#include <cstring>

namespace monitor::anime {

namespace {

// ---------------- fonts ----------------

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

#if defined(CONFIG_MONITOR_ANIME_THEME)

#define MONITOR_FONT(name) \
    extern const uint8_t name##_start[] asm("_binary_" #name "_vlw_start"); \
    extern const uint8_t name##_end[]   asm("_binary_" #name "_vlw_end");
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
#undef MONITOR_FONT

// Smallest first within a face
Built kBuilt[] = {
    { Face::Cond,   18,  cond18_start,    cond18_end },
    { Face::Cond,   22,  cond22_start,    cond22_end },
    { Face::Cond,   26,  cond26_start,    cond26_end },
    { Face::Cond,   32,  cond32_start,    cond32_end },
    { Face::Cond,   40,  cond40_start,    cond40_end },
    { Face::Cond,   56,  cond56_start,    cond56_end },
    { Face::Mono,   14,  mono14_start,    mono14_end },
    { Face::Mono,   16,  mono16_start,    mono16_end },
    { Face::Mono,   20,  mono20_start,    mono20_end },
    { Face::Gothic, 22,  gothic22_start,  gothic22_end },
    { Face::Gothic, 32,  gothic32_start,  gothic32_end },
    { Face::Mincho, 44,  mincho44_start,  mincho44_end },
    { Face::Kanji,  44,  mincho44_start,  mincho44_end },
    { Face::Kanji,  96,  mincho96_start,  mincho96_end },
    { Face::Kanji,  192, mincho192_start, mincho192_end },
};

#else

Built kBuilt[] = { { Face::Cond, 0, nullptr, nullptr } };

#endif

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
        if (b.face != face) continue;
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

inline int iround(float v){ return static_cast<int>(lroundf(v)); }

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

Look look(State s){
    switch (s){
        case State::Processing:  return { kOrange, kOrangeDark, "処理中",   "PROCESSING" };
        case State::Compacting:  return { kGrey,   kGreyDark,   "圧縮中",   "COMPACTING" };
        case State::WaitingUser: return { kRed,    kRedDark,    "承認待機", "APPROVAL" };
        case State::Question:    return { kCyan,   kCyanDark,   "質問",     "QUESTION" };
        case State::Error:       return { kRed,    kRedDark,    "異常発生", "ERROR" };
        case State::Paused:      return { kAmber,  kAmberDark,  "待機中",   "STANDBY" };
        case State::Idle:        return { kGreen,  kGreenDark,  "完了",     "COMPLETE" };
        case State::Off:         return { kGrey,   kGreyDark,   "停止",     "OFFLINE" };
    }
    return { kGrey, kGreyDark, "", "" };
}

// ---------------- shapes ----------------

void frame(LovyanGFX& g, int x, int y, int w, int h, int cut, int t, uint16_t color){
    for (int i = 0; i < t; i++){
        const int l = x + i, r = x + w - 1 - i, top = y + i, b = y + h - 1 - i;
        g.drawLine(x + cut, top, r, top, color);           // top
        g.drawLine(r, top, r, y + h - 1 - cut, color);     // right
        g.drawLine(r, y + h - 1 - cut, x + w - 1 - cut, b, color);   // bottom-right cut
        g.drawLine(x + w - 1 - cut, b, l, b, color);       // bottom
        g.drawLine(l, b, l, y + cut, color);               // left
        g.drawLine(l, y + cut, x + cut, top, color);       // top-left cut
    }
}

void fillFrame(LovyanGFX& g, int x, int y, int w, int h, int cut, uint16_t color){
    g.fillRect(x + cut, y, w - cut, h - cut, color);
    g.fillRect(x, y + cut, cut, h - cut, color);
    g.fillRect(x + cut, y + h - cut, w - 2 * cut, cut, color);
    g.fillTriangle(x, y + cut, x + cut, y, x + cut, y + cut, color);
    g.fillTriangle(x + w - cut, y + h - cut, x + w - 1, y + h - cut, x + w - cut, y + h - 1, color);
}

void hazard(LovyanGFX& g, int x, int y, int w, int h, int stripe, uint16_t a, uint16_t b){
    if (w <= 0 || h <= 0) return;
    if (stripe < 2) stripe = 2;
    int32_t cx, cy, cw, ch;
    g.getClipRect(&cx, &cy, &cw, &ch);
    // This rectangle, within whatever clip was set
    const int l = x > cx ? x : cx, t = y > cy ? y : cy;
    const int r = (x + w < cx + cw) ? x + w : cx + cw, bt = (y + h < cy + ch) ? y + h : cy + ch;
    if (r <= l || bt <= t) return;
    g.setClipRect(l, t, r - l, bt - t);
    g.fillRect(x, y, w, h, b);
    for (int k = -h; k < w; k += 2 * stripe){
        g.fillTriangle(x + k, y + h, x + k + h, y, x + k + h + stripe, y, a);
        g.fillTriangle(x + k, y + h, x + k + h + stripe, y, x + k + stripe, y + h, a);
    }
    g.setClipRect(cx, cy, cw, ch);
}

void hexagon(LovyanGFX& g, float cx, float cy, float r, uint16_t color){
    int vx[6], vy[6];
    for (int i = 0; i < 6; i++){
        const float a = i * 3.14159265f / 3.0f;
        vx[i] = iround(cx + r * cosf(a));
        vy[i] = iround(cy + r * sinf(a));
    }
    for (int i = 1; i < 5; i++) g.fillTriangle(vx[0], vy[0], vx[i], vy[i], vx[i + 1], vy[i + 1], color);
}

void hexagonOutline(LovyanGFX& g, float cx, float cy, float r, float t, uint16_t color){
    // A ring of six quads between the outer and the inner hexagon
    float ox[6], oy[6], ix[6], iy[6];
    for (int i = 0; i < 6; i++){
        const float a = i * 3.14159265f / 3.0f;
        ox[i] = cx + r * cosf(a);       oy[i] = cy + r * sinf(a);
        ix[i] = cx + (r - t) * cosf(a); iy[i] = cy + (r - t) * sinf(a);
    }
    for (int i = 0; i < 6; i++){
        const int j = (i + 1) % 6;
        g.fillTriangle(iround(ox[i]), iround(oy[i]), iround(ox[j]), iround(oy[j]), iround(ix[j]), iround(iy[j]), color);
        g.fillTriangle(iround(ox[i]), iround(oy[i]), iround(ix[j]), iround(iy[j]), iround(ix[i]), iround(iy[i]), color);
    }
}

int tag(LovyanGFX& g, int x, int y, int h, const char* s, Face face, int px, uint16_t fg, uint16_t bg){
    const int pad = h / 3;
    const int w = textWidth(g, s, face, px) + 2 * pad;
    const int slant = h / 2;
    g.fillRect(x, y, w, h, bg);
    g.fillTriangle(x + w, y, x + w + slant, y, x + w, y + h - 1, bg);
    // The text's cell can be taller than the tag: keep its background inside
    int32_t cx, cy, cw, ch;
    g.getClipRect(&cx, &cy, &cw, &ch);
    const int t = y > cy ? y : cy, b = (y + h < cy + ch) ? y + h : cy + ch;
    if (b > t){
        g.setClipRect(cx, t, cw, b - t);
        text(g, s, x + pad, y + h / 2, face, px, fg, textdatum_t::middle_left, bg);
        g.setClipRect(cx, cy, cw, ch);
    }
    return w + slant;
}

// ---------------- seven segments ----------------

namespace {

// Segments a..g as bits 0..6
uint8_t segmentsFor(char c){
    static const uint8_t kDigits[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    if (c >= '0' && c <= '9') return kDigits[c - '0'];
    if (c == '-') return 0x40;
    return 0;
}

float cellWidth(char c, int h){
    if (c == ':' || c == '.') return h * 0.26f;
    return h * 0.54f;
}

constexpr float kSkew = 0.08f;   // a slight lean, like a real display

// One segment as a hexagon along (x0, y0)-(x1, y1), axis-aligned
void segment(LovyanGFX& g, float x0, float y0, float x1, float y1, float half, float baseY, uint16_t color){
    float px[6], py[6];
    if (y0 == y1){
        px[0] = x0;        py[0] = y0;
        px[1] = x0 + half; py[1] = y0 - half;
        px[2] = x1 - half; py[2] = y0 - half;
        px[3] = x1;        py[3] = y0;
        px[4] = x1 - half; py[4] = y0 + half;
        px[5] = x0 + half; py[5] = y0 + half;
    } else {
        px[0] = x0;        py[0] = y0;
        px[1] = x0 + half; py[1] = y0 + half;
        px[2] = x0 + half; py[2] = y1 - half;
        px[3] = x0;        py[3] = y1;
        px[4] = x0 - half; py[4] = y1 - half;
        px[5] = x0 - half; py[5] = y0 + half;
    }
    int ix[6], iy[6];
    for (int i = 0; i < 6; i++){
        ix[i] = iround(px[i] + (baseY - py[i]) * kSkew);
        iy[i] = iround(py[i]);
    }
    for (int i = 1; i < 5; i++) g.fillTriangle(ix[0], iy[0], ix[i], iy[i], ix[i + 1], iy[i + 1], color);
}

void digit(LovyanGFX& g, float l, float t, float w, float h, uint8_t bits, uint16_t color){
    const float th = h * 0.13f, half = th * 0.5f, gap = th * 0.18f;
    const float xl = l + half, xr = l + w - half;
    const float yt = t + half, ym = t + h * 0.5f, yb = t + h - half;
    const float base = t + h;
    if (bits & 0x01) segment(g, xl + gap, yt, xr - gap, yt, half, base, color);   // a
    if (bits & 0x02) segment(g, xr, yt + gap, xr, ym - gap, half, base, color);   // b
    if (bits & 0x04) segment(g, xr, ym + gap, xr, yb - gap, half, base, color);   // c
    if (bits & 0x08) segment(g, xl + gap, yb, xr - gap, yb, half, base, color);   // d
    if (bits & 0x10) segment(g, xl, ym + gap, xl, yb - gap, half, base, color);   // e
    if (bits & 0x20) segment(g, xl, yt + gap, xl, ym - gap, half, base, color);   // f
    if (bits & 0x40) segment(g, xl + gap, ym, xr - gap, ym, half, base, color);   // g
}

}  // namespace

int sevenSegWidth(const char* s, int h){
    float w = 0.0f;
    const size_t n = strlen(s);
    for (size_t i = 0; i < n; i++){
        w += cellWidth(s[i], h);
        if (i + 1 < n) w += h * 0.14f;
    }
    return iround(w);
}

int sevenSeg(LovyanGFX& g, const char* s, int x, int y, int h, uint16_t on, uint16_t off, bool right){
    const int total = sevenSegWidth(s, h);
    float l = right ? static_cast<float>(x - total) : static_cast<float>(x);
    const size_t n = strlen(s);
    for (size_t i = 0; i < n; i++){
        const char c = s[i];
        const float cw = cellWidth(c, h);
        if (c == ':' || c == '.'){
            const float d = h * 0.13f;
            const float cx = l + (cw - d) * 0.5f;
            if (c == ':'){
                g.fillRect(iround(cx + (y + h - (y + h * 0.3f)) * kSkew), iround(y + h * 0.3f - d / 2), iround(d), iround(d), on);
                g.fillRect(iround(cx + (y + h - (y + h * 0.7f)) * kSkew), iround(y + h * 0.7f - d / 2), iround(d), iround(d), on);
            } else {
                g.fillRect(iround(cx), iround(y + h - d), iround(d), iround(d), on);
            }
        } else {
            if (off != on) digit(g, l, static_cast<float>(y), cw, static_cast<float>(h), 0x7F, off);
            const uint8_t bits = segmentsFor(c);
            if (bits) digit(g, l, static_cast<float>(y), cw, static_cast<float>(h), bits, on);
        }
        l += cw + h * 0.14f;
    }
    return total;
}

namespace {

// Blocks about two thirds as wide as tall, stretched to fill the width
struct BlockRow { int n, bw, gap, slant; };

BlockRow blockRow(int w, int h){
    BlockRow r;
    r.slant = h / 3;
    r.gap = h / 4 > 2 ? h / 4 : 2;
    r.bw = h * 2 / 3 > 4 ? h * 2 / 3 : 4;
    r.n = (w - r.slant + r.gap) / (r.bw + r.gap);
    if (r.n < 1) r.n = 1;
    r.bw = (w - r.slant - r.gap * (r.n - 1)) / r.n;
    return r;
}

void block(LovyanGFX& g, int x, int y, int h, const BlockRow& r, int i, uint16_t c){
    const int bx = x + i * (r.bw + r.gap);
    g.fillTriangle(bx + r.slant, y, bx + r.slant + r.bw, y, bx, y + h - 1, c);
    g.fillTriangle(bx + r.slant + r.bw, y, bx + r.bw, y + h - 1, bx, y + h - 1, c);
}

}  // namespace

void blocks(LovyanGFX& g, int x, int y, int w, int h, float fraction, uint16_t on, uint16_t off){
    if (w <= 0 || h <= 0) return;
    const BlockRow r = blockRow(w, h);
    if (fraction < 0.0f) fraction = 0.0f;
    int lit = static_cast<int>(lroundf(fraction * r.n));
    if (lit == 0 && fraction > 0.0f) lit = 1;   // something used shows
    if (lit > r.n) lit = r.n;
    for (int i = 0; i < r.n; i++) block(g, x, y, h, r, i, i < lit ? on : off);
}

void splitBlocks(LovyanGFX& g, int x, int y, int w, int h, const float* shares, const uint16_t* colors,
                 int n, uint16_t off){
    if (w <= 0 || h <= 0) return;
    const BlockRow r = blockRow(w, h);
    float total = 0.0f;
    for (int k = 0; k < n; k++) total += shares[k];
    for (int i = 0; i < r.n; i++){
        uint16_t c = off;
        if (total > 0.0f){
            const float mid = (i + 0.5f) / r.n * total;
            float acc = 0.0f;
            for (int k = 0; k < n; k++){
                acc += shares[k];
                if (mid < acc || k == n - 1){ c = colors[k]; break; }
            }
        }
        block(g, x, y, h, r, i, c);
    }
}

uint16_t splitColor(int i){
    static constexpr uint16_t kColors[] = {
        kOrange,               // Claude Code: the theme's colour
        kCyan,
        rgb(176, 120, 255),    // violet
        kGreen,
        kGrey,
    };
    return kColors[i % 5];
}

uint16_t levelColor(float percent, const std::string& severity){
    int level = percent >= 80.0f ? 2 : (percent >= 50.0f ? 1 : 0);
    if (severity == "critical") level = 2;
    else if (severity == "warning" && level < 1) level = 1;
    return level == 2 ? kRed : (level == 1 ? kAmber : kGreen);
}

void dashed(LovyanGFX& g, int x0, int y0, int x1, int y1, uint16_t color, int dash, int gap){
    const float dx = static_cast<float>(x1 - x0), dy = static_cast<float>(y1 - y0);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    for (float s = 0.0f; s < len; s += dash + gap){
        const float e = s + dash < len ? s + dash : len;
        for (int k = 0; k < 2; k++){
            g.drawLine(x0 + static_cast<int>(dx * s / len), y0 + static_cast<int>(dy * s / len) + k,
                       x0 + static_cast<int>(dx * e / len), y0 + static_cast<int>(dy * e / len) + k, color);
        }
    }
}

}  // namespace monitor::anime
