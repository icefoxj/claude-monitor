// claude-monitor on the M5Stack Tab5 (ESP32-P4): 5" 1280x720 IPS panel
// with capacitive touch, BMI270 IMU, 32 MB PSRAM, USB-C on the P4's USB
// Serial/JTAG.
//
// Layout: the status icon and, next to it, the hook inspector: every field
// of the last Claude Code hook event the host forwarded, plus a short
// history. This is the raw material for the multi-session mode: first see
// what the hooks deliver, then design.
//
// The whole display turns with gravity in 90-degree steps, like a tablet:
// landscape puts the icon on the left and the inspector on the right,
// portrait puts the icon on top. Which way the sensor maps to the panel is
// calibrated per unit with "calibrate rot=<upright rotation now> sign=<1|-1>"
// and stored in NVS.
//
// Everything board-independent (protocol, icons, orientation, state
// machine, event log and view, calibration storage) is in the monitor-core
// component; this file wires it to this hardware.

// FreeRTOS first: under ESP-IDF, FreeRTOS.h must be included before
// any header that pulls in task.h (M5Unified does)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <M5Unified.h>

#include <driver/usb_serial_jtag.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "monitor/calibration.h"
#include "monitor/eventlog.h"
#include "monitor/eventview.h"
#include "monitor/icons.h"
#include "monitor/orientation.h"
#include "monitor/protocol.h"
#include "monitor/tilt.h"
#include "monitor/ui.h"
#include "monitor/version.h"

namespace {

constexpr const char* TAG       = "monitor";
constexpr const char* kBoard    = "tab5";
constexpr const char* kFeatures = "events,touch,rotate";   // the host streams hook events to us
constexpr float kPi = 3.14159265f;

constexpr int kIconSize = 480;
constexpr int kViewW    = 720;
constexpr int kViewH    = 720;

// Where the two canvases go for each display rotation. Landscape (1280x720,
// rotations 1 and 3): icon in a 560 px column on the left, inspector on the
// right. Portrait (720x1280, rotations 0 and 2): icon in a 560 px band on
// top, inspector below.
struct Layout { int iconX, iconY, viewX, viewY; };

Layout layoutFor(int rotation){
    if (rotation & 1){
        return { 40, 120, 560, 0 };
    }
    return { 120, 40, 0, 560 };
}

constexpr uint8_t kBrightness    = 160;
constexpr uint8_t kDimBrightness = 40;    // while the host's heartbeat is missing

// Idle, or without a host, for this long (frames at ~30 fps): screen off
// until the next state change or a tap
constexpr int kAutoOffFrames = 30 * 60 * 30;   // 30 min

// The inspector redraws on every event and state change, and once a
// second so the "ago" ages move
constexpr int kViewRefreshFrames = 30;

// ---------------- orientation calibration for this unit ----------------
//
// Compiled defaults, overridden by whatever "calibrate" stored in NVS:
// rotation = the display rotation shown while gravity sector 0 is down,
// sign = which way the rotation follows the sector. "calibrate rot=N"
// means "N is upright the way I am holding it now"; the offset is derived.
// Verified on one Tab5: sector 0 (gravity along +x, the tablet on its
// stand) is rotation 3, and the rotation follows the sector directly.
constexpr int kDefaultOffset = 3;
constexpr int kDefaultSign   = 1;

// Both canvases live in PSRAM: 480x480x2 = 450 KB, 720x720x2 = 1 MB
M5Canvas iconCanvas(&M5.Display);
M5Canvas viewCanvas(&M5.Display);

int64_t nowMs(){
    return esp_timer_get_time() / 1000;
}

void writeLine(const char* msg, int n){
    if (n > 0){
        usb_serial_jtag_write_bytes(msg, n, pdMS_TO_TICKS(50));
    }
}

void sendVersion(){
    char msg[256];
    int n = monitor::formatVersionLine(msg, sizeof(msg) - 1, kBoard, kFeatures);
    if (n > static_cast<int>(sizeof(msg)) - 2){
        n = sizeof(msg) - 2;
    }
    msg[n++] = '\n';
    writeLine(msg, n);
}

const char* screenName(uint8_t brightness){
    if (brightness == 0) return "off";
    if (brightness == kDimBrightness) return "dim";
    return "on";
}

// Same shape as the AtomS3R's STATUS so the host's parsers work unchanged.
// rot = the display rotation in effect, angle = the gravity direction in
// the panel plane (degrees), offset = the calibration's sector-0 rotation,
// sector = the quantised gravity direction.
void sendStatus(const monitor::Ui& ui, const monitor::EventLog& log, const monitor::Orientation& orient,
                uint8_t brightness, float ax, float ay, float az){
    const char* sessions = ui.sessions();
    const char* link = !ui.linkArmed() ? "unarmed" : (ui.linkLost() ? "lost" : "ok");
    char msg[288];
    int n = snprintf(msg, sizeof(msg),
                     "STATUS state=%s subagents=%d rot=%u angle=%.1f ax=%.2f ay=%.2f az=%.2f"
                     " fw=%s board=%s sign=%d offset=%d sessions=%s link=%s work=%d screen=%s"
                     " events=%lu sector=%d\n",
                     monitor::stateName(ui.state()), ui.subagents(), M5.Display.getRotation(),
                     atan2f(ay, ax) * 180.0f / kPi, ax, ay, az,
                     esp_app_get_description()->version, kBoard,
                     orient.config().sign, orient.config().offset,
                     sessions[0] ? sessions : "-", link, ui.workFrames() / 30, screenName(brightness),
                     static_cast<unsigned long>(log.count()), orient.sector());
    writeLine(msg, n);
}

// Header of the inspector: what the icon side is showing
void statusText(const monitor::Ui& ui, char* out, size_t size){
    const char* sessions = ui.sessions();
    const char* link = !ui.linkArmed() ? "no host yet" : (ui.linkLost() ? "host lost" : "host ok");
    snprintf(out, size, "%s   sessions %s   %s",
             monitor::stateName(ui.state()), sessions[0] ? sessions : "-", link);
}

}  // namespace

extern "C" void app_main(void){
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(kBrightness);
    M5.Display.fillScreen(TFT_BLACK);

    const bool nvsOK = monitor::initCalibrationStorage();
    monitor::Calibration cal;
    cal.rotation  = kDefaultOffset;
    cal.sign      = kDefaultSign;
    cal.offsetDeg = 0.0f;
    const bool stored = nvsOK && monitor::loadCalibration(cal);

    monitor::OrientationConfig orientCfg;
    orientCfg.offset = cal.rotation & 3;
    orientCfg.sign   = cal.sign < 0 ? -1 : 1;
    monitor::Orientation orient(orientCfg);

    bool imuOK = M5.Imu.isEnabled();
    ESP_LOGI(TAG, "imu %s, orientation offset %d sign %d (%s)",
             imuOK ? "enabled" : "absent", orientCfg.offset, orientCfg.sign, stored ? "from nvs" : "compiled");

    // Start in landscape; the first accelerometer sample decides for real
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    int rotation = 1;
    if (imuOK){
        M5.Imu.update();
        M5.Imu.getAccel(&ax, &ay, &az);
        if (orient.update(ax, ay, az)){
            rotation = orient.rotation();
        }
    }
    M5.Display.setRotation(rotation);
    Layout layout = layoutFor(rotation);
    ESP_LOGI(TAG, "display %dx%d rotation %d", M5.Display.width(), M5.Display.height(), rotation);

    iconCanvas.setPsram(true);
    iconCanvas.setColorDepth(16);
    if (!iconCanvas.createSprite(kIconSize, kIconSize)){
        ESP_LOGE(TAG, "icon canvas allocation failed (PSRAM?)");
    }
    viewCanvas.setPsram(true);
    viewCanvas.setColorDepth(16);
    if (!viewCanvas.createSprite(kViewW, kViewH)){
        ESP_LOGE(TAG, "inspector canvas allocation failed (PSRAM?)");
    }

    monitor::Icons icons(iconCanvas, kIconSize, layout.iconX, layout.iconY);
    monitor::TiltConfig tiltCfg;   // unused: the icon is not tilted, the whole display turns
    monitor::Ui ui(icons, tiltCfg);
    monitor::EventLog log(16);
    monitor::EventView view(viewCanvas, layout.viewX, layout.viewY);

    ui.apply(monitor::State::Idle);
    char status[96];
    statusText(ui, status, sizeof(status));
    view.draw(log, status, nowMs());

    // USB Serial/JTAG driver: event lines can be a few KB, so a roomy
    // receive buffer and big reads
    usb_serial_jtag_driver_config_t usbCfg = {
        .tx_buffer_size = 2048,
        .rx_buffer_size = 8192,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usbCfg));
    sendVersion();   // announce once, through the driver

    // Turns the display and moves both canvases
    auto applyRotation = [&](int r){
        rotation = r;
        M5.Display.setRotation(r);
        M5.Display.fillScreen(TFT_BLACK);
        layout = layoutFor(r);
        icons.setOrigin(layout.iconX, layout.iconY);
        view.setOrigin(layout.viewX, layout.viewY);
        ui.redraw();
        ESP_LOGI(TAG, "rotation %d (sector %d)", r, orient.sector());
    };

    monitor::LineParser parser(4096);
    std::string line;
    uint8_t buf[512];
    bool    screenOn   = true;    // a tap toggles it
    bool    autoOff    = false;   // switched off by the idle / no-host timer
    uint8_t brightness = kBrightness;
    bool    viewDirty  = false;
    int     frame      = 0;

    while(true){
        M5.update();   // refreshes the touch state

        // Gravity decides the display rotation, in 90-degree steps
        if (imuOK){
            M5.Imu.update();
            M5.Imu.getAccel(&ax, &ay, &az);
            if (orient.update(ax, ay, az) && orient.rotation() != rotation){
                applyRotation(orient.rotation());
                viewDirty = true;
            }
        }

        // The 33 ms timeout blocks waiting for data (no busy-wait) and sets
        // the ~30 fps pace of the animations
        int bytesRead = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(33));
        for (int i = 0; i < bytesRead; i++){
            if (!parser.feed(static_cast<char>(buf[i]), line)){
                continue;
            }
            auto cmd = monitor::parseCommand(line);
            if (cmd.kind != monitor::Command::Status && cmd.kind != monitor::Command::Version){
                ui.noteCommand();
            }
            switch (cmd.kind){
                case monitor::Command::SetState:
                    if (ui.apply(cmd.state)){
                        autoOff = false;
                    }
                    viewDirty = true;
                    break;
                case monitor::Command::SubagentStart: ui.subagentStart(); viewDirty = true; break;
                case monitor::Command::SubagentStop:  ui.subagentStop();  viewDirty = true; break;
                case monitor::Command::Sessions:      ui.setSessions(cmd.arg); viewDirty = true; break;
                case monitor::Command::Ping:          ui.ping(); break;
                case monitor::Command::Status:        sendStatus(ui, log, orient, brightness, ax, ay, az); break;
                case monitor::Command::Version:       sendVersion(); break;
                case monitor::Command::Calibrate: {
                    monitor::CalibrationRequest req;
                    if (!monitor::parseCalibration(cmd.arg, req)){
                        static const char kUsage[] =
                            "ERROR calibrate: expected rot=<rotation that is upright now, 0-3> sign=<1|-1>, or reset\n";
                        writeLine(kUsage, sizeof(kUsage) - 1);
                        break;
                    }
                    monitor::OrientationConfig next = orient.config();
                    if (req.reset){
                        next.offset = kDefaultOffset;
                        next.sign   = kDefaultSign;
                    } else {
                        if (req.hasSign) next.sign = req.sign;
                        int upright = req.hasRotation ? req.rotation : rotation;
                        next.offset = orient.offsetFor(upright, next.sign);
                    }
                    orient.setConfig(next);
                    cal.rotation = static_cast<uint8_t>(next.offset);
                    cal.sign     = static_cast<float>(next.sign);
                    bool saved = nvsOK && monitor::saveCalibration(cal, req.reset);
                    ESP_LOGI(TAG, "calibration %s: offset %d sign %d (%s)",
                             req.reset ? "reset" : "set", next.offset, next.sign, saved ? "stored" : "not stored");
                    if (orient.valid() && orient.rotation() != rotation){
                        applyRotation(orient.rotation());
                        viewDirty = true;
                    }
                    sendStatus(ui, log, orient, brightness, ax, ay, az);
                    break;
                }
                case monitor::Command::Event: {
                    monitor::HookEvent ev;
                    if (monitor::parseEventLine(cmd.arg, ev)){
                        log.push(std::move(ev), nowMs());
                        viewDirty = true;
                    }
                    break;
                }
                case monitor::Command::Unknown: break;
            }
        }

        // Screen policy: a tap toggles; a long idle or a long silence from
        // the host switches off until something changes; a lost heartbeat dims
        if (M5.Touch.getDetail().wasClicked()){
            if (autoOff){
                autoOff  = false;
                screenOn = true;
            } else {
                screenOn = !screenOn;
            }
        }
        bool idleLong = ui.state() == monitor::State::Idle && ui.framesInState() >= kAutoOffFrames;
        bool hostGone = ui.linkLost() && ui.framesSinceCommand() >= kAutoOffFrames;
        if (!autoOff && (idleLong || hostGone)){
            autoOff = true;
            ESP_LOGI(TAG, "screen off: %s", idleLong ? "idle for 30 min" : "no host for 30 min");
        }
        const bool displayOn = screenOn && !autoOff;
        uint8_t want = !displayOn ? 0 : (ui.linkLost() ? kDimBrightness : kBrightness);
        if (want != brightness){
            brightness = want;
            M5.Display.setBrightness(brightness);
        }

        ui.tick(displayOn);

        ++frame;
        if (displayOn && (viewDirty || frame % kViewRefreshFrames == 0)){
            statusText(ui, status, sizeof(status));
            view.draw(log, status, nowMs());
            viewDirty = false;
        }
    }
}
