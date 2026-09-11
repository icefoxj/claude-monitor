#!/usr/bin/env bash
# Builds and runs the monitor-core host tests with any C++17 compiler
# (CXX to override g++). No ESP-IDF needed. Exit code = number of failures.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p tests/host/build
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I components/monitor-core/include \
    tests/host/test_core.cpp \
    components/monitor-core/src/protocol.cpp \
    components/monitor-core/src/tilt.cpp \
    components/monitor-core/src/orientation.cpp \
    -o tests/host/build/test_core
tests/host/build/test_core
