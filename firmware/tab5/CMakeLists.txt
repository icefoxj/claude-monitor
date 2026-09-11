# ESP-IDF project for the M5Stack Tab5 (ESP32-P4) build of claude-monitor.
# The board-independent logic lives in ../../components/monitor-core.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(claude-monitor-tab5)
