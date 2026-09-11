// claude-monitor on the M5Stack AtomS3R (ESP32-S3): 128x128 IPS panel,
// BMI270 IMU, one button under the screen, USB Serial/JTAG for the protocol.
//
// Everything board-independent (protocol, icons, tilt filter, state machine)
// is in the monitor-core component; this file wires it to this hardware:
// the transport, the backlight (dim without a heartbeat, off after a long
// idle), the button and the calibration stored in NVS.

// FreeRTOS first: under ESP-IDF, FreeRTOS.h must be included before
// any header that pulls in task.h (M5Unified does)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <M5Unified.h>

#include <driver/usb_serial_jtag.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "monitor/icons.h"
#include "monitor/protocol.h"
#include "monitor/tilt.h"
#include "monitor/ui.h"
#include "monitor/version.h"

namespace {

constexpr const char* TAG       = "monitor";
constexpr const char* kBoard    = "atoms3r";
constexpr const char* kFeatures = "";   // icon only: the host must not send event lines here
constexpr float kPi = 3.14159265f;

constexpr int     kCanvasSize    = 128;   // the whole panel
constexpr uint8_t kBrightness    = 128;
constexpr uint8_t kDimBrightness = 24;    // while the host's heartbeat is missing

// Idle, or without a host, for this long (frames at ~30 fps): screen off
// until the next state change or a button press
constexpr int kAutoOffFrames = 30 * 60 * 30;   // 30 min

// ---------------- orientation calibration for this unit ----------------
//
// Compiled-in defaults, overridden by whatever "calibrate" stored in NVS.

// Boot orientation, in 90-degree clockwise steps added to the display's
// default rotation. This is the frame all icons are drawn in, and what is
// shown while the cube lies flat on the desk.
constexpr uint8_t kBootRotationOffset = 1;

// Tilt -> icon angle: how the BMI270 is mounted relative to the panel.
// Verified on one AtomS3R in all four standing positions.
constexpr monitor::TiltConfig kTilt = {
    .angleSign   = -1.0f,
    .angleOffset = 0.0f,
};

struct Calibration {
    uint8_t rotation;    // absolute display rotation, 0..3
    float   sign;        // +1 / -1
    float   offsetDeg;
};

constexpr const char* kNvsNamespace = "monitor";

// Returns true when NVS held at least one field
bool loadCalibration(Calibration& c){
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK){
        return false;
    }
    bool any = false;
    uint8_t rot;
    if (nvs_get_u8(h, "rot", &rot) == ESP_OK){ c.rotation = rot & 3; any = true; }
    int8_t sign;
    if (nvs_get_i8(h, "sign", &sign) == ESP_OK){ c.sign = sign < 0 ? -1.0f : 1.0f; any = true; }
    int32_t mdeg;
    if (nvs_get_i32(h, "offset_mdeg", &mdeg) == ESP_OK){ c.offsetDeg = mdeg / 1000.0f; any = true; }
    nvs_close(h);
    return any;
}

bool saveCalibration(const Calibration& c, bool reset){
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK){
        return false;
    }
    esp_err_t err;
    if (reset){
        err = nvs_erase_all(h);
    } else {
        err = nvs_set_u8(h, "rot", c.rotation);
        if (err == ESP_OK) err = nvs_set_i8(h, "sign", c.sign < 0 ? -1 : 1);
        if (err == ESP_OK) err = nvs_set_i32(h, "offset_mdeg", static_cast<int32_t>(lroundf(c.offsetDeg * 1000.0f)));
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

// Puts a calibration into effect: display rotation and tilt mapping (redraws)
void applyCalibration(const Calibration& c, monitor::Ui& ui){
    M5.Display.setRotation(c.rotation);
    monitor::TiltConfig cfg = kTilt;
    cfg.angleSign   = c.sign;
    cfg.angleOffset = c.offsetDeg * kPi / 180.0f;
    ui.setTiltConfig(cfg);
}

// Framebuffer: 128x128 at 16 bpp = 32 KB of internal RAM
M5Canvas canvas(&M5.Display);

void writeLine(const char* msg, int n){
    if (n > 0){
        usb_serial_jtag_write_bytes(msg, n, pdMS_TO_TICKS(20));
    }
}

// Reply to "version": hardware and firmware identification
void sendVersion(){
    char msg[256];
    int n = monitor::formatVersionLine(msg, sizeof(msg) - 1, kBoard, kFeatures);
    if (n > static_cast<int>(sizeof(msg)) - 2){
        n = sizeof(msg) - 2;   // truncated: still terminate the line
    }
    msg[n++] = '\n';
    writeLine(msg, n);
}

const char* screenName(uint8_t brightness){
    if (brightness == 0) return "off";
    if (brightness == kDimBrightness) return "dim";
    return "on";
}

// Reply to the "status" command straight through the USB driver, so it
// works regardless of where the ESP-IDF console is routed
void sendStatus(const monitor::Ui& ui, const Calibration& cal, uint8_t brightness,
                float ax, float ay, float az){
    const char* sessions = ui.sessions();
    const char* link = !ui.linkArmed() ? "unarmed" : (ui.linkLost() ? "lost" : "ok");
    char msg[224];
    int n = snprintf(msg, sizeof(msg),
                     "STATUS state=%s subagents=%d rot=%u angle=%.1f ax=%.2f ay=%.2f az=%.2f"
                     " fw=%s board=%s sign=%d offset=%.1f sessions=%s link=%s work=%d screen=%s\n",
                     monitor::stateName(ui.state()), ui.subagents(), cal.rotation,
                     ui.tilt().angle * 180.0f / kPi, ax, ay, az,
                     esp_app_get_description()->version, kBoard,
                     cal.sign < 0 ? -1 : 1, cal.offsetDeg,
                     sessions[0] ? sessions : "-", link, ui.workFrames() / 30, screenName(brightness));
    writeLine(msg, n);
}

}  // namespace

extern "C" void app_main(void){
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(kBrightness);

    canvas.setColorDepth(16);
    canvas.createSprite(kCanvasSize, kCanvasSize);

    // NVS for the stored calibration (a version mismatch just wipes it)
    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND){
        nvs_flash_erase();
        nvsErr = nvs_flash_init();
    }
    const bool nvsOK = (nvsErr == ESP_OK);

    // Calibration: compiled defaults, then whatever "calibrate" stored
    const Calibration defaults = {
        static_cast<uint8_t>((M5.Display.getRotation() + kBootRotationOffset) & 3),
        kTilt.angleSign,
        kTilt.angleOffset * 180.0f / kPi,
    };
    Calibration cal = defaults;
    const bool stored = nvsOK && loadCalibration(cal);
    M5.Display.setRotation(cal.rotation);

    bool imuOK = M5.Imu.isEnabled();
    ESP_LOGI(TAG, "imu %s, rotation %u, sign %d, offset %.1f (%s)",
             imuOK ? "enabled" : "absent", cal.rotation, cal.sign < 0 ? -1 : 1, cal.offsetDeg,
             stored ? "from nvs" : "compiled");

    monitor::Icons icons(canvas, kCanvasSize);
    monitor::TiltConfig tiltCfg = kTilt;
    tiltCfg.angleSign   = cal.sign;
    tiltCfg.angleOffset = cal.offsetDeg * kPi / 180.0f;
    monitor::Ui ui(icons, tiltCfg);

    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    if (imuOK){
        // Prime the tilt so the first frame is already upright
        M5.Imu.update();
        M5.Imu.getAccel(&ax, &ay, &az);
        ui.feedAccel(ax, ay, az);
    }

    ui.apply(monitor::State::Idle);   // initial state

    // USB Serial/JTAG driver, to receive data from the computer
    usb_serial_jtag_driver_config_t usbCfg = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usbCfg));

    // Announce who we are once, through the driver: the console's USB
    // channel drops long lines when the host is not draining it, this path
    // does not, and the daemon logs every reboot with its version
    sendVersion();

    monitor::LineParser parser;
    std::string line;
    uint8_t buf[64];
    bool    screenOn   = true;    // the button's choice
    bool    autoOff    = false;   // switched off by the idle / no-host timer
    uint8_t brightness = kBrightness;

    while(true){
        M5.update();   // refreshes the button state

        // Tilt tracking: the static icons follow the gravity vector
        if (imuOK){
            M5.Imu.update();
            M5.Imu.getAccel(&ax, &ay, &az);
            ui.feedAccel(ax, ay, az);
        }

        // The 33 ms timeout blocks waiting for data (no busy-wait) and sets
        // the ~30 fps pace of the animations and the tilt filter
        int bytesRead = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(33));
        for (int i = 0; i < bytesRead; i++){
            if (!parser.feed(static_cast<char>(buf[i]), line)){
                continue;
            }
            auto cmd = monitor::parseCommand(line);
            if (cmd.kind != monitor::Command::Status && cmd.kind != monitor::Command::Version){
                ui.noteCommand();   // a query is not a sign of life from the state feed
            }
            switch (cmd.kind){
                case monitor::Command::SetState:
                    if (ui.apply(cmd.state)){
                        autoOff = false;   // something happened: wake the screen
                    }
                    break;
                case monitor::Command::SubagentStart: ui.subagentStart(); break;
                case monitor::Command::SubagentStop:  ui.subagentStop(); break;
                case monitor::Command::Sessions:      ui.setSessions(cmd.arg); break;
                case monitor::Command::Ping:          ui.ping(); break;
                case monitor::Command::Status:        sendStatus(ui, cal, brightness, ax, ay, az); break;
                case monitor::Command::Version:       sendVersion(); break;
                case monitor::Command::Calibrate: {
                    monitor::CalibrationRequest req;
                    if (!monitor::parseCalibration(cmd.arg, req)){
                        static const char kUsage[] =
                            "ERROR calibrate: expected rot=<0-3> sign=<1|-1> offset=<degrees>, or reset\n";
                        writeLine(kUsage, sizeof(kUsage) - 1);
                        break;
                    }
                    if (req.reset){
                        cal = defaults;
                    } else {
                        if (req.hasRotation) cal.rotation  = static_cast<uint8_t>(req.rotation);
                        if (req.hasSign)     cal.sign      = static_cast<float>(req.sign);
                        if (req.hasOffset)   cal.offsetDeg = req.offsetDeg;
                    }
                    bool saved = nvsOK && saveCalibration(cal, req.reset);
                    ESP_LOGI(TAG, "calibration %s: rot=%u sign=%d offset=%.1f (%s)",
                             req.reset ? "reset" : "set", cal.rotation, cal.sign < 0 ? -1 : 1,
                             cal.offsetDeg, saved ? "stored" : "not stored");
                    applyCalibration(cal, ui);
                    sendStatus(ui, cal, brightness, ax, ay, az);
                    break;
                }
                case monitor::Command::Event:   break;   // not shown on this board (and never sent to it)
                case monitor::Command::Unknown: break;
            }
        }

        // Screen policy: the button toggles; a long idle or a long silence
        // from the host switches off until something changes; a lost
        // heartbeat dims
        if (M5.BtnA.wasPressed()){
            if (autoOff){
                autoOff  = false;   // first press after an auto-off just wakes it
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
    }
}
