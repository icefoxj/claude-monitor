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
    switch (t){
        case Theme::Classic: return true;
#if defined(CONFIG_MONITOR_ANIME_THEME)
        case Theme::Anime: return true;
#endif
#if defined(CONFIG_MONITOR_LCARS_THEME)
        case Theme::Lcars: return true;
#endif
#if defined(CONFIG_MONITOR_GALACTIC_THEME)
        case Theme::Galactic: return true;
#endif
        default: return false;
    }
}

bool setActiveTheme(Theme t){
    if (!themeAvailable(t)) return false;
    active = t;
    return true;
}

Theme nextAvailableTheme(Theme t){
    Theme n = nextTheme(t);
    while (!themeAvailable(n)) n = nextTheme(n);   // Classic is always there
    return n;
}

bool loadTheme(Theme& t){
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK){
        return false;
    }
    uint8_t v;
    bool ok = nvs_get_u8(h, "theme", &v) == ESP_OK && v < kThemeCount;
    nvs_close(h);
    if (ok) t = static_cast<Theme>(v);
    return ok;
}

bool saveTheme(Theme t){
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK){
        return false;
    }
    esp_err_t err = nvs_set_u8(h, "theme", static_cast<uint8_t>(t));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

}  // namespace monitor
