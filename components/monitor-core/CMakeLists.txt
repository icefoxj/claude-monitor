# The anime theme's fonts (tools/make-fonts.py) go in only when the board
# enables the theme; its drawing code is always built and falls back to the
# classic look without them
set(anime_fonts)
if(CONFIG_MONITOR_ANIME_THEME)
    foreach(font cond18 cond22 cond26 cond32 cond40 cond56 mono14 mono16 mono20
                 gothic22 gothic32 mincho44 mincho96 mincho192)
        list(APPEND anime_fonts "fonts/${font}.vlw")
    endforeach()
endif()

idf_component_register(
    SRCS "src/protocol.cpp" "src/tilt.cpp" "src/orientation.cpp" "src/icons.cpp" "src/ui.cpp"
         "src/version.cpp" "src/calibration.cpp" "src/eventlog.cpp" "src/eventview.cpp" "src/screenshot.cpp"
         "src/usage.cpp" "src/usageview.cpp" "src/usagepage.cpp" "src/screensaver.cpp"
         "src/theme.cpp" "src/anime.cpp" "src/anime_icons.cpp" "src/anime_views.cpp"
    INCLUDE_DIRS "include"
    REQUIRES m5stack__m5unified
    PRIV_REQUIRES esp_app_format esp_hw_support esp_timer esp_system spi_flash nvs_flash
    EMBED_FILES ${anime_fonts})
