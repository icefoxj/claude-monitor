# The themes' fonts (tools/make-fonts.py) go in only when the board enables
# the theme; the drawing code is always built and falls back to the classic
# look without them
set(theme_fonts)
if(CONFIG_MONITOR_ANIME_THEME)
    foreach(font cond18 cond22 cond26 cond32 cond40 cond56 mono14 mono16 mono20
                 gothic22 gothic32 mincho44 mincho96 mincho192)
        list(APPEND theme_fonts "fonts/${font}.vlw")
    endforeach()
endif()
if(CONFIG_MONITOR_LCARS_THEME)
    foreach(font lcars16 lcars20 lcars26 lcars34 lcars44 lcars60 lcars90 lcars140)
        list(APPEND theme_fonts "fonts/${font}.vlw")
    endforeach()
endif()
if(CONFIG_MONITOR_GALACTIC_THEME)
    foreach(font gal16 gal20 gal26 gal34 gal44 gal60 gal90)
        list(APPEND theme_fonts "fonts/${font}.vlw")
    endforeach()
endif()

idf_component_register(
    SRCS "src/protocol.cpp" "src/tilt.cpp" "src/orientation.cpp" "src/icons.cpp" "src/ui.cpp"
         "src/version.cpp" "src/calibration.cpp" "src/eventlog.cpp" "src/eventview.cpp" "src/screenshot.cpp"
         "src/usage.cpp" "src/usageview.cpp" "src/usagepage.cpp" "src/screensaver.cpp"
         "src/theme.cpp" "src/glyphs.cpp" "src/anime.cpp" "src/anime_icons.cpp" "src/anime_views.cpp"
         "src/lcars.cpp" "src/lcars_icons.cpp" "src/lcars_views.cpp"
         "src/galactic.cpp" "src/galactic_icons.cpp" "src/galactic_views.cpp"
    INCLUDE_DIRS "include"
    REQUIRES m5stack__m5unified
    PRIV_REQUIRES esp_app_format esp_hw_support esp_timer esp_system spi_flash nvs_flash
    EMBED_FILES ${theme_fonts})
