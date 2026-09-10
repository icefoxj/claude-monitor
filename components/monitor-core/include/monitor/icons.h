#pragma once

#include <M5Unified.h>

#include <cstdint>

namespace monitor {

// How the gear is drawn
enum class GearStyle {
    Working,     // yellow: Claude is processing
    Compacting,  // grey: housekeeping, back soon
    Subagents,   // yellow with a smaller counter-rotating gear: subagents running
};

// Animation phase of the hourglass, computed by the state machine
struct Hourglass {
    float drained = 0.0f;   // 0 = top bulb full, 1 = all the sand in the bottom bulb
    float flip    = 0.0f;   // 0..1 progress of the 180-degree turn (0 = upright)
    float settle  = 1.0f;   // 0..1 sand sliding from the wide end to the neck after a turn
    bool  falling = false;  // draw the stream through the neck
};

// Procedural icons drawn into a square canvas and pushed to the display in
// one go (drawing straight to the panel flickers). All geometry is defined
// for a 128 px canvas and scaled by size/128, so the same drawings work on
// any square sprite. No bitmaps: triangle fans, circles and round-capped
// strokes only.
//
// The static icons take `angle`, the clockwise rotation (radians) that keeps
// them upright under tilt. The "look at the terminal" icons also take
// `scale`, used by the entry pulse. The gears spin on their own and ignore
// the tilt.
class Icons {
public:
    // `canvas` must already be created as a size x size sprite. The sprite
    // is pushed at (pushX, pushY), which lets a board centre it on a panel
    // larger than the icon.
    Icons(M5Canvas& canvas, int size, int pushX = 0, int pushY = 0);

    void gear(float spinAngle, GearStyle style = GearStyle::Working);
    void waiting(float angle, float scale = 1.0f);    // red sign, "!"
    void question(float angle, float scale = 1.0f);   // blue sign, "?"
    void error(float angle, float scale = 1.0f);      // red circle, cross
    void paused(float angle, const Hourglass& h);     // amber hourglass, animated
    void done(float angle);                           // green circle, check
    void blank();

private:
    struct Pt { float x; float y; };

    // A point given relative to the centre in 128-space, scaled, rotated by
    // `angle` (clockwise on screen) and converted to canvas pixels
    Pt rotated(float x, float y, float angle, float scale = 1.0f) const;

    void tri(Pt a, Pt b, Pt c, uint16_t color);

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

    // One gear: centre in canvas pixels, radii in 128-space
    void gearBody(float cx, float cy, float spin, int teeth,
                  float rTip, float rBody, float rHole, uint16_t color);

    // Fills the slice of an hourglass bulb between two heights. Heights are
    // in the top bulb's coordinates (-37 at the frame, -1 at the neck);
    // sy = +1 draws the top bulb, -1 the bottom one
    void fillBulb(float sy, float ya, float yb, uint16_t color, float angle);

    void push();

    M5Canvas& canvas_;
    float unit_;        // canvas pixels per 128-space unit
    float cx_, cy_;     // canvas centre in pixels
    int pushX_, pushY_;
};

}  // namespace monitor
