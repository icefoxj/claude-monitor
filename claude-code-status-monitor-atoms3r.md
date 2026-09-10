# A physical traffic light for Claude Code: a status monitor with the M5Stack AtomS3R, VS Code and ESP-IDF

> **Note (September 2026).** This is the original write-up and it is kept as the design rationale. The code in this repository has moved on since: the red STOP sign became an exclamation mark, there are nine states instead of three (a blue question mark, an error cross, an animated hourglass, a grey compacting gear, a subagent satellite), the icons rotate continuously with the tilt, a host daemon receives the hooks over HTTP and tracks several sessions, and the sources are split into a shared component and per-board projects. The [README](README.md) describes the current firmware and hook setup; [CHANGELOG.md](CHANGELOG.md) lists what each release changed.

Anyone who works with Claude Code daily knows the pattern: you kick off a task, switch to another window — email, documentation, another terminal — and keep coming back every thirty seconds to see whether it has finished or is sitting there waiting for your permission. This article shows how to build a small desk device that solves this: a USB monitor that displays a different icon depending on what Claude Code is doing.

- **Spinning yellow gear** — Claude Code is processing your request
- **Red STOP sign** — Claude Code is waiting for something from you (a permission, an answer)
- **Green circle with a check mark** — Claude Code is done; idle, ready for the next request

The hardware is a single 24×24 mm module, the **M5Stack AtomS3R**, connected over USB-C. No breadboard, no resistors, no Wi-Fi, no soldering. The module's color IPS display — **128×128 pixels** at 0.85" — has more than enough resolution and color depth for well-drawn icons, including animation. And because the module has a built-in IMU, the icon always stays upright — rest the cube on any of its four sides.

The development environment is **Visual Studio Code with Espressif's official ESP-IDF extension** — no PlatformIO and no Arduino IDE. That means programming against the ESP32's native framework (C/C++, CMake, FreeRTOS), which is Espressif's official path and the one that gives you the most control over the chip.

The total cost is the AtomS3R itself (roughly €15–20 depending on the vendor — check current pricing) and the build takes an afternoon.

---

## 1. How the project works

Before touching the hardware, it's worth understanding the architecture, because there is a common conceptual trap in this kind of project.

**What does not work:** querying the Anthropic API to learn Claude Code's state. The API does not expose Claude Code session state — there is no endpoint that answers "processing" or "waiting". That state lives exclusively on the machine where Claude Code is running.

**What works:** inverting the flow. Claude Code has a mechanism called **hooks** — user-defined shell commands that it runs automatically at specific points in its lifecycle. It's Claude Code that notifies the device, not the device that polls.

The complete flow looks like this:

```
┌──────────────┐   lifecycle event   ┌──────────────┐   text line      ┌──────────────┐
│  Claude Code │ ──────────────────► │ hook (shell) │ ───over USB────► │   AtomS3R    │
│              │  UserPromptSubmit   │ echo > serial│  "processing\n"  │  draws the   │
│              │  Notification       │     port     │                  │  icon for    │
│              │  Stop               │              │                  │  the state   │
└──────────────┘                     └──────────────┘                  └──────────────┘
```

Three hook events map directly onto the three states:

| Claude Code event | When it fires | Icon |
|---|---|---|
| `UserPromptSubmit` | You submit a prompt; processing begins | Spinning yellow gear |
| `Notification` | Claude needs your attention (permission request, waiting for input) | Red STOP sign |
| `Stop` | Claude has finished responding | Green circle with check |
| `SessionEnd` | The session ends | Screen off |

The protocol between the computer and the AtomS3R is deliberately primitive: a text line terminated by `\n` over the USB serial port. No JSON, no handshake. The simpler the protocol, the fewer things break.

> **Verification note:** the event names above were checked against the official hooks documentation (link in the references section). Anthropic has been expanding this area frequently — recent versions mention additional events such as `PermissionRequest`, which may be an even more precise trigger for the red state. Before configuring, open the documentation and check the current event list for your version.

---

## 2. The hardware: why the AtomS3R

The AtomS3R is a programmable controller from M5Stack based on the ESP32-S3-PICO-1-N8R8, with 8 MB of Flash, 8 MB of PSRAM, a 0.85" color IPS display (**128×128 px**) with a programmable button beneath the screen, and a USB Type-C connector that handles both power and firmware flashing. It measures 24×24×12.9 mm.

For this project, what matters:

1. **Built-in color display** — 128×128 pixels are enough for detailed icons and smooth animation.
2. **The ESP32-S3's native USB** — the chip speaks USB directly through its USB Serial/JTAG peripheral, with no intermediate USB-UART converter. The computer sees a virtual serial port, and opening/closing that port **does not reset the board** (there is no DTR auto-reset circuit — which is exactly why entering flashing mode for the first time requires holding the physical reset button).
3. **Button under the screen** — you get a display on/off button for free.
4. **Built-in IMU (BMI270)** — the accelerometer tells the firmware which way is down, so the image auto-rotates to stay upright in any position.
5. **Form factor** — it's a tiny cube that looks good on any desk; M5Stack sells mounting accessories if you want to clip it to your monitor.

Wi-Fi and the magnetometer (BMM150) exist on the module but won't be used here.

---

## 3. The environment: VS Code + the official ESP-IDF extension

A common question: "isn't there an official ESP32 plugin for VS Code?" There is — with one important nuance. Espressif's official plugin is the **ESP-IDF extension** (`espressif.esp-idf-extension`, available on the VS Code marketplace), and it works with the native **ESP-IDF** framework, not with Arduino. There is no official Arduino extension for VS Code: the one Microsoft maintained was discontinued and its repository archived. So "VS Code without PlatformIO" in practice means adopting ESP-IDF — and that's what we'll do.

What this changes compared to an Arduino sketch:

- The code is standard C/C++ with CMake; the entry point is `app_main()` instead of `setup()`/`loop()`.
- The system runs on FreeRTOS explicitly (it's there under Arduino too, just hidden).
- There is no Arduino `Serial` object — USB communication uses the ESP32-S3's native **USB Serial/JTAG** driver.
- Libraries come from the **ESP Component Registry** (ESP-IDF's "NuGet"), declared in a dependency manifest.

And the news that makes it all viable: the **M5Unified** library — which abstracts the AtomS3R's display, button and backlight — **officially supports ESP-IDF** and is published in the Component Registry as `m5stack/m5unified`, with the M5ATOMS3R on its supported-device list. It brings **M5GFX** along with it, the graphics library with the primitives (triangles, circles, sprites) we'll use to draw the icons.

### 3.1 Installing the extension and the framework

1. **Install the extension.** In VS Code, open the extensions panel (`Ctrl+Shift+X`), search for **ESP-IDF** and install the extension published by **Espressif Systems**.

2. **Install the framework.** Open the command palette (`Ctrl+Shift+P`) and run **`ESP-IDF: Open ESP-IDF Installation Manager`**. The extension downloads and runs the **EIM (ESP-IDF Installation Manager)**, the official installer that takes care of the framework, the toolchains (the xtensa compiler for the S3), Python and OpenOCD. Choose the standard installation with the **latest stable release of the 5.x series** — the ESP32-S3 has been supported since 4.4, but the current ecosystem assumes 5.x.

   Two classic installation traps: paths **with no spaces and no accented characters** (the installer is sensitive to this), and, if you've had an old ESP-IDF installation on the machine, prefer letting EIM create a clean one over reusing the old one.

3. **Linux — serial port permission.** Your user needs to belong to the group that owns the serial ports (usually `dialout`, on some distributions `uucp`):

   ```bash
   sudo usermod -aG dialout $USER
   ```

   Log out and back in afterwards.

When it's done, the VS Code status bar gains a strip of extension buttons: port selection, target selection, build (cylinder icon), flash (lightning bolt) and monitor (screen). You can live on the command palette alone, but those buttons cover 95% of the cycle.

### 3.2 Creating the project

1. Command palette → **`ESP-IDF: New Project`**. The wizard asks which ESP-IDF installation to use, the name (`claude-monitor`, for example), the directory and a template — pick the simplest one (`sample_project` or `hello_world`; we'll replace the contents anyway). When it finishes, it opens the project with the `.vscode` folder already configured. Put the project itself in a path **without spaces** too (`E:\work\claude-monitor`, not `E:\work\Claude Monitor`) — ESP-IDF's build tooling has a long history of breaking on spaces in project paths.

2. **Set the target.** Palette → **`ESP-IDF: Set Espressif Device Target`** → **esp32s3**. This regenerates the configuration for the correct toolchain. (Equivalent, in the activated ESP-IDF terminal: `idf.py set-target esp32s3` — same environment caveat as step 3.)

3. **Add M5Unified.** Run `idf.py` in a terminal where the ESP-IDF environment is active. The safe route on every OS is the extension's own terminal — palette → **`ESP-IDF: Open ESP-IDF Terminal`** — which opens the system shell (**PowerShell, on Windows**) with everything already loaded. In it, run the command **directly, with no prefix**:

   ```powershell
   idf.py add-dependency "m5stack/m5unified^0.2"
   ```

   Three Windows-specific notes:

   - **Never prefix the command with `bash`.** On Windows, `bash` hands the line over to WSL, whose shell then tries to execute `idf.py` — a Python script — as if it were a shell script. The result is a cascade of `import: command not found` errors (and `$'\r'` complaints, which are bash choking on Windows line endings). `idf.py` is Python and must be run by the Python of the activated environment, which the ESP-IDF terminal already puts first on the PATH.
   - If you prefer a standalone PowerShell (outside VS Code), activate the environment first: use the "ESP-IDF PowerShell" shortcut the installer creates in the Start Menu, or run the activation script from your installation (for an ESP-IDF in `D:\espressif\v5.5\esp-idf`, that's `D:\espressif\v5.5\esp-idf\export.ps1`). Only then does `idf.py` exist in the session. The script's exact name and location can vary with the installer version — the Start Menu shortcut is the low-friction path.
   - The quotes around `"m5stack/m5unified^0.2"` matter in PowerShell too: they keep the `^` literal (and in cmd, which treats `^` as an escape character, they are what saves it).

   This creates/updates the `main/idf_component.yml` file — the dependency manifest. On the first build, ESP-IDF downloads M5Unified and M5GFX (its dependency) from the Component Registry into the `managed_components/` folder. If you'd rather write the manifest by hand, it looks like this:

   ```yaml
   dependencies:
     idf: ">=5.0"
     m5stack/m5unified: "^0.2"
   ```

   Writing the manifest by hand is also the zero-terminal escape hatch: on the first build, ESP-IDF resolves and downloads the dependencies on its own — no `idf.py add-dependency` involved.

4. **Configure the SDK.** Palette → **`ESP-IDF: SDK Configuration Editor (menuconfig)`**. Two settings:

   - **Serial flasher config → Flash size → 8 MB** (the AtomS3R has 8 MB; the template default is usually 2 MB).
   - **Component config → ESP System Settings → Channel for console output → USB Serial/JTAG Controller**. This is the critical one: since the AtomS3R has no USB-UART converter chip, if the console stays on the default (UART0) you simply won't see any log output over USB. With USB Serial/JTAG, both the system logs and our protocol travel over the same virtual port.

   The 8 MB of PSRAM is not needed for this project — leave it disabled, which avoids an entire class of settings (octal mode, frequency) that add nothing here.

Save and close the configuration editor.

### 3.3 The project structure

At the end, the relevant tree is this:

```
claude-monitor/
├── CMakeLists.txt              # root — comes from the template, leave it alone
├── sdkconfig                   # generated by menuconfig
├── managed_components/         # M5Unified + M5GFX, downloaded (generated)
└── main/
    ├── CMakeLists.txt          # registers the app sources
    ├── idf_component.yml       # dependency manifest
    └── main.cpp                # our code (rename the template's main.c)
```

The `main/CMakeLists.txt` must point to the `.cpp` file:

```cmake
idf_component_register(SRCS "main.cpp")
```

Note: **the main file is `.cpp`**, not `.c` — M5Unified is C++. Delete the template's `main.c` so you don't end up with a duplicate `app_main`.

---

## 4. The firmware

The firmware has two clearly separated halves: **communication** (accumulate bytes from USB until a `\n`, interpret the line, switch state) and **drawing** (render each state's icon, with animation in the gear's case). The drawing decisions are worth explaining before the code.

### 4.1 How the icons are drawn

**An in-memory canvas, not direct-to-screen drawing.** Animating by drawing directly on the display produces flicker: every frame starts by clearing the screen, and the eye sees the "clear-then-draw". The classic solution is a *framebuffer*: a 128×128 sprite in RAM (M5GFX's `M5Canvas` class), where each frame is fully composed and then pushed to the display in one go with `pushSprite`. The cost: 128 × 128 × 2 bytes (16 bits per pixel) = **32 KB of RAM** — comfortable for the ESP32-S3 even without PSRAM.

**Procedural icons, not bitmaps.** The three icons are drawn in code, with geometric primitives — no image arrays. This keeps the entire firmware typeable and readable, and yields two nice geometry exercises:

- **The STOP sign** is an octagon: 8 vertices computed with sine/cosine every 45° (offset by 22.5° so the flat sides sit at the top and bottom, like the real sign), filled with a triangle "fan" from the first vertex. Draw a slightly larger white octagon and a smaller red one on top — done, white border for free. The "STOP" text goes on top, centered.
- **The gear** is a central circle + 8 trapezoidal teeth. Each tooth is a radial trapezoid between two radii (wide base at the body, narrower tip), decomposed into 2 triangles. Rotating the gear means adding an increment to the base angle and redrawing — rotation by math, with no dependency on sprite-rotation APIs.
- **The green check** is a filled circle + two thick lines. Since the basic primitives draw 1 px lines, the code includes a `thickLine` helper that builds a thick stroke as an oriented rectangle (2 triangles) with rounded caps (2 circles) — the same trick vector-drawing libraries use.

**Upright by accelerometer, not gyroscope.** Although "gyroscope" is the word everyone reaches for, absolute orientation relative to the ground comes from the accelerometer: gravity is a constant ~1 g vector, and whichever screen-plane axis it projects onto tells you which edge is facing down. The gyroscope measures angular velocity — integrating it accumulates drift and, on its own, never knows where "down" is. Since the display is a 128×128 square, rotation is quantized to 90° steps with `setRotation`: pixel-perfect, essentially free, and the canvas doesn't change — only how it's pushed to the panel. The guards that keep the screen from flapping between orientations are covered in 4.3.

About the original request for a thumbs-up: a thumb drawn with geometric primitives alone looks visibly crude at this resolution. The circle-with-check communicates "done" just as clearly and draws clean. If you insist on the thumbs-up, the right path is a bitmap — see the improvements section.

### 4.2 The complete code

Contents of `main/main.cpp`:

```cpp
// FreeRTOS first: under ESP-IDF, FreeRTOS.h must be included before
// any header that pulls in task.h (M5Unified does), or the build
// stops with "#error include FreeRTOS.h must appear ..."
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <M5Unified.h>

#include <driver/usb_serial_jtag.h>
#include <esp_err.h>

#include <cmath>
#include <string>

// Serial protocol: one command per line, terminated by \n
//   "processing"   -> spinning yellow gear (Claude processing)
//   "waiting_user" -> red STOP sign (waiting for the user)
//   "idle"         -> green circle with check (idle)
//   "off"          -> screen off

namespace {

constexpr float kPi     = 3.14159265f;
constexpr int   kWidth  = 128;
constexpr int   kHeight = 128;

// Compile-time RGB888 -> RGB565 conversion, so colors can be
// picked with the familiar 0-255 values
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kColorBg     = rgb565(0, 0, 0);
constexpr uint16_t kColorRed    = rgb565(210, 35, 35);
constexpr uint16_t kColorYellow = rgb565(255, 200, 0);
constexpr uint16_t kColorGreen  = rgb565(60, 180, 75);
constexpr uint16_t kColorWhite  = rgb565(255, 255, 255);

enum class State { Idle, Processing, WaitingUser, Off };

// Framebuffer: every frame is composed here and pushed to the
// display in one go, eliminating animation flicker
M5Canvas canvas(&M5.Display);

// ---------------- helper primitives ----------------

// Thick stroke with rounded caps: an oriented rectangle
// (2 triangles) plus a circle at each end
void thickLine(float x0, float y0, float x1, float y1,
               float thickness, uint16_t color)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) {
        return;
    }
    // unit perpendicular vector * half-thickness
    float px = -dy / length * (thickness * 0.5f);
    float py =  dx / length * (thickness * 0.5f);

    canvas.fillTriangle(x0 + px, y0 + py, x1 + px, y1 + py,
                        x1 - px, y1 - py, color);
    canvas.fillTriangle(x0 + px, y0 + py, x1 - px, y1 - py,
                        x0 - px, y0 - py, color);
    canvas.fillCircle(x0, y0, thickness * 0.5f, color);
    canvas.fillCircle(x1, y1, thickness * 0.5f, color);
}

// Filled regular octagon with flat sides at the top and bottom
// (like the STOP sign): vertices every 45 degrees, offset by
// 22.5 degrees, filled as a triangle fan
void octagon(int cx, int cy, float radius, uint16_t color)
{
    float xs[8];
    float ys[8];
    for (int i = 0; i < 8; ++i) {
        float a = (22.5f + 45.0f * i) * kPi / 180.0f;
        xs[i] = cx + radius * cosf(a);
        ys[i] = cy + radius * sinf(a);
    }
    for (int i = 1; i < 7; ++i) {
        canvas.fillTriangle(xs[0], ys[0], xs[i], ys[i],
                            xs[i + 1], ys[i + 1], color);
    }
}

// ---------------- icons ----------------

void drawStopIcon()
{
    canvas.fillSprite(kColorBg);
    int cx = kWidth / 2;
    int cy = kHeight / 2;

    octagon(cx, cy, 58, kColorWhite);  // white border
    octagon(cx, cy, 51, kColorRed);    // red body

    canvas.setTextColor(kColorWhite, kColorRed);
    canvas.setTextDatum(middle_center);
    canvas.setTextSize(2);
    canvas.drawString("STOP", cx, cy);

    canvas.pushSprite(0, 0);
}

void drawGearIcon(float angle)
{
    canvas.fillSprite(kColorBg);
    int cx = kWidth / 2;
    int cy = kHeight / 2;

    const int   teeth = 8;
    const float rTip  = 52.0f;  // outer radius of the teeth
    const float rBody = 38.0f;  // body radius
    const float rHole = 14.0f;  // center hole radius
    const float step  = 2.0f * kPi / teeth;

    // Each tooth is a radial trapezoid: wide base at the body,
    // narrower tip; decomposed into 2 triangles
    for (int i = 0; i < teeth; ++i) {
        float a  = angle + i * step;
        float wb = step * 0.28f;  // angular half-width at the base
        float wt = step * 0.16f;  // angular half-width at the tip

        float b0x = cx + rBody * cosf(a - wb);
        float b0y = cy + rBody * sinf(a - wb);
        float b1x = cx + rBody * cosf(a + wb);
        float b1y = cy + rBody * sinf(a + wb);
        float t0x = cx + rTip * cosf(a - wt);
        float t0y = cy + rTip * sinf(a - wt);
        float t1x = cx + rTip * cosf(a + wt);
        float t1y = cy + rTip * sinf(a + wt);

        canvas.fillTriangle(b0x, b0y, b1x, b1y, t1x, t1y, kColorYellow);
        canvas.fillTriangle(b0x, b0y, t1x, t1y, t0x, t0y, kColorYellow);
    }

    canvas.fillCircle(cx, cy, rBody, kColorYellow);
    canvas.fillCircle(cx, cy, rHole, kColorBg);  // center hole

    canvas.pushSprite(0, 0);
}

void drawDoneIcon()
{
    canvas.fillSprite(kColorBg);
    int cx = kWidth / 2;
    int cy = kHeight / 2;

    canvas.fillCircle(cx, cy, 56, kColorGreen);

    // Check mark as two thick white strokes
    thickLine(cx - 27, cy + 2, cx - 8, cy + 23, 13, kColorWhite);
    thickLine(cx - 8, cy + 23, cx + 30, cy - 21, 13, kColorWhite);

    canvas.pushSprite(0, 0);
}

void drawBlankScreen()
{
    canvas.fillSprite(kColorBg);
    canvas.pushSprite(0, 0);
}

// ---------------- auto-rotation via IMU ----------------

// Adjust here if, on your unit, the image comes out consistently
// rotated: 0..3, added to the detected rotation
constexpr uint8_t kRotationOffset = 0;

constexpr float kFlatThreshold = 0.80f;  // |az| above this = lying flat
constexpr float kTiltThreshold = 0.55f;  // in-plane minimum to cast a "vote"
constexpr int   kStableSamples = 8;      // consecutive readings (~8 x 33 ms)

uint8_t rotationFromAccel(float ax, float ay, float az, uint8_t current)
{
    if (fabsf(az) > kFlatThreshold) {
        return current;             // lying flat on the desk: keep as is
    }
    uint8_t base;
    if      (ax >  kTiltThreshold) base = 0;
    else if (ax < -kTiltThreshold) base = 2;
    else if (ay >  kTiltThreshold) base = 1;
    else if (ay < -kTiltThreshold) base = 3;
    else return current;            // near a diagonal: keep as is
    // If your unit rotates to the wrong side, swap the two ay cases
    return (base + kRotationOffset) & 3;
}

// ---------------- state machine ----------------

void renderState(State state, float gearAngle)
{
    switch (state) {
        case State::Processing:
            drawGearIcon(gearAngle);
            break;
        case State::WaitingUser:
            drawStopIcon();
            break;
        case State::Idle:
            drawDoneIcon();
            break;
        case State::Off:
            drawBlankScreen();
            break;
    }
}

void applyState(State next, State& current, float& gearAngle)
{
    if (next == current) {
        return;  // avoids needless redraws of the static icons
    }
    current = next;
    if (next == State::Processing) {
        gearAngle = 0.0f;
    }
    renderState(current, gearAngle);
}

void handleCommand(const std::string& cmd, State& state, float& gearAngle)
{
    if (cmd == "processing") {
        applyState(State::Processing, state, gearAngle);
    } else if (cmd == "waiting_user") {
        applyState(State::WaitingUser, state, gearAngle);
    } else if (cmd == "idle") {
        applyState(State::Idle, state, gearAngle);
    } else if (cmd == "off") {
        applyState(State::Off, state, gearAngle);
    }
    // Unknown command: silently ignore
}

} // namespace

extern "C" void app_main(void)
{
    // Initialize the AtomS3R hardware (display, button, I2C backlight)
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(128);

    // 128x128 framebuffer at 16 bpp = 32 KB of RAM
    canvas.setColorDepth(16);
    canvas.createSprite(kWidth, kHeight);

    State state     = State::Off;
    float gearAngle = 0.0f;
    applyState(State::Idle, state, gearAngle);  // initial state

    // USB Serial/JTAG driver, to receive data from the computer
    usb_serial_jtag_driver_config_t usbCfg = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usbCfg));

    std::string line;
    uint8_t buf[64];
    bool screenOn = true;

    // Auto-rotation: debounce state
    bool    imuOk       = M5.Imu.isEnabled();
    uint8_t rotation    = M5.Display.getRotation();
    uint8_t candidate   = rotation;
    int     stableCount = 0;

    while (true) {
        M5.update();  // refreshes the button state

        // Auto-rotation: read gravity and, after a stable streak,
        // rotate the display and redraw the current icon upright
        if (imuOk) {
            M5.Imu.update();
            float ax, ay, az;
            M5.Imu.getAccel(&ax, &ay, &az);
            uint8_t wanted = rotationFromAccel(ax, ay, az, rotation);
            if (wanted != rotation) {
                if (wanted == candidate) {
                    if (++stableCount >= kStableSamples) {
                        rotation = wanted;
                        M5.Display.setRotation(rotation);
                        renderState(state, gearAngle);
                        stableCount = 0;
                    }
                } else {
                    candidate   = wanted;
                    stableCount = 1;
                }
            } else {
                stableCount = 0;
            }
        }

        // The 33 ms timeout plays two roles: it blocks waiting for
        // data (no busy-wait) and it sets the ~30 fps animation pace
        int bytesRead = usb_serial_jtag_read_bytes(buf, sizeof(buf),
                                                   pdMS_TO_TICKS(33));
        for (int i = 0; i < bytesRead; ++i) {
            char c = static_cast<char>(buf[i]);
            if (c == '\n') {
                // Strip trailing \r and spaces (lines may arrive as
                // "processing\r\n" depending on the sending OS)
                while (!line.empty() &&
                       (line.back() == '\r' || line.back() == ' ')) {
                    line.pop_back();
                }
                handleCommand(line, state, gearAngle);
                line.clear();
            } else {
                line += c;
                if (line.size() > 64) {
                    line.clear();  // protection against serial garbage
                }
            }
        }

        // Animation: only the "processing" state redraws every frame
        if (state == State::Processing && screenOn) {
            gearAngle += 0.10f;  // ~3 s per full turn at 30 fps
            if (gearAngle >= 2.0f * kPi) {
                gearAngle -= 2.0f * kPi;
            }
            drawGearIcon(gearAngle);
        }

        // Screen button: toggles the display on and off
        if (M5.BtnA.wasPressed()) {
            screenOn = !screenOn;
            M5.Display.setBrightness(screenOn ? 128 : 0);
        }
    }
}
```

### 4.3 Code decisions worth commenting on

**The 33 ms timeout as the animation clock.** `usb_serial_jtag_read_bytes` blocks waiting for data for up to 33 ms and returns 0 if nothing arrived. This gives the loop a natural rhythm of ~30 frames per second with no busy-wait — it's the gear's "clock". One honest detail: when bytes do arrive, the call returns before the timeout, so the next frame comes slightly early and the rotation speeds up imperceptibly for a moment. For this project it's irrelevant; if you ever want exact cadence, the way to go is measuring time with `esp_timer_get_time()` and advancing the angle proportionally to elapsed time.

**`applyState` only redraws on change.** The STOP and done icons are static — redrawing them at 30 fps would be wasteful and would hog the display bus with pointless `pushSprite` calls. The gear is the exception, redrawn per frame in the main loop.

**`rgb565` as a `constexpr`.** The display works with 16-bit colors (5 bits of red, 6 of green, 5 of blue). The function converts familiar RGB values (0–255) at compile time, letting you tune the palette by editing three numbers — the sign's red, for example, is a signage red (210, 35, 35), not the blown-out pure red of `TFT_RED`.

**Why strip `\r` by hand?** Depending on the operating system and the command used in the hook, the line may arrive as `processing\r\n`. Without the cleanup, the string comparison fails on an invisible character — a classic of serial debugging.

**Include order matters under ESP-IDF.** The FreeRTOS umbrella header (`freertos/FreeRTOS.h`) must be included before any header that drags in `freertos/task.h` — and M5Unified does. Get it backwards and the build stops at `#error "include FreeRTOS.h must appear in source files before include task.h"`. Under Arduino you never see this, because the core includes FreeRTOS globally before your code; under native ESP-IDF, the include order in the listing above is load-bearing.

**The IMU axis mapping is empirical.** Which physical edge corresponds to which accelerometer axis depends on how the BMI270 is mounted on the board, and extracting that from datasheets is more work than it's worth. The code is written to be corrected in one place: run it and tilt the cube — if the image lands consistently 90° or 180° off, fix `kRotationOffset`; if it rotates to the wrong side (mirrored), swap the two `ay` cases inside `rotationFromAccel`. Two flashes and it's calibrated for good.

**Why the debounce and the dead zones?** Raw accelerometer readings jitter, and near a 45° diagonal the dominant axis flips constantly — without guards, the screen would flap between orientations. Three rules keep it calm: when the module lies flat (gravity mostly on the Z axis) the current rotation is kept; near diagonals nothing votes; and a change only applies after 8 consistent readings — about a quarter of a second at the loop's ~30 Hz pace.

**About the IMU API:** `M5.Imu.isEnabled()`, `M5.Imu.update()` and `M5.Imu.getAccel(&ax, &ay, &az)` are M5Unified's IMU class — the same one used by the examples that ship with the library — with acceleration in g units. As with the rest of the code, I haven't run this on hardware; if a signature differs in your library version, the bundled M5Unified IMU examples are the reference.

**About the driver API:** `usb_serial_jtag_driver_install` and `usb_serial_jtag_read_bytes` are the API used by ESP-IDF's official example for this peripheral (`examples/peripherals/usb_serial_jtag/usb_serial_jtag_echo` in the framework tree). If any signature differs in your IDF version, that example — which ships and compiles with the framework installed on your machine — is the canonical reference; check it there.

**About the graphics API:** `M5Canvas`, `createSprite`, `fillSprite`, `fillTriangle`, `fillCircle`, `drawString`, `setTextDatum(middle_center)` and `pushSprite` are the standard M5GFX API (LovyanGFX-based), identical across the Arduino and ESP-IDF frameworks. One AtomS3R-specific detail: the display backlight is controlled by a dedicated I2C chip (unlike the original AtomS3, which used a GPIO pin), and older M5Unified versions had problems with it. The `^0.2` manifest already pulls a recent version, but if the screen stays black while the code appears to run, the library version is the first suspect.

---

## 5. Build, flash and monitor

Everything through the extension, via the status bar or the palette:

1. **Build** — palette → **`ESP-IDF: Build your Project`** (or the cylinder icon). The first build takes a while: it downloads the Component Registry dependencies and compiles the entire framework. Subsequent builds are incremental and fast.

2. **Port and flashing mode.** Connect the AtomS3R. **For the first flash**, you must put it into download mode manually: hold the side reset button for about 2 seconds, until the internal LED indicates the mode, then release — the board then shows up as a serial port (`/dev/ttyACM0` on Linux, `COMx` on Windows). Select it with **`ESP-IDF: Select Port to Use`**. On later flashes, with the firmware already using USB Serial/JTAG, the tooling can usually restart the board into download mode on its own, no button needed.

3. **Flash** — palette → **`ESP-IDF: Flash your Project`** (UART method, which here travels over the virtual USB port).

4. **Monitor** — palette → **`ESP-IDF: Monitor Device`**. You'll see the ESP-IDF boot log, and the AtomS3R's screen should show the **green circle with the check**. That's the first test passing. **Close the monitor before moving on** — it keeps the port open, and the next tests need it free.

---

## 6. Manual serial test (before touching Claude Code)

The golden rule of integration: test each half separately before joining them. With the firmware flashed, the AtomS3R connected and the monitor closed, validate the protocol by hand.

**Linux.** Find the port (typically `/dev/ttyACM0`):

```bash
ls /dev/ttyACM*
```

Configure the port in raw mode once (needed for `echo` to work cleanly, without the OS driver trying to interpret control characters):

```bash
stty -F /dev/ttyACM0 raw -echo
```

Then test:

```bash
echo processing > /dev/ttyACM0    # spinning yellow gear
echo waiting_user > /dev/ttyACM0  # red STOP sign
echo idle > /dev/ttyACM0          # green circle with check
echo off > /dev/ttyACM0           # screen off
```

**macOS.** The port will be something like `/dev/cu.usbmodemXXXX` (list with `ls /dev/cu.*`). macOS's `stty` uses a lowercase `-f`:

```bash
stty -f /dev/cu.usbmodemXXXX raw -echo
echo processing > /dev/cu.usbmodemXXXX
```

One peculiarity of the console over USB Serial/JTAG: besides switching the icon, the device may emit log bytes over the same port. For this test that's irrelevant (you only write, never read), but it explains why, if you open the port in a serial terminal, you'll see ESP-IDF logs mixed in.

If all four icons respond — and the gear spins smoothly, without flicker — the hardware half is done and proven. Anything that fails from here on is on the Claude Code side.

---

## 7. Configuring the Claude Code hooks

Hooks are configured in `~/.claude/settings.json` (applies to all sessions) or in a project's `.claude/settings.json` (applies only there). The structure is: event name → list of matchers → list of commands.

Complete configuration (Linux — adjust the port path; for macOS, switch to the `cu.usbmodem` port):

```json
{
  "hooks": {
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "echo processing > /dev/ttyACM0 2>/dev/null || true"
          }
        ]
      }
    ],
    "Notification": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "echo waiting_user > /dev/ttyACM0 2>/dev/null || true"
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "echo idle > /dev/ttyACM0 2>/dev/null || true"
          }
        ]
      }
    ],
    "SessionEnd": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "echo off > /dev/ttyACM0 2>/dev/null || true"
          }
        ]
      }
    ]
  }
}
```

The most important detail of this configuration is not the `echo` — it's the **`2>/dev/null || true`** at the end of each command. In Claude Code hooks, exit codes have semantics: an exit code 2, for instance, **blocks** the action in progress. If the AtomS3R is unplugged and the `echo` fails, without the `|| true` you'd turn a loose cable into a Claude Code that refuses to work. With it, the failure is swallowed silently and the worst that happens is the icon not updating.

After saving the file, restart the Claude Code session (or use the `/hooks` command inside it to check that the configuration loaded).

**End-to-end test:** open Claude Code, send any prompt and watch: the gear should start spinning immediately, and the green check should appear when the response finishes. Ask for something that requires permission (for instance, running a shell command that isn't pre-approved) and the STOP sign should appear while it waits for your OK.

---

## 8. Testing on Windows with PowerShell

On Linux and macOS the direct `echo` to the port works because `stty` prepares the device first. On Windows, the equivalent path (`echo ... > \\.\COM5` in cmd, preceded by `mode COM5 ...`) is fragile: `mode` fails if any program has the port open, the redirection gives no diagnostics when something goes wrong, and through Git Bash the `COM5 → /dev/ttyS4` mapping doesn't always behave well with USB CDC devices.

The clean way out is **PowerShell with .NET's `System.IO.Ports.SerialPort` class**. It configures the port by itself on open (baud, parity, bits — no `mode` needed), throws catchable exceptions when the port doesn't exist or is busy, and is available on any Windows, both in Windows PowerShell 5.1 (`powershell`) and PowerShell 7+ (`pwsh`).

### 8.1 Finding the COM port

With the AtomS3R connected (and flashed), it shows up as "USB Serial Device (COMx)" in Device Manager. From PowerShell:

```powershell
# Lists the serial port names currently present
[System.IO.Ports.SerialPort]::GetPortNames()

# Or, with each device's description, to identify which one is the Atom
Get-PnpDevice -Class Ports -PresentOnly | Select-Object FriendlyName, Status
```

Note the number (in the examples below, `COM5`). A reminder that applies to every test here: **on Windows the serial port is exclusive-access** — close the ESP-IDF extension's monitor first, or `Open()` will fail with "access denied" (which is actually a great diagnostic: if you see it, something is holding the port).

### 8.2 A quick one-liner test

Before any script, validate the entire path with one line in a PowerShell console:

```powershell
$p = New-Object System.IO.Ports.SerialPort COM5, 115200
$p.DtrEnable = $false; $p.RtsEnable = $false
$p.Open(); $p.Write("processing`n"); $p.Close()
```

The gear should start spinning. The `` `n `` inside double quotes is PowerShell's line feed — exactly the `\n` the firmware expects as the command terminator.

About `DtrEnable`/`RtsEnable` set to `$false`: the ESP32-S3 flashing tools use DTR/RTS sequences over USB Serial/JTAG to restart the board into download mode. A plain port open shouldn't trigger that, but pinning both lines to `$false` removes any chance of an accidental reset on open — and if you ever see the board rebooting when a serial program connects, this is the first place to look.

### 8.3 The `Send-ClaudeState.ps1` script

For use by the hooks, wrap everything in a script with validation and the same philosophy as Linux's `|| true`: **never fail because of a loose cable**. Save it as `C:\claude-tools\Send-ClaudeState.ps1` (a path without spaces keeps settings.json simple):

```powershell
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("processing", "waiting_user", "idle", "off")]
    [string]$State,

    [string]$PortName = "COM5"
)

$port = New-Object System.IO.Ports.SerialPort $PortName, 115200
$port.NewLine       = "`n"     # WriteLine will terminate with \n, not \r\n
$port.DtrEnable     = $false   # removes any chance of an accidental reset
$port.RtsEnable     = $false
$port.WriteTimeout  = 1000     # ms; never leaves a hook hanging

try {
    $port.Open()
    $port.WriteLine($State)
}
catch {
    # Silent on purpose: a disconnected Atom or a busy port
    # must never become a hook failure
}
finally {
    if ($port.IsOpen) { $port.Close() }
    $port.Dispose()
}

exit 0   # the "|| true" equivalent: hooks treat exit codes as semantics
```

Two design details: `NewLine = "`n"` matters because the class default is `\r\n` — the firmware would tolerate it (it strips `\r`), but sending exactly the protocol is cleaner; and the unconditional `exit 0` exists because, in Claude Code hooks, exit codes have semantics (2 blocks actions), so this script must never propagate an error.

### 8.4 Testing the four icons

```powershell
.\Send-ClaudeState.ps1 -State processing -PortName COM5   # spinning gear
.\Send-ClaudeState.ps1 -State waiting_user -PortName COM5 # STOP sign
.\Send-ClaudeState.ps1 -State idle -PortName COM5         # green check
.\Send-ClaudeState.ps1 -State off -PortName COM5          # screen off
```

If the console complains about execution policy when running the script locally, use `powershell -ExecutionPolicy Bypass -File .\Send-ClaudeState.ps1 -State idle` — the same form the hooks will use.

### 8.5 Wiring it into the hooks

The advantage of invoking `powershell.exe` explicitly is being **shell-agnostic**: the same line works whether called from cmd or Git Bash, so it doesn't matter which of them your Claude Code installation uses to run hooks (when in doubt, test the line manually in both). The Windows `~/.claude/settings.json` becomes:

```json
{
  "hooks": {
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\claude-tools\\Send-ClaudeState.ps1 -State processing"
          }
        ]
      }
    ],
    "Notification": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\claude-tools\\Send-ClaudeState.ps1 -State waiting_user"
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\claude-tools\\Send-ClaudeState.ps1 -State idle"
          }
        ]
      }
    ],
    "SessionEnd": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\claude-tools\\Send-ClaudeState.ps1 -State off"
          }
        ]
      }
    ]
  }
}
```

Note the doubled `\\` in the path — a JSON requirement, not a PowerShell one. `-NoProfile` skips loading the user profile (faster and more predictable) and `-ExecutionPolicy Bypass` applies to this invocation only, without touching the machine's global policy.

One honest note about cost: every trigger pays the `powershell.exe` startup — typically a few hundred milliseconds. For switching an icon, that's imperceptible in practice; if it bothers you, `pwsh` (PowerShell 7) starts somewhat faster, and the definitive fix is the always-open-port daemon described in the improvements section.

---

## 9. Known limitations and possible improvements

**Opening/closing the port on every hook.** Each `echo` opens and closes the serial device. With the ESP32-S3's USB Serial/JTAG this usually works without loss, and the board doesn't reset — but there's no absolute guarantee that no byte gets lost in the opening window. If you observe dropped commands frequently, the robust solution is a small daemon that keeps the port permanently open and exposes a named pipe (or local socket) where the hooks write. The hooks stay identical; only the `echo` destination changes.

**State granularity.** The `Notification` event covers "Claude needs your attention", but the exact behavior (only permission requests? also idle-waiting-for-input? after how many seconds?) may vary between versions — check the documentation for yours. If the green check sometimes shows when you'd expect the STOP sign, that's the likely cause, and newer events like `PermissionRequest` may refine the mapping.

**Logs on the same channel as the protocol.** With the console on USB Serial/JTAG, ESP-IDF logs go out over the same port the protocol comes in on. Since the computer only writes and the device only interprets what it receives, there's no real conflict — but if you ever want a response channel from the device to the computer (a confirmation "ok", for example), you'll need to separate things: lower the log level (`menuconfig → Log output`) or prefix the protocol replies so they can be filtered from the logs.

**Multiple simultaneous sessions.** If you run two Claude Code instances at once, both write to the same device and the screen reflects the latest event from either of them. Distinguishing sessions would require adding an identifier to the protocol (hooks receive a `session_id` in their stdin JSON) and drawing a split screen — a good version-2 project.

**Bitmap icons (the real thumbs-up).** If you want illustrated icons — a thumbs-up with fingers, shading, a gradient — the way to go is converting a 128×128 (or smaller) PNG into a C array in RGB565 and drawing it with M5GFX's `pushImage`. Online and offline image-to-C-array converters exist (the LVGL project's image converter is a well-known option). The resulting array is generated code — nobody types 32 KB of hex by hand — and goes into its own header included from `main.cpp`. The animated gear would stay procedural, or become a sequence of bitmap frames.

**Other evolution ideas:**

- **Gear with easing** — varying the angle increment over time (ease-in when entering the state) gives the animation life; it's a one-line change.
- **Elapsed-time counter** — show how long the current state has lasted (how many seconds processing); in ESP-IDF, `esp_timer_get_time()` gives microseconds since boot, and the text goes onto the canvas before the `pushSprite`.
- **Buzzer/vibration** — the AtomS3R has no buzzer, but the Grove port (HY2.0-4P) takes an external module to give an audible alert on the transition to STOP.
- **MQTT instead of USB** — if you want the device away from the computer (another room, another floor), the architecture changes to hooks → MQTT broker → AtomS3R over Wi-Fi. ESP-IDF ships the native `esp-mqtt` client; the hooks swap `echo` for `mosquitto_pub`.

---

## 10. References

- ESP-IDF extension for VS Code (installation, commands, EIM): https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/
- The extension on the marketplace: https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension
- AtomS3R documentation (specifications, display resolution, pinout): https://docs.m5stack.com/en/core/AtomS3R
- Product page at the M5Stack store: https://shop.m5stack.com/products/atoms3r-dev-kit
- M5Unified on the ESP Component Registry (supported frameworks and devices): https://components.espressif.com/components/m5stack/m5unified
- M5Unified repository: https://github.com/m5stack/M5Unified
- The .NET System.IO.Ports.SerialPort class, used by the PowerShell script: https://learn.microsoft.com/dotnet/api/system.io.ports.serialport
- Official Claude Code hooks documentation (event list, settings.json format, exit-code semantics): https://code.claude.com/docs/en/hooks-guide

---

*This article describes hook event names, ESP-IDF extension commands and APIs as checked against the documentation in September 2026. Both Claude Code hooks and the ESP-IDF extension evolve quickly — before implementing, check the documentation for the versions you have installed.*
