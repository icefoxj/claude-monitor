#pragma once

#include "monitor/protocol.h"

namespace monitor {

// The theme every view draws with, read at draw time; Classic until the
// board sets another. The anime theme's fonts are only in builds with
// CONFIG_MONITOR_ANIME_THEME (the Tab5's); elsewhere it cannot be chosen.
Theme activeTheme();
bool themeAvailable(Theme t);

// False (and nothing changes) when this build does not have the theme
bool setActiveTheme(Theme t);

inline bool animeTheme(){ return activeTheme() == Theme::Anime; }

// Kept in NVS (namespace "display", key "theme"), apart from the
// calibration's namespace, which "calibrate reset" erases whole.
// initCalibrationStorage() must have run.
bool loadTheme(Theme& t);
bool saveTheme(Theme t);

}  // namespace monitor
