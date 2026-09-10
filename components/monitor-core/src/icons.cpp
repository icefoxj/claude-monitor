#include "monitor/icons.h"

#include <cmath>

namespace monitor {

namespace {

constexpr float kPi = 3.14159265f;

// Compile-time RGB888 -> RGB565, so colours are picked in 0-255 values
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t kColorBg     = rgb565(0, 0, 0);
constexpr uint16_t kColorRed    = rgb565(210, 35, 35);
constexpr uint16_t kColorBlue   = rgb565(35, 120, 225);
constexpr uint16_t kColorYellow = rgb565(255, 200, 0);
constexpr uint16_t kColorGreen  = rgb565(60, 180, 75);
constexpr uint16_t kColorWhite  = rgb565(255, 255, 255);

inline int px(float v){ return static_cast<int>(lroundf(v)); }

}  // namespace

Icons::Icons(M5Canvas& canvas, int size, int pushX, int pushY)
    : canvas_(canvas),
      unit_(size / 128.0f),
      cx_(size * 0.5f),
      cy_(size * 0.5f),
      pushX_(pushX),
      pushY_(pushY) {}

// ---------------- helper primitives ----------------

Icons::Pt Icons::rotated(float x, float y, float angle, float scale) const {
    x *= scale * unit_;
    y *= scale * unit_;
    float c = cosf(angle);
    float s = sinf(angle);
    return { cx_ + x * c - y * s, cy_ + x * s + y * c };
}

// Oriented rectangle (2 triangles) plus a circle at each end
void Icons::thickLine(Pt a, Pt b, float thickness, uint16_t color){
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float length = sqrtf(dx*dx + dy*dy);
    if (length < 0.001f){
        return;
    }
    float half = thickness * unit_ * 0.5f;
    float hx = -dy / length * half;
    float hy =  dx / length * half;

    canvas_.fillTriangle(px(a.x+hx), px(a.y+hy), px(b.x+hx), px(b.y+hy), px(b.x-hx), px(b.y-hy), color);
    canvas_.fillTriangle(px(a.x+hx), px(a.y+hy), px(b.x-hx), px(b.y-hy), px(a.x-hx), px(a.y-hy), color);
    canvas_.fillCircle(px(a.x), px(a.y), px(half), color);
    canvas_.fillCircle(px(b.x), px(b.y), px(half), color);
}

void Icons::thickArc(float cx, float cy, float r, float a0, float a1,
                     float thickness, uint16_t color, float angle, float scale){
    const int steps = 16;
    float t0 = a0 * kPi / 180.0f;
    Pt prev = rotated(cx + r * cosf(t0), cy + r * sinf(t0), angle, scale);
    for (int i = 1; i <= steps; i++){
        float t = (a0 + (a1 - a0) * i / steps) * kPi / 180.0f;
        Pt next = rotated(cx + r * cosf(t), cy + r * sinf(t), angle, scale);
        thickLine(prev, next, thickness * scale, color);
        prev = next;
    }
}

// Vertices every 45 degrees, offset by 22.5, filled as a triangle fan
void Icons::octagon(float radius, float angle, uint16_t color){
    float rp = radius * unit_;
    Pt v[8];
    for (int i = 0; i < 8; i++){
        float a = (22.5f + 45.0f * i) * kPi / 180.0f + angle;
        v[i] = { cx_ + rp * cosf(a), cy_ + rp * sinf(a) };
    }
    for (int i = 1; i < 7; i++){
        canvas_.fillTriangle(px(v[0].x), px(v[0].y), px(v[i].x), px(v[i].y), px(v[i+1].x), px(v[i+1].y), color);
    }
}

void Icons::sign(uint16_t body, float angle, float scale){
    canvas_.fillSprite(kColorBg);
    octagon(58 * scale, angle, kColorWhite);   // white border
    octagon(51 * scale, angle, body);          // coloured body
}

void Icons::push(){
    canvas_.pushSprite(pushX_, pushY_);
}

// ---------------- icons ----------------

void Icons::waiting(float angle, float scale){
    sign(kColorRed, angle, scale);

    // Exclamation mark: a pill-shaped bar and a dot
    thickLine(rotated(0, -28, angle, scale), rotated(0, 6, angle, scale), 13 * scale, kColorWhite);
    Pt dot = rotated(0, 26, angle, scale);
    canvas_.fillCircle(px(dot.x), px(dot.y), px(7 * scale * unit_), kColorWhite);

    push();
}

void Icons::question(float angle, float scale){
    sign(kColorBlue, angle, scale);

    // Question mark: a 260-degree hook, a short stem and a dot
    thickArc(0, -14, 16, 190, 450, 12, kColorWhite, angle, scale);
    thickLine(rotated(0, 2, angle, scale), rotated(0, 8, angle, scale), 12 * scale, kColorWhite);
    Pt dot = rotated(0, 27, angle, scale);
    canvas_.fillCircle(px(dot.x), px(dot.y), px(7 * scale * unit_), kColorWhite);

    push();
}

void Icons::gear(float spinAngle){
    canvas_.fillSprite(kColorBg);

    const int   teeth = 8;
    const float rTip  = 52.0f * unit_;   // outer radius of the teeth
    const float rBody = 38.0f * unit_;   // body radius
    const float rHole = 14.0f * unit_;   // centre hole radius
    const float step  = 2.0f * kPi / teeth;

    // Each tooth is a radial trapezoid: wide base at the body, narrower tip;
    // decomposed into 2 triangles. Rotation is done by maths, no sprite API
    for (int i = 0; i < teeth; i++){
        float a  = spinAngle + i * step;
        float wb = step * 0.28f;   // angular half-width at the base
        float wt = step * 0.16f;   // angular half-width at the tip
        float b0x = cx_ + rBody * cosf(a - wb);
        float b0y = cy_ + rBody * sinf(a - wb);
        float b1x = cx_ + rBody * cosf(a + wb);
        float b1y = cy_ + rBody * sinf(a + wb);
        float t0x = cx_ + rTip * cosf(a - wt);
        float t0y = cy_ + rTip * sinf(a - wt);
        float t1x = cx_ + rTip * cosf(a + wt);
        float t1y = cy_ + rTip * sinf(a + wt);

        canvas_.fillTriangle(px(b0x), px(b0y), px(b1x), px(b1y), px(t1x), px(t1y), kColorYellow);
        canvas_.fillTriangle(px(b0x), px(b0y), px(t1x), px(t1y), px(t0x), px(t0y), kColorYellow);
    }
    canvas_.fillCircle(px(cx_), px(cy_), px(rBody), kColorYellow);
    canvas_.fillCircle(px(cx_), px(cy_), px(rHole), kColorBg);   // centre hole

    push();
}

void Icons::done(float angle){
    canvas_.fillSprite(kColorBg);
    canvas_.fillCircle(px(cx_), px(cy_), px(56 * unit_), kColorGreen);

    // Check mark as two thick white strokes
    thickLine(rotated(-27, 2, angle), rotated(-8, 23, angle), 13, kColorWhite);
    thickLine(rotated(-8, 23, angle), rotated(30, -21, angle), 13, kColorWhite);

    push();
}

void Icons::blank(){
    canvas_.fillSprite(kColorBg);
    push();
}

}  // namespace monitor
