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
constexpr uint16_t kColorAmber  = rgb565(255, 150, 30);
constexpr uint16_t kColorSand   = rgb565(255, 205, 70);
constexpr uint16_t kColorGlass  = rgb565(48, 32, 8);
constexpr uint16_t kColorGreen  = rgb565(60, 180, 75);
constexpr uint16_t kColorGrey   = rgb565(150, 150, 150);
constexpr uint16_t kColorWhite  = rgb565(255, 255, 255);

inline int px(float v){ return static_cast<int>(lroundf(v)); }

// Hourglass glass wall: top bulb, left side, from the frame down to the
// neck, in 128-space. The other three walls are mirror images.
constexpr float kWall[][2] = {
    {-26, -37}, {-26, -28}, {-23, -19}, {-17, -11}, {-9, -5}, {-3, -1},
};
constexpr int kWallPoints = sizeof(kWall) / sizeof(kWall[0]);
constexpr float kBulbFrame = -37.0f;   // height of the wide end
constexpr float kBulbNeck  = -1.0f;    // height of the neck

// x of the left wall at height y (top-bulb coordinates), piecewise linear
float wallX(float y){
    if (y <= kWall[0][1]) return kWall[0][0];
    for (int i = 0; i < kWallPoints - 1; i++){
        if (y <= kWall[i + 1][1]){
            float t = (y - kWall[i][1]) / (kWall[i + 1][1] - kWall[i][1]);
            return kWall[i][0] + t * (kWall[i + 1][0] - kWall[i][0]);
        }
    }
    return kWall[kWallPoints - 1][0];
}

}  // namespace

Icons::Icons(M5Canvas& canvas, int size, int pushX, int pushY)
    : canvas_(canvas),
      unit_(size / 128.0f),
      cx_(size * 0.5f),
      cy_(size * 0.5f),
      pushX_(pushX),
      pushY_(pushY) {}

void Icons::setSize(int size, int pushX, int pushY){
    unit_  = size / 128.0f;
    cx_    = size * 0.5f;
    cy_    = size * 0.5f;
    pushX_ = pushX;
    pushY_ = pushY;
}

// ---------------- helper primitives ----------------

Icons::Pt Icons::rotated(float x, float y, float angle, float scale) const {
    x *= scale * unit_;
    y *= scale * unit_;
    float c = cosf(angle);
    float s = sinf(angle);
    return { cx_ + x * c - y * s, cy_ + x * s + y * c };
}

void Icons::tri(Pt a, Pt b, Pt c, uint16_t color){
    canvas_.fillTriangle(px(a.x), px(a.y), px(b.x), px(b.y), px(c.x), px(c.y), color);
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

    tri({a.x+hx, a.y+hy}, {b.x+hx, b.y+hy}, {b.x-hx, b.y-hy}, color);
    tri({a.x+hx, a.y+hy}, {b.x-hx, b.y-hy}, {a.x-hx, a.y-hy}, color);
    canvas_.fillCircle(px(a.x), px(a.y), px(half), color);
    canvas_.fillCircle(px(b.x), px(b.y), px(half), color);
}

void Icons::thickArc(float cx, float cy, float r, float a0, float a1,
                     float thickness, uint16_t color, float angle, float scale){
    // A segment every ~6 degrees keeps a full ring round at radius 60
    int steps = static_cast<int>(fabsf(a1 - a0) / 6.0f);
    if (steps < 4) steps = 4;
    if (steps > 64) steps = 64;
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
        tri(v[0], v[i], v[i+1], color);
    }
}

void Icons::sign(uint16_t body, float angle, float scale){
    canvas_.fillSprite(kColorBg);
    octagon(58 * scale, angle, kColorWhite);   // white border
    octagon(51 * scale, angle, body);          // coloured body
}

// Each tooth is a radial trapezoid: wide base at the body, narrower tip;
// decomposed into 2 triangles. Rotation is done by maths, no sprite API
void Icons::gearBody(float cx, float cy, float spin, int teeth,
                     float rTip, float rBody, float rHole, uint16_t color){
    rTip  *= unit_;
    rBody *= unit_;
    rHole *= unit_;
    const float step = 2.0f * kPi / teeth;
    for (int i = 0; i < teeth; i++){
        float a  = spin + i * step;
        float wb = step * 0.28f;   // angular half-width at the base
        float wt = step * 0.16f;   // angular half-width at the tip
        Pt b0 = { cx + rBody * cosf(a - wb), cy + rBody * sinf(a - wb) };
        Pt b1 = { cx + rBody * cosf(a + wb), cy + rBody * sinf(a + wb) };
        Pt t0 = { cx + rTip * cosf(a - wt),  cy + rTip * sinf(a - wt) };
        Pt t1 = { cx + rTip * cosf(a + wt),  cy + rTip * sinf(a + wt) };
        tri(b0, b1, t1, color);
        tri(b0, t1, t0, color);
    }
    canvas_.fillCircle(px(cx), px(cy), px(rBody), color);
    canvas_.fillCircle(px(cx), px(cy), px(rHole), kColorBg);   // centre hole
}

// Polygon between the two walls from ya to yb, filled as a fan from its middle
void Icons::fillBulb(float sy, float ya, float yb, uint16_t color, float angle){
    if (yb - ya < 0.5f){
        return;
    }
    Pt poly[2 * kWallPoints + 4];
    int n = 0;
    poly[n++] = rotated(wallX(ya), ya * sy, angle);
    for (int i = 0; i < kWallPoints; i++){
        if (kWall[i][1] > ya && kWall[i][1] < yb){
            poly[n++] = rotated(kWall[i][0], kWall[i][1] * sy, angle);
        }
    }
    poly[n++] = rotated(wallX(yb), yb * sy, angle);
    poly[n++] = rotated(-wallX(yb), yb * sy, angle);
    for (int i = kWallPoints - 1; i >= 0; i--){
        if (kWall[i][1] > ya && kWall[i][1] < yb){
            poly[n++] = rotated(-kWall[i][0], kWall[i][1] * sy, angle);
        }
    }
    poly[n++] = rotated(-wallX(ya), ya * sy, angle);

    Pt c = rotated(0, (ya + yb) * 0.5f * sy, angle);
    for (int i = 0; i < n; i++){
        tri(c, poly[i], poly[(i + 1) % n], color);
    }
}

void Icons::push(){
    canvas_.pushSprite(pushX_, pushY_);
}

// ---------------- overlays ----------------

constexpr float kBandRadius = 60.0f;   // just outside the icons (max 58 with the pulse excluded)

void Icons::ring(float angle, float fraction, int lap){
    static constexpr uint16_t kLapColor[] = { kColorYellow, kColorAmber, kColorRed };
    if (lap >= 3){
        thickArc(0, 0, kBandRadius, -90, 270, 3, kColorRed, angle, 1.0f);
        return;
    }
    if (lap > 0){
        thickArc(0, 0, kBandRadius, -90, 270, 3, kLapColor[lap - 1], angle, 1.0f);
    }
    if (fraction > 0.005f){
        thickArc(0, 0, kBandRadius, -90, -90 + 360.0f * fraction, 3, kLapColor[lap], angle, 1.0f);
    }
}

void Icons::sessionDots(float angle, std::string_view codes){
    int n = static_cast<int>(codes.size());
    if (n > 8) n = 8;
    const float step = 10.0f;   // degrees between dots along the band
    for (int i = 0; i < n; i++){
        uint16_t color;
        switch (codes[i]){
            case 'p': color = kColorYellow; break;
            case 'c': color = kColorGrey;   break;
            case 'w':
            case 'e': color = kColorRed;    break;
            case 'q': color = kColorBlue;   break;
            case 'h': color = kColorAmber;  break;
            case 'i': color = kColorGreen;  break;
            default:  color = kColorGlass;  break;
        }
        float a = (90.0f + (i - (n - 1) * 0.5f) * step) * kPi / 180.0f;
        Pt p = rotated(kBandRadius * cosf(a), kBandRadius * sinf(a), angle);
        canvas_.fillCircle(px(p.x), px(p.y), px(4.5f * unit_), kColorBg);   // outline, so it reads over the ring
        canvas_.fillCircle(px(p.x), px(p.y), px(3.5f * unit_), color);
    }
}

void Icons::linkLost(float angle){
    Pt p = rotated(0, -kBandRadius, angle);
    canvas_.fillCircle(px(p.x), px(p.y), px(5.0f * unit_), kColorGrey);
    canvas_.fillCircle(px(p.x), px(p.y), px(2.5f * unit_), kColorBg);
}

void Icons::toolMark(float angle){
    Pt p = rotated(kBandRadius, 0, angle);
    canvas_.fillCircle(px(p.x), px(p.y), px(5.0f * unit_), kColorBg);   // outline over the ring
    canvas_.fillCircle(px(p.x), px(p.y), px(4.0f * unit_), kColorBlue);
}

// ---------------- icons ----------------

void Icons::gear(float spinAngle, GearStyle style){
    canvas_.fillSprite(kColorBg);
    if (style == GearStyle::Subagents){
        // A smaller main gear and a satellite meshing up-right, turning the
        // other way at the tooth ratio. The pair is offset so that its
        // bounding box, not the main gear, sits in the centre of the canvas
        const float mx = cx_ - 6.9f * unit_;
        const float my = cy_ + 6.9f * unit_;
        gearBody(mx, my, spinAngle, 8, 42, 30, 11, kColorYellow);
        const float sx = mx + 36.8f * unit_;
        const float sy = my - 36.8f * unit_;
        gearBody(sx, sy, -spinAngle * (8.0f / 6.0f) + kPi / 6.0f, 6, 19, 13, 4, kColorYellow);
    } else {
        uint16_t color = (style == GearStyle::Compacting) ? kColorGrey : kColorYellow;
        gearBody(cx_, cy_, spinAngle, 8, 52, 38, 14, color);
    }
}

void Icons::waiting(float angle, float scale){
    sign(kColorRed, angle, scale);

    // Exclamation mark: a pill-shaped bar and a dot
    thickLine(rotated(0, -28, angle, scale), rotated(0, 6, angle, scale), 13 * scale, kColorWhite);
    Pt dot = rotated(0, 26, angle, scale);
    canvas_.fillCircle(px(dot.x), px(dot.y), px(7 * scale * unit_), kColorWhite);
}

void Icons::question(float angle, float scale){
    sign(kColorBlue, angle, scale);

    // Question mark: a 260-degree hook, a short stem and a dot
    thickArc(0, -14, 16, 190, 450, 12, kColorWhite, angle, scale);
    thickLine(rotated(0, 2, angle, scale), rotated(0, 8, angle, scale), 12 * scale, kColorWhite);
    Pt dot = rotated(0, 27, angle, scale);
    canvas_.fillCircle(px(dot.x), px(dot.y), px(7 * scale * unit_), kColorWhite);
}

void Icons::error(float angle, float scale){
    canvas_.fillSprite(kColorBg);
    canvas_.fillCircle(px(cx_), px(cy_), px(56 * scale * unit_), kColorRed);

    // Cross as two thick white strokes, the mirror image of the check
    thickLine(rotated(-21, -21, angle, scale), rotated(21, 21, angle, scale), 13 * scale, kColorWhite);
    thickLine(rotated(-21, 21, angle, scale), rotated(21, -21, angle, scale), 13 * scale, kColorWhite);
}

void Icons::paused(float angle, const Hourglass& h){
    canvas_.fillSprite(kColorBg);

    // The whole hourglass turns around the centre during the flip
    angle += h.flip * kPi;

    const float kPileFull = 18.0f;   // height of the sand pile when all of it is in one bulb
    const float kMound    = 6.0f;    // extra height of the pile's peak at the centre

    // Glass interiors: a dark tint so the empty part of a bulb still reads as glass
    fillBulb(1, kBulbFrame, kBulbNeck, kColorGlass, angle);
    fillBulb(-1, kBulbFrame, kBulbNeck, kColorGlass, angle);

    // Top bulb: sand resting on the neck, level falling as it drains. Right
    // after a turn the sand is still at the wide end and slides down (settle)
    float topLevel = kBulbNeck - 29.0f * (1.0f - h.drained) * h.settle;
    fillBulb(1, topLevel, kBulbNeck, kColorSand, angle);
    if (h.settle < 1.0f){
        float hang = kBulbFrame + kPileFull * (1.0f - h.settle);
        fillBulb(1, kBulbFrame, hang, kColorSand, angle);
        float w = 0.75f * -wallX(hang);
        tri(rotated(-w, hang, angle), rotated(w, hang, angle),
            rotated(0, hang + kMound * (1.0f - h.settle), angle), kColorSand);
    }
    if (h.falling && kBulbNeck - topLevel > 6.0f){
        // Funnel-shaped dip where the sand runs out
        float w = 0.5f * -wallX(topLevel);
        tri(rotated(-w, topLevel, angle), rotated(w, topLevel, angle),
            rotated(0, topLevel + 4.0f, angle), kColorGlass);
    }

    // Bottom bulb: the pile grows from the wide end, with a peak at the centre
    float pileLevel = kBulbFrame + kPileFull * h.drained;
    fillBulb(-1, kBulbFrame, pileLevel, kColorSand, angle);
    float peak = pileLevel + kMound * h.drained;
    if (h.drained > 0.05f){
        float w = 0.75f * -wallX(pileLevel);
        tri(rotated(-w, -pileLevel, angle), rotated(w, -pileLevel, angle),
            rotated(0, -peak, angle), kColorSand);
    }

    // Stream through the neck, down to the top of the pile
    if (h.falling && h.drained < 1.0f){
        thickLine(rotated(0, kBulbNeck, angle), rotated(0, -peak, angle), 3, kColorSand);
    }

    // Glass walls on top of the sand, then the frame bars
    for (float sy : {1.0f, -1.0f}){
        for (int i = 0; i < kWallPoints - 1; i++){
            Pt a = rotated(kWall[i][0], kWall[i][1] * sy, angle);
            Pt b = rotated(kWall[i + 1][0], kWall[i + 1][1] * sy, angle);
            thickLine(a, b, 4, kColorWhite);
            Pt a2 = rotated(-kWall[i][0], kWall[i][1] * sy, angle);
            Pt b2 = rotated(-kWall[i + 1][0], kWall[i + 1][1] * sy, angle);
            thickLine(a2, b2, 4, kColorWhite);
        }
    }
    thickLine(rotated(-31, -42, angle), rotated(31, -42, angle), 9, kColorAmber);
    thickLine(rotated(-31, 42, angle), rotated(31, 42, angle), 9, kColorAmber);
}

void Icons::done(float angle){
    canvas_.fillSprite(kColorBg);
    canvas_.fillCircle(px(cx_), px(cy_), px(56 * unit_), kColorGreen);

    // Check mark as two thick white strokes
    thickLine(rotated(-27, 2, angle), rotated(-8, 23, angle), 13, kColorWhite);
    thickLine(rotated(-8, 23, angle), rotated(30, -21, angle), 13, kColorWhite);
}

void Icons::blank(){
    canvas_.fillSprite(kColorBg);
}

}  // namespace monitor
