// FreeRTOS first: under ESP-IDF, FreeRTOS.h must be included before
// any header that pulls in task.h (M5Unified does)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <M5Unified.h>

#include <driver/usb_serial_jtag.h>
#include <esp_err.h>
#include <esp_log.h>

#include <cmath>
#include <cstdio>
#include <string>

// Serial protocol: one command per line, terminated by \n
//   "processing"   -> spinning yellow gear (Claude processing)
//   "waiting_user" -> red sign with an exclamation mark (waiting for a permission)
//   "question"     -> blue sign with a question mark (Claude asked you something)
//   "idle"         -> green circle with check (idle)
//   "off"          -> screen off
//   "status"       -> device replies with one STATUS line (debug/calibration)

namespace{
    constexpr const char* TAG = "monitor";
    constexpr float kPi = 3.14159265f;
    constexpr int kWidth = 128;
    constexpr int kHeight = 128;
    constexpr float kCx = kWidth * 0.5f;
    constexpr float kCy = kHeight * 0.5f;

    constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
        return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    constexpr uint16_t kColorBg = rgb565(0,0,0);
    constexpr uint16_t kColorRed = rgb565(210,35,35);
    constexpr uint16_t kColorBlue = rgb565(35,120,225);
    constexpr uint16_t kColorYellow = rgb565(255,200,0);
    constexpr uint16_t kColorGreen = rgb565(60,180,75);
    constexpr uint16_t kColorWhite = rgb565(255,255,255);

    enum class State {Idle, Processing, WaitingUser, Question, Off};

    // States that mean "Claude needs you": they get an entry pulse
    bool needsAttention(State s){
        return s == State::WaitingUser || s == State::Question;
    }

    // States drawn once and then only redrawn when the tilt changes
    bool isStatic(State s){
        return s == State::Idle || s == State::WaitingUser || s == State::Question;
    }

    // Framebuffer: every frame is composed here and pushed in one go
    M5Canvas canvas(&M5.Display);

    // ---------------- helper primitives ----------------

    inline int px(float v){ return static_cast<int>(lroundf(v)); }

    struct Pt { float x; float y; };

    // A point given relative to the canvas centre, scaled, rotated by
    // `angle` radians (clockwise on screen) and translated to canvas coordinates
    Pt rotated(float x, float y, float angle, float scale = 1.0f){
        x *= scale;
        y *= scale;
        float c = cosf(angle);
        float s = sinf(angle);
        return { kCx + x * c - y * s, kCy + x * s + y * c };
    }

    // Thick stroke with rounded caps: an oriented rectangle (2 triangles)
    // plus a circle at each end
    void thickLine(Pt a, Pt b, float thickness, uint16_t color){
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float length = sqrtf(dx*dx + dy*dy);
        if (length < 0.001f){
            return;
        }
        float hx = -dy / length * (thickness * 0.5f);
        float hy =  dx / length * (thickness * 0.5f);

        canvas.fillTriangle(px(a.x+hx), px(a.y+hy), px(b.x+hx), px(b.y+hy), px(b.x-hx), px(b.y-hy), color);
        canvas.fillTriangle(px(a.x+hx), px(a.y+hy), px(b.x-hx), px(b.y-hy), px(a.x-hx), px(a.y-hy), color);
        canvas.fillCircle(px(a.x), px(a.y), px(thickness * 0.5f), color);
        canvas.fillCircle(px(b.x), px(b.y), px(thickness * 0.5f), color);
    }

    // Thick arc as a polyline of round-capped segments. Centre and radius
    // are relative to the canvas centre; angles in screen degrees
    // (0 = right, 90 = down, i.e. clockwise), before the icon rotation
    void thickArc(float cx, float cy, float r, float a0, float a1,
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

    // Filled regular octagon centred on the canvas. At angle 0 the flat
    // sides sit at the top and bottom, like a road sign; `angle` rotates it
    void octagon(float radius, float angle, uint16_t color){
        Pt v[8];
        for (int i = 0; i < 8; i++){
            float a = (22.5f + 45.0f * i) * kPi / 180.0f + angle;
            v[i] = { kCx + radius * cosf(a), kCy + radius * sinf(a) };
        }
        for (int i = 1; i < 7; i++){
            canvas.fillTriangle(px(v[0].x), px(v[0].y), px(v[i].x), px(v[i].y), px(v[i+1].x), px(v[i+1].y), color);
        }
    }

    // ---------------- icons ----------------
    // The static icons take `angle`, the clockwise rotation that keeps them
    // upright for the current tilt of the cube. The two "needs you" signs
    // also take `scale`, used by the entry pulse. The gear spins on its own
    // and ignores the tilt.

    // Road sign: white border, coloured body
    void sign(uint16_t body, float angle, float scale){
        canvas.fillSprite(kColorBg);
        octagon(58 * scale, angle, kColorWhite);
        octagon(51 * scale, angle, body);
    }

    void drawWaitingIcon(float angle, float scale){
        sign(kColorRed, angle, scale);

        // Exclamation mark: a pill-shaped bar and a dot
        thickLine(rotated(0, -28, angle, scale), rotated(0, 6, angle, scale), 13 * scale, kColorWhite);
        Pt dot = rotated(0, 26, angle, scale);
        canvas.fillCircle(px(dot.x), px(dot.y), px(7 * scale), kColorWhite);

        canvas.pushSprite(0,0);
    }

    void drawQuestionIcon(float angle, float scale){
        sign(kColorBlue, angle, scale);

        // Question mark: a 260-degree hook, a short stem and a dot
        thickArc(0, -14, 16, 190, 450, 12, kColorWhite, angle, scale);
        thickLine(rotated(0, 2, angle, scale), rotated(0, 8, angle, scale), 12 * scale, kColorWhite);
        Pt dot = rotated(0, 27, angle, scale);
        canvas.fillCircle(px(dot.x), px(dot.y), px(7 * scale), kColorWhite);

        canvas.pushSprite(0,0);
    }

    void drawGearIcon(float gearAngle){
        canvas.fillSprite(kColorBg);
        int cx = kWidth / 2;
        int cy = kHeight / 2;

        const int teeth = 8;
        const float rTip = 52.0f;
        const float rBody = 38.0f;
        const float rHole = 14.0f;
        const float step = 2.0f * kPi / teeth;

        // Each tooth is a radial trapezoid: wide base at the body,
        // narrower tip; decomposed into 2 triangles
        for (int i = 0; i < teeth; i++){
            float a = gearAngle + i * step;
            float wb = step * 0.28f;
            float wt = step * 0.16f;
            float b0x = cx + rBody * cosf(a - wb);
            float b0y = cy + rBody * sinf(a - wb);
            float b1x = cx + rBody * cosf(a + wb);
            float b1y = cy + rBody * sinf(a + wb);
            float t0x = cx + rTip * cosf(a - wt);
            float t0y = cy + rTip * sinf(a - wt);
            float t1x = cx + rTip * cosf(a + wt);
            float t1y = cy + rTip * sinf(a + wt);

            canvas.fillTriangle(b0x,b0y,b1x,b1y,t1x,t1y,kColorYellow);
            canvas.fillTriangle(b0x,b0y,t1x,t1y,t0x,t0y,kColorYellow);
        }
        canvas.fillCircle(cx,cy,rBody,kColorYellow);
        canvas.fillCircle(cx,cy,rHole,kColorBg);

        canvas.pushSprite(0,0);
    }

    void drawDoneIcon(float angle){
        canvas.fillSprite(kColorBg);
        canvas.fillCircle(kWidth / 2, kHeight / 2, 56, kColorGreen);

        // Check mark as two thick white strokes
        thickLine(rotated(-27, 2, angle), rotated(-8, 23, angle), 13, kColorWhite);
        thickLine(rotated(-8, 23, angle), rotated(30, -21, angle), 13, kColorWhite);

        canvas.pushSprite(0,0);
    }

    void drawBlankScreen(){
        canvas.fillSprite(kColorBg);
        canvas.pushSprite(0,0);
    }

    // ---------------- orientation ----------------

    // Boot orientation, in 90-degree clockwise steps added to the display's
    // default rotation. This is the frame all icons are drawn in, and what
    // is shown while the cube lies flat on the desk.
    constexpr uint8_t kBootRotationOffset = 1;

    // Tilt -> icon angle. atan2(ay, ax) is the direction the accelerometer
    // reports as "up" in the sensor frame; the sign and offset absorb how
    // the BMI270 is mounted relative to the panel. Calibrate with the
    // "status" serial command, which reports the angle in use.
    // On this unit: sign -1, offset 0.
    constexpr float kImuAngleSign = -1.0f;
    constexpr float kImuAngleOffset = 0.0f;   // radians

    constexpr float kFlatThreshold = 0.80f;   // |az| above this = lying flat: hold the angle
    constexpr float kTiltThreshold = 0.40f;   // in-plane |g| below this = too noisy: hold
    constexpr float kAngleSmoothing = 0.25f;  // per-frame low-pass factor (~130 ms at 30 fps)
    constexpr float kRedrawStep = 1.0f * kPi / 180.0f;   // redraw once the angle moved this much

    float wrapAngle(float a){
        while (a >  kPi) a -= 2.0f * kPi;
        while (a < -kPi) a += 2.0f * kPi;
        return a;
    }

    struct Tilt {
        float angle = 0.0f;   // smoothed icon angle, radians clockwise
        float drawn = 0.0f;   // angle of the icon currently on screen
        bool  valid = false;  // a usable reading has been seen
    };

    // Feed one accelerometer sample. Returns true when the icon on screen
    // is stale enough to be redrawn.
    bool updateTilt(Tilt& t, float ax, float ay, float az){
        if (fabsf(az) < kFlatThreshold && sqrtf(ax*ax + ay*ay) > kTiltThreshold){
            float target = wrapAngle(kImuAngleSign * atan2f(ay, ax) + kImuAngleOffset);
            if (!t.valid){
                t.angle = target;   // first reading: snap, no easing from zero
                t.valid = true;
            } else {
                t.angle = wrapAngle(t.angle + wrapAngle(target - t.angle) * kAngleSmoothing);
            }
        }
        return fabsf(wrapAngle(t.angle - t.drawn)) >= kRedrawStep;
    }

    // ---------------- attention pulse ----------------
    // When a "needs you" sign appears it breathes three times over ~2 s so
    // the change catches the eye from across the desk

    constexpr int   kAttentionFrames = 60;    // total length at ~30 fps
    constexpr int   kAttentionPeriod = 20;    // frames per pulse
    constexpr float kAttentionAmp    = 0.10f; // +10 % size at the peak

    // ---------------- state machine ----------------

    struct Ui {
        State state     = State::Off;
        float gearAngle = 0.0f;
        Tilt  tilt;
        int   attention = 0;   // pulse frames left, 0 = idle
    };

    void renderState(Ui& ui, float scale = 1.0f){
        switch(ui.state){
            case State::Processing:
                drawGearIcon(ui.gearAngle);
                break;
            case State::WaitingUser:
                drawWaitingIcon(ui.tilt.angle, scale);
                ui.tilt.drawn = ui.tilt.angle;
                break;
            case State::Question:
                drawQuestionIcon(ui.tilt.angle, scale);
                ui.tilt.drawn = ui.tilt.angle;
                break;
            case State::Idle:
                drawDoneIcon(ui.tilt.angle);
                ui.tilt.drawn = ui.tilt.angle;
                break;
            case State::Off:
                drawBlankScreen();
                break;
        }
    }

    void applyState(State next, Ui& ui){
        if (next == ui.state){
            return;   // avoids needless redraws of the static icons
        }
        ui.state = next;
        if (next == State::Processing){
            ui.gearAngle = 0.0f;
        }
        ui.attention = needsAttention(next) ? kAttentionFrames : 0;
        renderState(ui);
    }

    void handleCommand(const std::string& cmd, Ui& ui){
        if (cmd == "processing"){
            applyState(State::Processing, ui);
        } else if (cmd == "waiting_user"){
            applyState(State::WaitingUser, ui);
        } else if (cmd == "question"){
            applyState(State::Question, ui);
        } else if (cmd == "idle"){
            applyState(State::Idle, ui);
        } else if (cmd == "off"){
            applyState(State::Off, ui);
        }
        // Unknown command: silently ignore
    }

    const char* stateName(State s){
        switch(s){
            case State::Processing:  return "processing";
            case State::WaitingUser: return "waiting_user";
            case State::Question:    return "question";
            case State::Idle:        return "idle";
            case State::Off:         return "off";
        }
        return "?";
    }

    // Reply to the "status" command straight through the USB driver, so it
    // works regardless of where the ESP-IDF console is routed
    void sendStatus(const Ui& ui, uint8_t rotation, float ax, float ay, float az){
        char msg[112];
        int n = snprintf(msg, sizeof(msg),
                         "STATUS state=%s rot=%u angle=%.1f ax=%.2f ay=%.2f az=%.2f\n",
                         stateName(ui.state), rotation, ui.tilt.angle * 180.0f / kPi, ax, ay, az);
        if (n > 0){
            usb_serial_jtag_write_bytes(msg, n, pdMS_TO_TICKS(20));
        }
    }
}

extern "C" void app_main(void){
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(128);

    // 128x128 framebuffer at 16 bpp = 32 KB of RAM
    canvas.setColorDepth(16);
    canvas.createSprite(kWidth,kHeight);

    // Boot orientation: apply the calibration offset before the first draw
    bool imuOK = M5.Imu.isEnabled();
    uint8_t rotation = (M5.Display.getRotation() + kBootRotationOffset) & 3;
    M5.Display.setRotation(rotation);
    ESP_LOGI(TAG, "imu %s, boot rotation %u", imuOK ? "enabled" : "absent", rotation);

    Ui ui;
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    if (imuOK){
        // Prime the tilt so the first frame is already upright
        M5.Imu.update();
        M5.Imu.getAccel(&ax,&ay,&az);
        updateTilt(ui.tilt, ax, ay, az);
    }

    applyState(State::Idle, ui);   // initial state

    // USB Serial/JTAG driver, to receive data from the computer
    usb_serial_jtag_driver_config_t usbCfg = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usbCfg));

    std::string line;
    uint8_t buf[64];
    bool screenOn = true;

    while(true){
        M5.update();   // refreshes the button state

        // Tilt tracking: the static icons follow the gravity vector
        // (while a pulse is running it redraws every frame anyway)
        if (imuOK){
            M5.Imu.update();
            M5.Imu.getAccel(&ax,&ay,&az);
            bool stale = updateTilt(ui.tilt, ax, ay, az);
            if (stale && ui.attention == 0 && isStatic(ui.state)){
                renderState(ui);
            }
        }

        // The 33 ms timeout blocks waiting for data (no busy-wait) and sets
        // the ~30 fps pace of the animations and the tilt filter
        int bytesRead = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(33));

        for (int i = 0; i < bytesRead; i++){
            char c = static_cast<char>(buf[i]);
            if (c == '\n'){
                // Strip trailing \r and spaces ("processing\r\n" from some senders)
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')){
                    line.pop_back();
                }
                if (line == "status"){
                    sendStatus(ui, rotation, ax, ay, az);
                } else {
                    handleCommand(line, ui);
                }
                line.clear();
            } else {
                line += c;
                if (line.size() > 64){
                    line.clear();   // protection against serial garbage
                }
            }
        }

        // Animation: only the "processing" state redraws every frame
        if (ui.state == State::Processing && screenOn){
            ui.gearAngle += 0.10f;   // ~3 s per full turn at 30 fps
            if (ui.gearAngle >= 2.0f * kPi){
                ui.gearAngle -= 2.0f * kPi;
            }
            drawGearIcon(ui.gearAngle);
        }

        // Entry pulse of the "needs you" signs: the last frame lands on scale 1
        if (ui.attention > 0){
            if (screenOn){
                int k = kAttentionFrames - ui.attention + 1;   // 1..kAttentionFrames
                --ui.attention;
                float scale = (ui.attention == 0)
                    ? 1.0f
                    : 1.0f + kAttentionAmp * fabsf(sinf(kPi * k / kAttentionPeriod));
                renderState(ui, scale);
            } else {
                ui.attention = 0;   // screen is dark: nothing to animate
            }
        }

        // Screen button: toggles the display on and off
        if (M5.BtnA.wasPressed()){
            screenOn = !screenOn;
            M5.Display.setBrightness(screenOn ? 128 : 0);
        }
    }
}
