// claude-monitor on the M5Stack AtomS3R (ESP32-S3): 128x128 IPS panel,
// BMI270 IMU, one button under the screen, USB Serial/JTAG for the protocol.
//
// Everything board-independent (protocol, icons, tilt filter, state machine)
// is in the monitor-core component; this file wires it to this hardware.

// FreeRTOS first: under ESP-IDF, FreeRTOS.h must be included before
// any header that pulls in task.h (M5Unified does)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <M5Unified.h>

#include <driver/usb_serial_jtag.h>
#include <esp_err.h>
#include <esp_log.h>

#include <cstdio>
#include <string>

#include "monitor/icons.h"
#include "monitor/protocol.h"
#include "monitor/tilt.h"
#include "monitor/ui.h"

namespace {

constexpr const char* TAG = "monitor";

constexpr int     kCanvasSize = 128;   // the whole panel
constexpr uint8_t kBrightness = 128;

// ---------------- orientation calibration for this unit ----------------

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

// Framebuffer: 128x128 at 16 bpp = 32 KB of internal RAM
M5Canvas canvas(&M5.Display);

// Reply to the "status" command straight through the USB driver, so it
// works regardless of where the ESP-IDF console is routed
void sendStatus(const monitor::Ui& ui, uint8_t rotation, float ax, float ay, float az){
    char msg[128];
    int n = snprintf(msg, sizeof(msg),
                     "STATUS state=%s subagents=%d rot=%u angle=%.1f ax=%.2f ay=%.2f az=%.2f\n",
                     monitor::stateName(ui.state()), ui.subagents(), rotation,
                     ui.tilt().angle * 180.0f / 3.14159265f, ax, ay, az);
    if (n > 0){
        usb_serial_jtag_write_bytes(msg, n, pdMS_TO_TICKS(20));
    }
}

}  // namespace

extern "C" void app_main(void){
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(kBrightness);

    canvas.setColorDepth(16);
    canvas.createSprite(kCanvasSize, kCanvasSize);

    // Boot orientation: apply the calibration offset before the first draw
    bool imuOK = M5.Imu.isEnabled();
    uint8_t rotation = (M5.Display.getRotation() + kBootRotationOffset) & 3;
    M5.Display.setRotation(rotation);
    ESP_LOGI(TAG, "imu %s, boot rotation %u", imuOK ? "enabled" : "absent", rotation);

    monitor::Icons icons(canvas, kCanvasSize);
    monitor::Ui ui(icons, kTilt);

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

    monitor::LineParser parser;
    std::string line;
    uint8_t buf[64];
    bool screenOn = true;

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
            if (parser.feed(static_cast<char>(buf[i]), line)){
                auto cmd = monitor::parseCommand(line);
                switch (cmd.kind){
                    case monitor::Command::SetState:      ui.apply(cmd.state); break;
                    case monitor::Command::SubagentStart: ui.subagentStart(); break;
                    case monitor::Command::SubagentStop:  ui.subagentStop(); break;
                    case monitor::Command::Status:        sendStatus(ui, rotation, ax, ay, az); break;
                    case monitor::Command::Unknown:       break;
                }
            }
        }

        ui.tick(screenOn);

        // Screen button: toggles the display on and off
        if (M5.BtnA.wasPressed()){
            screenOn = !screenOn;
            M5.Display.setBrightness(screenOn ? kBrightness : 0);
        }
    }
}
