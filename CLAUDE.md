# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for an **M5Stack AtomS3R** (ESP32-S3, 128×128 IPS display, BMI270 IMU) that acts as a desk status light for Claude Code. Claude Code hooks send a one-word line over USB serial; the device draws the matching icon and keeps it upright as the cube is tilted. Native **ESP-IDF 5.5** (C++/CMake/FreeRTOS), no Arduino, no PlatformIO, no Wi-Fi, and no host-side component beyond a PowerShell script.

The whole firmware is a single file: `main/main.cpp`. `claude-code-status-monitor-atoms3r.md` is the original write-up (design rationale, icon geometry, hook configuration) — read it before non-trivial changes. The code has since moved past it on purpose: the waiting icon shows an exclamation mark instead of the word STOP, there is a blue question-mark sign for `AskUserQuestion`, the two "needs you" signs pulse on entry, the static icons rotate continuously with the tilt instead of in 90° steps, there is a `status` command, and the hook set is richer than the article's four events. Do not "fix" those back toward the article's listing. The README's hooks section is the current reference for the host side.

Machine-specific notes (toolchain paths, COM port, antivirus) belong in `CLAUDE.local.md`, which is gitignored.

## Toolchain and commands

`idf.py` only exists inside an activated ESP-IDF environment: the VS Code command **ESP-IDF: Open ESP-IDF Terminal**, the "ESP-IDF PowerShell" Start Menu shortcut on Windows, or `export.ps1` / `export.sh` from the ESP-IDF directory. Never prefix `idf.py` with `bash` on Windows (it hands the line to WSL). Keep the project path free of spaces.

```
idf.py set-target esp32s3     # only on a fresh checkout or after fullclean
idf.py build                  # first build downloads M5Unified/M5GFX into managed_components/
idf.py -p COM5 flash          # esptool over the USB-C virtual serial port (/dev/ttyACM0 on Linux)
idf.py -p COM5 monitor        # Ctrl+] to exit. CLOSE IT before sending protocol commands: the port is exclusive on Windows
idf.py menuconfig
idf.py fullclean
```

`idf.py flash` rebuilds first. A rebuild can take minutes on a machine whose antivirus scans the toolchain, so run it with a long timeout. First-ever flash of a board may require holding the side reset button ~2 s to enter download mode. The VS Code extension is configured with `idf.flashType: JTAG` (OpenOCD, `board/esp32s3-builtin.cfg`), so **ESP-IDF: Flash your Project** goes over the S3's built-in USB-JTAG instead; both paths use the same cable.

Manual end-to-end test (device flashed, monitor closed):

```powershell
.\Send-ClaudeState.ps1 -State processing -PortName COM5   # also: waiting_user | question | idle | off
```

Reading the device's reply to `status` needs a real serial session; the hook script only writes. In PowerShell: open `System.IO.Ports.SerialPort` on the port at 115200 with DTR/RTS off, `WriteLine("status")`, then `ReadLine()` until a line containing `STATUS` — boot-log fragments may precede it. Note that in a session where the hooks are active, every tool call that prompts for permission sends `waiting_user` before it runs, so a status read taken from Claude Code will usually report that state rather than the one you set a moment earlier.

There are no unit tests and no linter. `.clangd` strips `-f*`/`-m*` flags so clangd can parse the xtensa compile database in `build/compile_commands.json`. `.vscode/settings.json` is gitignored because it holds machine-specific paths (ESP-IDF setup, clangd binary and compile-commands directory, COM port); the ESP-IDF extension writes those entries when the setup, target and port are selected. `.vscode/c_cpp_properties.json` is tracked and also carries an absolute `compilerPath`.

## Architecture

**Serial protocol.** One command per line, `\n`-terminated, over the ESP32-S3's native **USB Serial/JTAG** peripheral (no UART bridge chip; opening the port does not reset the board). Commands: `processing`, `waiting_user`, `question`, `idle`, `off`, plus `status`, which is the only device→host traffic: one `STATUS state=… rot=… angle=… ax=… ay=… az=…` line written straight through the USB driver (independent of where the ESP-IDF console is routed). Trailing `\r`/spaces are stripped, lines over 64 bytes are discarded, unknown commands are ignored. Claude Code hooks invoke `Send-ClaudeState.ps1`: `SessionStart`/`Stop`/`StopFailure` → idle, `UserPromptSubmit`/`PostToolUse` → processing, `PreToolUse` on `AskUserQuestion` → question, `Notification` on `permission_prompt` → waiting_user, `SessionEnd` → off. The script must always `exit 0` because hook exit code 2 blocks Claude Code, and it retries a busy port because two async hooks can fire within milliseconds.

**Rendering.** Everything is drawn into a 128×128 16-bpp `M5Canvas` framebuffer (32 KB, internal RAM, PSRAM disabled) and pushed with `pushSprite` — never directly to `M5.Display`, which flickers. Icons are procedural (triangle fans, `thickLine` = oriented rect + two round caps, `thickArc` = polyline of those), no bitmaps. The static icons are defined relative to the canvas centre and every vertex goes through `rotated(x, y, angle, scale)`, so they can be drawn at any angle and size; the gear takes its own spin angle and ignores tilt. The two signs share `sign(body, angle, scale)` (white border octagon + coloured body). Colours are `constexpr rgb565(r,g,b)` so the palette is edited in 0–255 values.

**State machine.** `State {Idle, Processing, WaitingUser, Question, Off}` lives in a `Ui` struct together with the gear angle, the tilt filter and the pulse counter. `applyState` redraws only on change; `Processing` is the sole continuously animated state (`gearAngle += 0.10` per frame). `WaitingUser` and `Question` are the "needs attention" states: on entry they get `kAttentionFrames` (60) of a size pulse, `1 + kAttentionAmp·|sin(πk/kAttentionPeriod)|` = three +10 % breaths over ~2 s, with the last frame forced to scale 1. The static states are also redrawn when the tilt angle has moved ≥ 1° since the last draw, but not while a pulse is running (it redraws every frame anyway).

**Main loop timing.** There is no separate task or timer. `usb_serial_jtag_read_bytes(..., pdMS_TO_TICKS(33))` is both the blocking read and the ~30 fps clock for the animations and the tilt filter. Anything added to the loop must stay well under 33 ms.

**Auto-rotation.** Uses the accelerometer (gravity vector), not the gyro, and is continuous: `updateTilt` computes the icon angle as `kImuAngleSign * atan2(ay, ax) + kImuAngleOffset`, low-pass filters it (`kAngleSmoothing` 0.25 per 33 ms frame, wrap-aware), and reports the icon as stale once it has moved ≥ `kRedrawStep` (1°). Readings are ignored and the angle held when the module lies flat (`|az| > 0.80`) or the in-plane component is below `kTiltThreshold` (0.40). The display itself stays at the boot rotation; only the drawing rotates. Calibration knobs, all empirical (M5Unified does not remap the BMI270 axes on this board): `kBootRotationOffset` (steps of 90° clockwise added to the display default; the frame everything is drawn in and what shows when flat) and `kImuAngleSign` / `kImuAngleOffset`. The committed values (1, −1, 0) were verified on one unit in all four standing positions and under continuous tilt. To recheck, stand the cube on a side, send `status`, and compare `angle=` with what looks upright.

**Button.** `BtnA` (under the screen) toggles backlight brightness 128/0; it does not change state. While dark, the gear keeps animating logically and an entry pulse is skipped.

## Gotchas

- `freertos/FreeRTOS.h` must be included **before** `M5Unified.h` or the build fails with `#error include FreeRTOS.h must appear ...`.
- `sdkconfig` is gitignored; `sdkconfig.defaults` (generated with `idf.py save-defconfig`) holds the only non-defaults: target `esp32s3` and **flash size 8 MB**. Regenerate it with that command after changing anything in menuconfig that must persist. The console is UART0 primary with USB Serial/JTAG as secondary, which is the ESP-IDF default for the S3; that secondary channel is what makes logs visible over USB.
- ESP-IDF logs and the protocol share the same USB port. The host only writes (except for `status`), so it works, but the `STATUS` reply can arrive interleaved with log lines — filter on the `STATUS` prefix.
- The AtomS3R's backlight is driven by an I2C chip, not a GPIO; if the screen stays black with the code apparently running, suspect the M5Unified version (`^0.2` in `main/idf_component.yml`, locked at 0.2.21).
- The USB port can disappear for a few seconds after a reset or re-plug; a flash that fails with "could not open port" usually succeeds on retry.
- Claude Code rewrites `~/.claude/settings.json` itself (e.g. when it records a newly allowed directory or permission). A hooks block edited by hand during a live session can be lost in that rewrite; verify with `/hooks` or by re-reading the file, and re-apply if needed.
