# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for M5Stack devices that act as a desk status light for Claude Code. Claude Code hooks send a one-word line over USB serial; the device draws the matching icon and keeps it upright as it is tilted. Native **ESP-IDF 5.5** (C++/CMake/FreeRTOS), no Arduino, no PlatformIO, no Wi-Fi, and no host-side component beyond a PowerShell script. The first and reference board is the **AtomS3R** (ESP32-S3, 128×128 IPS display, BMI270 IMU); the layout is built so a second board (planned: Tab5, ESP32-P4) is another small project sharing the same component.

`claude-code-status-monitor-atoms3r.md` is the original write-up (design rationale, icon geometry, hook configuration) — read it before non-trivial changes. The code has since moved past it on purpose: the waiting icon shows an exclamation mark instead of the word STOP, there is a blue question-mark sign for `AskUserQuestion`, the two "needs you" signs pulse on entry, the static icons rotate continuously with the tilt instead of in 90° steps, there is a `status` command, the hook set is richer than the article's four events, and the code is split into a component plus per-board projects instead of one `main.cpp`. Do not "fix" those back toward the article's listing. The README's hooks section is the current reference for the host side.

Machine-specific notes (toolchain paths, COM port, antivirus) belong in `CLAUDE.local.md`, which is gitignored.

## Layout

- `components/monitor-core/` — everything board-independent, in namespace `monitor`: `protocol.h` (states, command parsing, `LineParser`), `tilt.h` (accelerometer → icon angle, `TiltConfig`), `icons.h` (`Icons`: procedural drawing into an `M5Canvas`), `ui.h` (`Ui`: state machine, gear spin, entry pulse). Its `idf_component.yml` declares `m5stack/m5unified ^0.2` as a **public** dependency, because `icons.h` exposes `M5Canvas`.
- `firmware/atoms3r/` — the ESP-IDF project for the AtomS3R: `CMakeLists.txt` adds `../../components` via `EXTRA_COMPONENT_DIRS`; `main/main.cpp` holds only board wiring (canvas size, boot rotation and IMU calibration constants, `usb_serial_jtag` transport, `status` reply formatting, `BtnA` screen toggle, the 33 ms loop); `sdkconfig.defaults` carries the two non-defaults (target `esp32s3`, 8 MB flash). Build artefacts (`build/`, `managed_components/`, `sdkconfig`, `.vscode/settings.json`) live here and are gitignored.
- `host/Send-ClaudeState.ps1` — the Windows hook helper. Moving it means updating every hook path in `~/.claude/settings.json` and the README.
- `claude-monitor.code-workspace` — multi-root workspace (repo + `firmware/atoms3r`) so the ESP-IDF extension finds a project.

A new board = a new `firmware/<board>/` project with its own `sdkconfig.defaults`, `main/main.cpp` and calibration constants, plus a section in the README. Anything that would be duplicated between boards belongs in `monitor-core`.

## Toolchain and commands

`idf.py` only exists inside an activated ESP-IDF environment: the VS Code command **ESP-IDF: Open ESP-IDF Terminal**, the "ESP-IDF PowerShell" Start Menu shortcut on Windows, or `export.ps1` / `export.sh` from the ESP-IDF directory. Never prefix `idf.py` with `bash` on Windows (it hands the line to WSL). Keep the project path free of spaces. Run from the board's project directory, or pass it with `-C`:

```
cd firmware/atoms3r
idf.py set-target esp32s3     # only on a fresh checkout or after fullclean
idf.py build                  # first build downloads M5Unified/M5GFX into managed_components/
idf.py -p COM5 flash          # esptool over the USB-C virtual serial port (/dev/ttyACM0 on Linux)
idf.py -p COM5 monitor        # Ctrl+] to exit. CLOSE IT before sending protocol commands: the port is exclusive on Windows
idf.py menuconfig
idf.py fullclean
```

`idf.py flash` rebuilds first. A rebuild can take minutes on a machine whose antivirus scans the toolchain, so run it with a long timeout. First-ever flash of a board may require holding the side reset button ~2 s to enter download mode. The VS Code extension is configured with `idf.flashType: JTAG` (OpenOCD, `board/esp32s3-builtin.cfg`), so **ESP-IDF: Flash your Project** goes over the S3's built-in USB-JTAG instead; both paths use the same cable. The binary is `build/claude-monitor-atoms3r.bin`.

Manual end-to-end test (device flashed, monitor closed):

```powershell
.\host\Send-ClaudeState.ps1 -State processing -PortName COM5   # also: waiting_user | question | idle | off
```

Reading the device's reply to `status` needs a real serial session; the hook script only writes. In PowerShell: open `System.IO.Ports.SerialPort` on the port at 115200 with DTR/RTS off, `WriteLine("status")`, then `ReadLine()` until a line containing `STATUS` — boot-log fragments may precede it. Note that in a session where the hooks are active, every tool call that prompts for permission sends `waiting_user` before it runs, so a status read taken from Claude Code will usually report that state rather than the one you set a moment earlier.

There are no unit tests and no linter. `.clangd` (repo root) strips `-f*`/`-m*` flags so clangd can parse the xtensa compile database; `firmware/atoms3r/.vscode/settings.json` (gitignored, machine-specific: ESP-IDF setup, clangd binary and `--compile-commands-dir`, COM port) points it at `firmware/atoms3r/build`. `firmware/atoms3r/.vscode/c_cpp_properties.json` is tracked and carries an absolute `compilerPath`.

## Architecture

**Serial protocol** (`protocol.h`). One command per line, `\n`-terminated. Commands: `processing`, `waiting_user`, `question`, `idle`, `off`, plus `status`, the only device→host traffic: one `STATUS state=… rot=… angle=… ax=… ay=… az=…` line. `LineParser` strips trailing `\r`/spaces and discards lines over 64 bytes; `parseCommand` ignores unknown words. The transport is the board's: on the AtomS3R it is the ESP32-S3's native **USB Serial/JTAG** peripheral (no UART bridge chip; opening the port does not reset the board), and the `STATUS` reply is written straight through that driver so it does not depend on where the ESP-IDF console is routed. Claude Code hooks invoke `host/Send-ClaudeState.ps1`: `SessionStart`/`Stop`/`StopFailure` → idle, `UserPromptSubmit`/`PostToolUse` → processing, `PreToolUse` on `AskUserQuestion` → question, `Notification` on `permission_prompt` → waiting_user, `SessionEnd` → off. The script must always `exit 0` because hook exit code 2 blocks Claude Code, and it retries a busy port because two async hooks can fire within milliseconds.

**Rendering** (`icons.h`). `Icons` draws into a square 16-bpp `M5Canvas` supplied by the board (128×128 = 32 KB internal RAM on the AtomS3R) and pushes it with `pushSprite` at a configurable origin — never straight to `M5.Display`, which flickers. Icons are procedural (triangle fans, `thickLine` = oriented rect + two round caps, `thickArc` = polyline of those), no bitmaps. All geometry is written in "128-space" and multiplied by `unit_ = size/128`, so a bigger canvas just scales. Static icons go through `rotated(x, y, angle, scale)` vertex by vertex, hence any angle and size; the gear takes its own spin angle and ignores tilt. The two signs share `sign(body, angle, scale)`. Colours are `constexpr rgb565(r,g,b)`.

**State machine** (`ui.h`). `State {Idle, Processing, WaitingUser, Question, Off}`. `Ui::apply` redraws only on change; `Processing` is the sole continuously animated state (`kGearStep` 0.10 rad per frame). `WaitingUser` and `Question` are the "needs attention" states: on entry they get `AttentionConfig::frames` (60) of a size pulse, `1 + amp·|sin(πk/period)|` = three +10 % breaths over ~2 s, with the last frame forced to scale 1. `feedAccel` redraws a static state when the tilt moved ≥ 1° since the last draw, but not while a pulse is running. `tick(screenOn)` does the per-frame work; with the screen dark it draws nothing and drops a pending pulse.

**Main loop timing** (board). No separate task or timer. `usb_serial_jtag_read_bytes(..., pdMS_TO_TICKS(33))` is both the blocking read and the ~30 fps clock for the animations and the tilt filter. Anything added to the loop must stay well under 33 ms.

**Auto-rotation** (`tilt.h`). Accelerometer (gravity vector), not the gyro, and continuous: `updateTilt` computes the icon angle as `angleSign * atan2(ay, ax) + angleOffset`, low-pass filters it (`smoothing` 0.25 per frame, wrap-aware), and reports the icon as stale once it has moved ≥ `redrawStep` (1°). Readings are ignored and the angle held when the module lies flat (`|az| > flatThreshold` 0.80) or the in-plane component is below `tiltThreshold` (0.40). The display stays at the boot rotation; only the drawing rotates. Calibration is per board, in its `main.cpp`: `kBootRotationOffset` (steps of 90° clockwise added to the display default; the frame everything is drawn in and what shows when flat) and `TiltConfig::angleSign` / `angleOffset`. M5Unified does not remap the BMI270 axes, so these are empirical; the AtomS3R values (1, −1, 0) were verified in all four standing positions and under continuous tilt. To recheck, stand the device on a side, send `status`, and compare `angle=` with what looks upright.

**Button** (board). On the AtomS3R, `BtnA` (under the screen) toggles backlight brightness 128/0; it does not change state.

## Gotchas

- `freertos/FreeRTOS.h` must be included **before** `M5Unified.h` or the build fails with `#error include FreeRTOS.h must appear ...`.
- `sdkconfig` is gitignored; `firmware/<board>/sdkconfig.defaults` (generated with `idf.py save-defconfig`) holds the only non-defaults. For the AtomS3R: target `esp32s3` and **flash size 8 MB**. Regenerate it with that command after changing anything in menuconfig that must persist. The console is UART0 primary with USB Serial/JTAG as secondary, which is the ESP-IDF default for the S3; that secondary channel is what makes logs visible over USB.
- The component manager resolves manifests from every component in the build, including `components/monitor-core`; `dependencies.lock` and `managed_components/` are still created in the project directory (`firmware/atoms3r/`). If the dependency ever looks missing, check that `EXTRA_COMPONENT_DIRS` in the project `CMakeLists.txt` points at `../../components`.
- ESP-IDF logs and the protocol share the same USB port. The host only writes (except for `status`), so it works, but the `STATUS` reply can arrive interleaved with log lines — filter on the `STATUS` prefix.
- The AtomS3R's backlight is driven by an I2C chip, not a GPIO; if the screen stays black with the code apparently running, suspect the M5Unified version (`^0.2`, locked at 0.2.21).
- The USB port can disappear for a few seconds after a reset or re-plug; a flash that fails with "could not open port" usually succeeds on retry.
- Claude Code rewrites `~/.claude/settings.json` itself (e.g. when it records a newly allowed directory or permission). A hooks block edited by hand during a live session can be lost in that rewrite; verify with `/hooks` or by re-reading the file, and re-apply if needed.
