#include "monitor/calibration.h"

#include <nvs.h>
#include <nvs_flash.h>

#include <cmath>

namespace monitor {

namespace {
constexpr const char* kNamespace = "monitor";
}

bool initCalibrationStorage(){
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND){
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

bool loadCalibration(Calibration& c){
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK){
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
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK){
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

}  // namespace monitor
