#pragma once

#include <cstdint>

#include "monitor/protocol.h"

namespace monitor {

// The theme every view draws with, read at draw time; Classic until the
// board sets another. A theme's fonts are only in builds that enable it
// (CONFIG_MONITOR_ANIME_THEME, CONFIG_MONITOR_LCARS_THEME,
// CONFIG_MONITOR_GALACTIC_THEME: the Tab5's; the mech theme has no fonts,
// CONFIG_MONITOR_MECH_THEME only makes it selectable);
// elsewhere it cannot be chosen.
Theme activeTheme();
bool themeAvailable(Theme t);

// False (and nothing changes) when this build does not have the theme
bool setActiveTheme(Theme t);

// The next theme this build has after `t` (the switch on the usage page)
Theme nextAvailableTheme(Theme t);

inline bool animeTheme(){ return activeTheme() == Theme::Anime; }
inline bool lcarsTheme(){ return activeTheme() == Theme::Lcars; }
inline bool galacticTheme(){ return activeTheme() == Theme::Galactic; }
inline bool mechTheme(){ return activeTheme() == Theme::Mech; }

// What the screen shows behind everything in the active theme: black, but
// for the mech theme's graphite (RGB565)
uint16_t themeBackground();

// Kept in NVS (namespace "display", key "theme"), apart from the
// calibration's namespace, which "calibrate reset" erases whole.
// initCalibrationStorage() must have run.
bool loadTheme(Theme& t);
bool saveTheme(Theme t);

}  // namespace monitor
