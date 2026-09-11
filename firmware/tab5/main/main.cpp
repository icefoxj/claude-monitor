// claude-monitor on the M5Stack Tab5 (ESP32-P4): 5" 1280x720 IPS panel
// with capacitive touch, BMI270 IMU, speaker, 32 MB PSRAM, USB-C on the
// P4's USB Serial/JTAG.
//
// Multi-session view: one tile per live Claude Code session (up to six),
// the screen split by how many there are (1, 2, 2x2, 3x2). A tile shows
// the session's name above its status icon and, below it, a clock with
// the time the current icon has been up, reset on every change. A tap on
// a tile opens the detail page: the same icon, name and clock next to the
// hook inspector for that session (every field of its last hook event and
// a short history); a tap anywhere on the detail page goes back. A short
// two-tone beep marks a red icon appearing (permission wanted, or error).
//
// The whole display turns with gravity in 90-degree steps, like a tablet;
// which way the sensor maps to the panel is calibrated per unit with
// "calibrate rot=<upright rotation now> sign=<1|-1>" and stored in NVS.
//
// The host sends one "session" line per live session (state, label,
// subagents, tool flag) and "event" lines with the hook payloads; the
// aggregate state words the cube uses are still accepted and drive a
// single placeholder tile while no session lines have arrived (manual
// tests, older daemons).
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

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
constexpr const char* kFeatures = "events,touch,rotate,sessions";
constexpr float kPi = 3.14159265f;

constexpr int   kMaxSessions = 6;
constexpr int   kTileGap     = 16;     // px between an icon and the tile's side
constexpr float kIconScale   = 0.85f;  // the icon takes this much of the room the texts leave
constexpr int   kViewW       = 720;    // the inspector canvas on the detail page
constexpr int   kViewH       = 720;

// Name and clock: a bigger face when the tiles are tall, and a text band
// (above for the name, below for the clock) sized for it
struct TextStyle { const lgfx::IFont* font; int band; };

TextStyle textStyleFor(int tileHeight){
    if (tileHeight >= 600) return { &fonts::DejaVu40, 72 };
    if (tileHeight >= 300) return { &fonts::DejaVu24, 52 };
    return { &fonts::DejaVu18, 40 };
}

constexpr uint8_t kBrightness    = 160;
constexpr uint8_t kDimBrightness = 40;    // while the host's heartbeat is missing
constexpr uint8_t kVolume        = 128;

// Every tile idle, or no host, for this long (frames at ~30 fps): screen
// off until the next state change or a tap
constexpr int kAutoOffFrames = 30 * 60 * 30;   // 30 min

// Names, clocks and the inspector refresh once a second
constexpr int kTextRefreshFrames = 30;

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

struct Rect { int x = 0, y = 0, w = 0, h = 0; bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };

int64_t nowMs(){
    return esp_timer_get_time() / 1000;
}

// One session and everything that draws it. The canvas lives in PSRAM and
// is recreated at the size the current layout gives the tile.
struct Slot {
    std::string id;
    std::string label;
    M5Canvas canvas{&M5.Display};
    monitor::Icons icons;
    monitor::Ui ui;
    monitor::EventLog log{12};
    int  size = 0;        // canvas size in px, 0 = not created yet
    int  x = 0, y = 0;    // where the canvas is pushed
    Rect tile;            // the tile on screen, for touch and the texts
    bool used = false;

    Slot() : icons(canvas, 1, 0, 0), ui(icons, monitor::TiltConfig{}) {}

    void place(int newSize, int px, int py){
        if (newSize != size){
            if (size > 0){
                canvas.deleteSprite();
            }
            canvas.setPsram(true);
            canvas.setColorDepth(16);
            if (!canvas.createSprite(newSize, newSize)){
                ESP_LOGE(TAG, "canvas of %d px failed (PSRAM?)", newSize);
            }
            size = newSize;
        }
        x = px;
        y = py;
        icons.setSize(newSize, px, py);
    }

    void reset(){
        ui.setVisible(false);
        ui.apply(monitor::State::Off);
        id.clear();
        label.clear();
        log = monitor::EventLog(12);
        used = false;
    }
};

std::array<Slot, kMaxSessions> slots;
Slot placeholder;   // shown while no session lines have arrived; driven by the aggregate words
M5Canvas viewCanvas(&M5.Display);

enum class View { Grid, Detail };

// ---------------- serial ----------------

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

// ---------------- sound ----------------

// Two notes, the second five frames after the first, without blocking the loop
struct Beep {
    int   frame = -1;
    float f1 = 0, f2 = 0;
};
Beep beep;

void startBeep(monitor::State s){
    beep.frame = 0;
    if (s == monitor::State::Error){
        beep.f1 = 660; beep.f2 = 440;    // falling: something broke
    } else {
        beep.f1 = 880; beep.f2 = 1175;   // rising: your turn
    }
}

void tickBeep(){
    if (beep.frame < 0) return;
    if (beep.frame == 0) M5.Speaker.tone(beep.f1, 120);
    if (beep.frame == 5) M5.Speaker.tone(beep.f2, 180);
    if (++beep.frame > 6) beep.frame = -1;
}

// ---------------- text helpers ----------------

void clockText(int frames, char* out, size_t size){
    int s = frames / 30;
    if (s >= 3600){
        snprintf(out, size, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    } else {
        snprintf(out, size, "%02d:%02d", s / 60, s % 60);
    }
}

void drawCentred(const char* text, int cx, int cy, int bandW, const lgfx::IFont* font, uint16_t color){
    M5.Display.setFont(font);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(color, TFT_BLACK);
    int h = M5.Display.fontHeight() + 8;
    M5.Display.fillRect(cx - bandW / 2, cy - h / 2, bandW, h, TFT_BLACK);
    M5.Display.drawString(text, cx, cy);
}

int priorityOf(monitor::State s){
    switch (s){
        case monitor::State::Error:       return 6;
        case monitor::State::WaitingUser: return 5;
        case monitor::State::Question:    return 5;
        case monitor::State::Paused:      return 4;
        case monitor::State::Compacting:  return 3;
        case monitor::State::Processing:  return 2;
        case monitor::State::Idle:        return 1;
        case monitor::State::Off:         return 0;
    }
    return 0;
}

}  // namespace

extern "C" void app_main(void){
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(kBrightness);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Speaker.setVolume(kVolume);

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
    ESP_LOGI(TAG, "imu %s, speaker %s, orientation offset %d sign %d (%s)",
             imuOK ? "enabled" : "absent", M5.Speaker.isEnabled() ? "enabled" : "absent",
             orientCfg.offset, orientCfg.sign, stored ? "from nvs" : "compiled");

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
    ESP_LOGI(TAG, "display %dx%d rotation %d", M5.Display.width(), M5.Display.height(), rotation);

    viewCanvas.setPsram(true);
    viewCanvas.setColorDepth(16);
    if (!viewCanvas.createSprite(kViewW, kViewH)){
        ESP_LOGE(TAG, "inspector canvas allocation failed (PSRAM?)");
    }
    monitor::EventView inspector(viewCanvas, 560, 0);

    placeholder.label = "claude";
    placeholder.used  = true;
    placeholder.ui.setVisible(false);
    placeholder.ui.apply(monitor::State::Idle);
    for (auto& s : slots){
        s.ui.setVisible(false);
    }

    View  view       = View::Grid;
    Slot* detail     = nullptr;
    bool  relayout   = true;
    bool  textDirty  = true;
    bool  viewDirty  = true;
    Rect  detailLabel, detailClock;   // text bands on the detail page
    TextStyle style = textStyleFor(720);   // for the layout on screen

    // The tiles on screen: the live sessions, or the placeholder
    auto visibleSlots = [&]() {
        std::vector<Slot*> v;
        for (auto& s : slots) if (s.used) v.push_back(&s);
        if (v.empty()) v.push_back(&placeholder);
        return v;
    };

    auto findSlot = [&](const std::string& id) -> Slot* {
        for (auto& s : slots) if (s.used && s.id == id) return &s;
        return nullptr;
    };

    auto allocSlot = [&](const std::string& id) -> Slot* {
        for (auto& s : slots){
            if (!s.used){
                s.used = true;
                s.id = id;
                s.ui.setVisible(false);
                s.ui.apply(monitor::State::Idle);
                return &s;
            }
        }
        ESP_LOGW(TAG, "session %s ignored: %d tiles already", id.c_str(), kMaxSessions);
        return nullptr;
    };

    auto endSlot = [&](Slot* s) {
        if (detail == s){
            detail = nullptr;
            view = View::Grid;
        }
        s->reset();
    };

    // Lays the tiles (or the detail page) out for the current rotation
    auto applyLayout = [&]() {
        const int W = M5.Display.width();
        const int H = M5.Display.height();
        const bool landscape = W > H;
        M5.Display.fillScreen(TFT_BLACK);
        for (auto& s : slots) s.ui.setVisible(false);
        placeholder.ui.setVisible(false);

        if (view == View::Detail && detail && detail->used){
            // The icon column is 560 wide in landscape (720 tall), 720 wide
            // and 560 tall in portrait: name band, icon, clock band
            style = textStyleFor(720);
            int colX, colY, colW, colH;
            if (landscape){ colX = 0;  colY = 0;  colW = 560; colH = 720; inspector.setOrigin(560, 0); }
            else          { colX = 0;  colY = 0;  colW = 720; colH = 560; inspector.setOrigin(0, 560); }
            int room = colH - 2 * style.band;
            if (colW - 2 * kTileGap < room) room = colW - 2 * kTileGap;
            int iconSize = static_cast<int>(room * kIconScale) & ~1;
            int ix = colX + (colW - iconSize) / 2;
            int iy = colY + style.band + (colH - 2 * style.band - iconSize) / 2;
            detailLabel = { colX, colY, colW, style.band };
            detailClock = { colX, colY + colH - style.band, colW, style.band };
            detail->place(iconSize, ix, iy);
            detail->ui.setVisible(true);
            detail->ui.redraw();
            viewDirty = true;
        } else {
            view = View::Grid;
            auto vis = visibleSlots();
            const int n = static_cast<int>(vis.size());
            int cols, rows;
            if (n <= 1)      { cols = 1; rows = 1; }
            else if (n == 2) { cols = landscape ? 2 : 1; rows = landscape ? 1 : 2; }
            else if (n <= 4) { cols = 2; rows = 2; }
            else             { cols = landscape ? 3 : 2; rows = landscape ? 2 : 3; }
            const int tw = W / cols;
            const int th = H / rows;
            style = textStyleFor(th);
            int room = th - 2 * style.band;
            if (tw - 2 * kTileGap < room) room = tw - 2 * kTileGap;
            int iconSize = static_cast<int>(room * kIconScale) & ~1;
            for (int i = 0; i < n; i++){
                Slot* s = vis[i];
                s->tile = { (i % cols) * tw, (i / cols) * th, tw, th };
                int ix = s->tile.x + (tw - iconSize) / 2;
                int iy = s->tile.y + style.band + (th - 2 * style.band - iconSize) / 2;
                s->place(iconSize, ix, iy);
                s->ui.setVisible(true);
                s->ui.redraw();
            }
        }
        textDirty = true;
    };

    // Names and clocks (and the inspector on the detail page)
    auto drawTexts = [&]() {
        char clock[16];
        if (view == View::Detail && detail){
            clockText(detail->ui.framesInState(), clock, sizeof(clock));
            drawCentred(detail->label.c_str(), detailLabel.x + detailLabel.w / 2, detailLabel.y + detailLabel.h / 2,
                        detailLabel.w, style.font, TFT_WHITE);
            drawCentred(clock, detailClock.x + detailClock.w / 2, detailClock.y + detailClock.h / 2,
                        detailClock.w, style.font, TFT_LIGHTGREY);
        } else {
            for (Slot* s : visibleSlots()){
                clockText(s->ui.framesInState(), clock, sizeof(clock));
                drawCentred(s->label.c_str(), s->tile.x + s->tile.w / 2, s->tile.y + style.band / 2,
                            s->tile.w, style.font, TFT_WHITE);
                drawCentred(clock, s->tile.x + s->tile.w / 2, s->tile.y + s->tile.h - style.band / 2,
                            s->tile.w, style.font, TFT_LIGHTGREY);
            }
        }
    };

    auto drawInspector = [&]() {
        if (view != View::Detail || !detail) return;
        char status[128];
        const char* link = !detail->ui.linkArmed() ? "no host yet" : (detail->ui.linkLost() ? "host lost" : "host ok");
        snprintf(status, sizeof(status), "%s%s   %s",
                 monitor::stateName(detail->ui.state()), detail->ui.toolRunning() ? " (tool running)" : "", link);
        inspector.draw(detail->log, status, nowMs());
    };

    // A state change on a visible tile: wake the screen, beep on red
    auto applyState = [&](Slot* s, monitor::State st, bool& woke) {
        if (s->ui.apply(st)){
            woke = true;
            textDirty = true;
            if (s->ui.visible() && (st == monitor::State::WaitingUser || st == monitor::State::Error)){
                startBeep(st);
            }
        }
    };

    // Highest-priority state among the tiles on screen
    auto aggregateState = [&]() {
        monitor::State best = monitor::State::Off;
        for (Slot* s : visibleSlots()){
            if (priorityOf(s->ui.state()) > priorityOf(best)) best = s->ui.state();
        }
        return best;
    };

    auto sendStatus = [&](uint8_t brightness) {
        std::string codes;
        int events = 0;
        for (auto& s : slots){
            if (s.used){
                codes += monitor::sessionCode(s.ui.state());
                events += static_cast<int>(s.log.count());
            }
        }
        monitor::State agg = aggregateState();
        bool tool = false, lost = false, armed = false;
        int work = 0;
        for (Slot* s : visibleSlots()){
            if (s->ui.state() == agg){ tool = tool || s->ui.toolRunning(); work = s->ui.workFrames() / 30; }
            lost = lost || s->ui.linkLost();
            armed = armed || s->ui.linkArmed();
        }
        const char* link = !armed ? "unarmed" : (lost ? "lost" : "ok");
        char msg[320];
        int n = snprintf(msg, sizeof(msg),
                         "STATUS state=%s subagents=0 rot=%u angle=%.1f ax=%.2f ay=%.2f az=%.2f"
                         " fw=%s board=%s sign=%d offset=%d sessions=%s link=%s work=%d screen=%s tool=%d"
                         " events=%d sector=%d tiles=%d view=%s\n",
                         monitor::stateName(agg), M5.Display.getRotation(),
                         atan2f(ay, ax) * 180.0f / kPi, ax, ay, az,
                         esp_app_get_description()->version, kBoard,
                         orient.config().sign, orient.config().offset,
                         codes.empty() ? "-" : codes.c_str(), link, work, screenName(brightness), tool ? 1 : 0,
                         events, orient.sector(), static_cast<int>(visibleSlots().size()),
                         view == View::Detail ? "detail" : "grid");
        writeLine(msg, n);
    };

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
    bool    autoOff    = false;   // switched off by the idle / no-host timer
    uint8_t brightness = kBrightness;
    int     frame      = 0;
    monitor::State lastAggregate = monitor::State::Off;
    int     aggregateFrames = 0;

    while(true){
        M5.update();   // refreshes the touch state

        // Gravity decides the display rotation, in 90-degree steps
        if (imuOK){
            M5.Imu.update();
            M5.Imu.getAccel(&ax, &ay, &az);
            if (orient.update(ax, ay, az) && orient.rotation() != rotation){
                rotation = orient.rotation();
                M5.Display.setRotation(rotation);
                ESP_LOGI(TAG, "rotation %d (sector %d)", rotation, orient.sector());
                relayout = true;
            }
        }

        // The 33 ms timeout blocks waiting for data (no busy-wait) and sets
        // the ~30 fps pace of the animations
        bool woke = false;
        int bytesRead = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(33));
        for (int i = 0; i < bytesRead; i++){
            if (!parser.feed(static_cast<char>(buf[i]), line)){
                continue;
            }
            auto cmd = monitor::parseCommand(line);
            if (cmd.kind != monitor::Command::Status && cmd.kind != monitor::Command::Version){
                for (auto& s : slots) s.ui.noteCommand();
                placeholder.ui.noteCommand();
            }
            switch (cmd.kind){
                case monitor::Command::Session: {
                    monitor::HookEvent ev;
                    if (!monitor::parseEventLine(cmd.arg, ev)) break;
                    Slot* s = findSlot(ev.name);
                    if (!s){
                        s = allocSlot(ev.name);
                        if (!s) break;
                        relayout = true;
                    }
                    const char* label = ev.find("label");
                    if (label && label[0] && s->label != label){
                        s->label = label;
                        textDirty = true;
                    }
                    monitor::State st;
                    const char* sn = ev.find("state");
                    if (sn && monitor::parseState(sn, st)){
                        applyState(s, st, woke);
                    }
                    const char* sa = ev.find("subagents");
                    if (sa) s->ui.setSubagents(atoi(sa));
                    const char* tl = ev.find("tool");
                    if (tl) s->ui.setToolRunning(tl[0] == '1');
                    break;
                }
                case monitor::Command::SessionEnd: {
                    Slot* s = findSlot(cmd.arg);
                    if (s){
                        endSlot(s);
                        relayout = true;
                    }
                    break;
                }
                case monitor::Command::SessionClear:
                    for (auto& s : slots) if (s.used) endSlot(&s);
                    relayout = true;
                    break;
                case monitor::Command::Event: {
                    monitor::HookEvent ev;
                    if (!monitor::parseEventLine(cmd.arg, ev)) break;
                    const char* sid = ev.find("session_id");
                    Slot* s = sid ? findSlot(sid) : nullptr;
                    if (!s && sid && ev.name != "SessionEnd"){
                        s = allocSlot(sid);   // the event beat its session line
                        if (s){
                            const char* p = ev.find("project");
                            s->label = p ? p : "";
                            relayout = true;
                        }
                    }
                    if (s){
                        s->log.push(std::move(ev), nowMs());
                        if (view == View::Detail && detail == s) viewDirty = true;
                    }
                    break;
                }
                case monitor::Command::SetState:      applyState(&placeholder, cmd.state, woke); break;
                case monitor::Command::SubagentStart: placeholder.ui.subagentStart(); break;
                case monitor::Command::SubagentStop:  placeholder.ui.subagentStop(); break;
                case monitor::Command::ToolStart:     placeholder.ui.toolStart(); break;
                case monitor::Command::ToolStop:      placeholder.ui.toolStop(); break;
                case monitor::Command::Sessions:      break;   // the aggregate codes: the tiles say it
                case monitor::Command::Ping:
                    for (auto& s : slots) s.ui.ping();
                    placeholder.ui.ping();
                    break;
                case monitor::Command::Status:        sendStatus(brightness); break;
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
                        rotation = orient.rotation();
                        M5.Display.setRotation(rotation);
                        relayout = true;
                    }
                    sendStatus(brightness);
                    break;
                }
                case monitor::Command::Unknown: break;
            }
        }

        // Touch: wake, open a tile's detail page, or leave it
        auto t = M5.Touch.getDetail();
        if (t.wasClicked()){
            if (autoOff){
                autoOff = false;
            } else if (view == View::Detail){
                view = View::Grid;
                detail = nullptr;
                relayout = true;
            } else {
                for (Slot* s : visibleSlots()){
                    if (s->tile.contains(t.x, t.y)){
                        detail = s;
                        view = View::Detail;
                        relayout = true;
                        break;
                    }
                }
            }
        }
        if (woke){
            autoOff = false;
        }

        if (relayout){
            applyLayout();
            relayout = false;
        }

        // Screen policy: everything idle for 30 min, or no host for 30 min,
        // switches off until something changes or a tap; a lost heartbeat dims
        monitor::State agg = aggregateState();
        if (agg != lastAggregate){
            lastAggregate = agg;
            aggregateFrames = 0;
        } else {
            ++aggregateFrames;
        }
        bool lost = false;
        int  silent = 0;
        for (Slot* s : visibleSlots()){
            lost = lost || s->ui.linkLost();
            if (s->ui.framesSinceCommand() > silent) silent = s->ui.framesSinceCommand();
        }
        bool idleLong = agg == monitor::State::Idle && aggregateFrames >= kAutoOffFrames;
        bool hostGone = lost && silent >= kAutoOffFrames;
        if (!autoOff && (idleLong || hostGone)){
            autoOff = true;
            ESP_LOGI(TAG, "screen off: %s", idleLong ? "idle for 30 min" : "no host for 30 min");
        }
        uint8_t want = autoOff ? 0 : (lost ? kDimBrightness : kBrightness);
        if (want != brightness){
            brightness = want;
            M5.Display.setBrightness(brightness);
        }
        const bool displayOn = !autoOff;

        for (auto& s : slots) s.ui.tick(displayOn && s.ui.visible());
        placeholder.ui.tick(displayOn && placeholder.ui.visible());
        tickBeep();

        ++frame;
        if (displayOn && (textDirty || frame % kTextRefreshFrames == 0)){
            drawTexts();
            textDirty = false;
        }
        if (displayOn && view == View::Detail && (viewDirty || frame % kTextRefreshFrames == 0)){
            drawInspector();
            viewDirty = false;
        }
    }
}
