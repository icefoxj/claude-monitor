#pragma once

#include <M5Unified.h>

#include <cstdint>

namespace monitor {

// Procedural icons drawn into a square canvas and pushed to the display in
// one go (drawing straight to the panel flickers). All geometry is defined
// for a 128 px canvas and scaled by size/128, so the same drawings work on
// any square sprite. No bitmaps: triangle fans, circles and round-capped
// strokes only.
//
// The static icons take `angle`, the clockwise rotation (radians) that keeps
// them upright under tilt. The two "needs you" signs also take `scale`, used
// by the entry pulse. The gear spins on its own and ignores the tilt.
class Icons {
public:
    // `canvas` must already be created as a size x size sprite. The sprite
    // is pushed at (pushX, pushY), which lets a board centre it on a panel
    // larger than the icon.
    Icons(M5Canvas& canvas, int size, int pushX = 0, int pushY = 0);

    void gear(float spinAngle);
    void waiting(float angle, float scale = 1.0f);
    void question(float angle, float scale = 1.0f);
    void done(float angle);
    void blank();

private:
    struct Pt { float x; float y; };

    // A point given relative to the centre in 128-space, scaled, rotated by
    // `angle` (clockwise on screen) and converted to canvas pixels
    Pt rotated(float x, float y, float angle, float scale = 1.0f) const;

    // Thick stroke with rounded caps between two canvas points; thickness in 128-space
    void thickLine(Pt a, Pt b, float thickness, uint16_t color);

    // Thick arc as a polyline of round-capped segments. Centre and radius in
    // 128-space; angles in screen degrees (0 = right, 90 = down), before rotation
    void thickArc(float cx, float cy, float r, float a0, float a1,
                  float thickness, uint16_t color, float angle, float scale);

    // Filled regular octagon centred on the canvas, radius in 128-space. At
    // angle 0 the flat sides sit at the top and bottom, like a road sign
    void octagon(float radius, float angle, uint16_t color);

    // Road sign background: white border, coloured body
    void sign(uint16_t body, float angle, float scale);

    void push();

    M5Canvas& canvas_;
    float unit_;        // canvas pixels per 128-space unit
    float cx_, cy_;     // canvas centre in pixels
    int pushX_, pushY_;
};

}  // namespace monitor
