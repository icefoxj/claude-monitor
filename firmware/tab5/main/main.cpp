// claude-monitor on the M5Stack Tab5 (ESP32-P4): 5" 1280x720 IPS panel
// with capacitive touch, 32 MB PSRAM, USB-C.
//
// Layout in landscape: the status icon on the left, and on the right the
// hook inspector: every field of the last Claude Code hook event the host
// forwarded, plus a short history. This is the raw material for the
// multi-session mode: first see what the hooks deliver, then design.
//
// Everything board-independent (protocol, icons, tilt filter, state
// machine, event log and view) is in the monitor-core component; this file
// wires it to this hardware. No tilt tracking and no calibration here: the
// tablet sits on its stand. The transport is the P4's USB Serial/JTAG,
// like the AtomS3R; whether the Tab5's USB-C reaches it, or only the
// high-speed OTG controller, is to be confirmed on hardware.

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

#include <cstdio>
#include <cstring>
#include <string>

#include "monitor/eventlog.h"
#include "monitor/eventview.h"
#include "monitor/icons.h"
#include "monitor/protocol.h"
#include "monitor/tilt.h"
#include "monitor/ui.h"
#include "monitor/version.h"

namespace {

constexpr const char* TAG       = "monitor";
constexpr const char* kBoard    = "tab5";
constexpr const char* kFeatures = "events,touch";   // the host streams hook events to us

// Landscape 1280x720: the icon in a 560 px column on the left, the
// inspector in the remaining 720 px
constexpr int kIconSize = 480;
constexpr int kIconX    = 40;
constexpr int kIconY    = 120;
constexpr int kViewX    = 560;
constexpr int kViewW    = 720;
constexpr int kViewH    = 720;

constexpr uint8_t kBrightness    = 160;
constexpr uint8_t kDimBrightness = 40;    // while the host's heartbeat is missing

// Idle, or without a host, for this long (frames at ~30 fps): screen off
// until the next state change or a tap
constexpr int kAutoOffFrames = 30 * 60 * 30;   // 30 min

// The inspector redraws on every event and state change, and once a
// second so the "ago" ages move
constexpr int kViewRefreshFrames = 30;

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

// Same shape as the AtomS3R's STATUS so the host's parsers work unchanged;
// no tilt on this board, so the angle and the calibration are constants
void sendStatus(const monitor::Ui& ui, const monitor::EventLog& log, uint8_t brightness){
    const char* sessions = ui.sessions();
    const char* link = !ui.linkArmed() ? "unarmed" : (ui.linkLost() ? "lost" : "ok");
    char msg[256];
    int n = snprintf(msg, sizeof(msg),
                     "STATUS state=%s subagents=%d rot=%u angle=0.0 ax=0.00 ay=0.00 az=0.00"
                     " fw=%s board=%s sign=1 offset=0.0 sessions=%s link=%s work=%d screen=%s events=%lu\n",
                     monitor::stateName(ui.state()), ui.subagents(), M5.Display.getRotation(),
                     esp_app_get_description()->version, kBoard,
                     sessions[0] ? sessions : "-", link, ui.workFrames() / 30, screenName(brightness),
                     static_cast<unsigned long>(log.count()));
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

    // The panel is 720x1280 portrait; we work in landscape
    if (M5.Display.width() < M5.Display.height()){
        M5.Display.setRotation(1);
    }
    M5.Display.setBrightness(kBrightness);
    M5.Display.fillScreen(TFT_BLACK);
    ESP_LOGI(TAG, "display %dx%d rotation %u", M5.Display.width(), M5.Display.height(), M5.Display.getRotation());

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

    monitor::Icons icons(iconCanvas, kIconSize, kIconX, kIconY);
    monitor::TiltConfig tiltCfg;   // unused: no accelerometer samples are fed
    monitor::Ui ui(icons, tiltCfg);
    monitor::EventLog log(16);
    monitor::EventView view(viewCanvas, kViewX, 0);

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
                case monitor::Command::Status:        sendStatus(ui, log, brightness); break;
                case monitor::Command::Version:       sendVersion(); break;
                case monitor::Command::Calibrate: {
                    static const char kNo[] = "ERROR calibrate: not supported on tab5 (no tilt tracking)\n";
                    writeLine(kNo, sizeof(kNo) - 1);
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
