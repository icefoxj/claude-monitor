idf_component_register(
    SRCS "src/protocol.cpp" "src/tilt.cpp" "src/icons.cpp" "src/ui.cpp" "src/version.cpp"
         "src/eventlog.cpp" "src/eventview.cpp"
    INCLUDE_DIRS "include"
    REQUIRES m5stack__m5unified
    PRIV_REQUIRES esp_app_format esp_hw_support esp_timer esp_system spi_flash)
