#include "monitor/version.h"

#include "monitor/protocol.h"

#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <sdkconfig.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace monitor {

namespace {

const char* resetName(esp_reset_reason_t r){
    switch (r){
        case ESP_RST_POWERON:    return "poweron";
        case ESP_RST_EXT:        return "ext";
        case ESP_RST_SW:         return "sw";
        case ESP_RST_PANIC:      return "panic";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:        return "wdt";
        case ESP_RST_DEEPSLEEP:  return "deepsleep";
        case ESP_RST_BROWNOUT:   return "brownout";
        case ESP_RST_SDIO:       return "sdio";
        case ESP_RST_USB:        return "usb";
        case ESP_RST_JTAG:       return "jtag";
        case ESP_RST_EFUSE:      return "efuse";
        case ESP_RST_PWR_GLITCH: return "glitch";
        case ESP_RST_CPU_LOCKUP: return "lockup";
        default:                 return "unknown";
    }
}

// "Sep 10 2026" + "19:00:00" (the compiler's __DATE__ / __TIME__ as stored
// in the app descriptor) -> "2026-09-10T19:00:00"
void isoBuildTime(const char* date, const char* time, char* out, size_t size){
    static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int month = 0;
    for (int i = 0; i < 12; i++){
        if (strncmp(kMonths + 3 * i, date, 3) == 0){
            month = i + 1;
            break;
        }
    }
    int day  = atoi(date + 4);
    int year = atoi(date + 7);
    snprintf(out, size, "%04d-%02d-%02dT%.8s", year, month, day, time);   // "HH:MM:SS"
}

}  // namespace

const char* boardModelName(m5::board_t board){
    switch (board){
        case m5::board_t::board_M5AtomS3:       return "M5Stack-AtomS3";
        case m5::board_t::board_M5AtomS3Lite:   return "M5Stack-AtomS3-Lite";
        case m5::board_t::board_M5AtomS3U:      return "M5Stack-AtomS3U";
        case m5::board_t::board_M5AtomS3R:      return "M5Stack-AtomS3R";
        case m5::board_t::board_M5AtomS3RCam:   return "M5Stack-AtomS3R-Cam";
        case m5::board_t::board_M5AtomS3RExt:   return "M5Stack-AtomS3R-Ext";
        case m5::board_t::board_M5Tab5:         return "M5Stack-Tab5";
        case m5::board_t::board_M5Tab5X:        return "M5Stack-Tab5X";
        case m5::board_t::board_unknown:        return "unknown";
        default:                                return "other";
    }
}

int formatVersionLine(char* buf, size_t size, const char* board, const char* features){
    const esp_app_desc_t* app = esp_app_get_description();

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flashBytes = 0;
    esp_flash_get_size(nullptr, &flashBytes);

    char sha[9] = {0};
    esp_app_get_elf_sha256(sha, sizeof(sha));

    char built[32];
    isoBuildTime(app->date, app->time, built, sizeof(built));

    long long uptime = esp_timer_get_time() / 1000000LL;

    return snprintf(buf, size,
                    "VERSION board=%s model=%s chip=%s rev=v%d.%d cores=%d flash=%uMB"
                    " fw=%s idf=%s m5unified=%d.%d.%d protocol=%d features=%s project=%s"
                    " built=%s sha=%s uptime=%lld reset=%s",
                    board, boardModelName(M5.getBoard()), CONFIG_IDF_TARGET,
                    chip.revision / 100, chip.revision % 100, chip.cores,
                    static_cast<unsigned>(flashBytes / (1024u * 1024u)),
                    app->version, app->idf_ver,
                    M5UNIFIED_VERSION_MAJOR, M5UNIFIED_VERSION_MINOR, M5UNIFIED_VERSION_PATCH,
                    kProtocolVersion, (features && features[0]) ? features : "-", app->project_name,
                    built, sha, uptime, resetName(esp_reset_reason()));
}

}  // namespace monitor
