#include "monitor/theme.h"

#include <nvs.h>
#include <sdkconfig.h>

namespace monitor {

namespace {
constexpr const char* kNamespace = "display";
Theme active = Theme::Classic;
}

Theme activeTheme(){
    return active;
}

bool themeAvailable(Theme t){
#if defined(CONFIG_MONITOR_ANIME_THEME)
    (void)t;
    return true;
#else
    return t == Theme::Classic;
#endif
}

bool setActiveTheme(Theme t){
    if (!themeAvailable(t)) return false;
    active = t;
    return true;
}

bool loadTheme(Theme& t){
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK){
        return false;
    }
    uint8_t v;
    bool ok = nvs_get_u8(h, "theme", &v) == ESP_OK;
    nvs_close(h);
    if (ok) t = v == 1 ? Theme::Anime : Theme::Classic;
    return ok;
}

bool saveTheme(Theme t){
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK){
        return false;
    }
    esp_err_t err = nvs_set_u8(h, "theme", t == Theme::Anime ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

}  // namespace monitor
