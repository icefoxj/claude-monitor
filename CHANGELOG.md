# Changelog

Versions are annotated git tags (`vX.Y.Z`) on `main`; each one has a [GitHub release](https://github.com/icefoxj/claude-monitor/releases) with prebuilt images: the AtomS3R's since 1.0.0, the Tab5's since 1.3.0. Dates are release dates.

## Unreleased

### Fixed

- The daemon's scheduled task starts `pwsh` through `conhost.exe --headless`. With Windows Terminal as the default terminal (the Windows 11 default), `-WindowStyle Hidden` did not hide anything: each logon left a Terminal window titled `pwsh.exe` on screen, and closing it killed the daemon (task result `0xC000013A`), after which the hooks went nowhere until the next logon. Since stopping the task now ends only that conhost, the daemon leaves on its own when its launcher is gone, and the installer waits for it (stopping an older instance by its command line) before registering again. Re-run `Install-MonitorDaemon.ps1` to pick this up.

## 1.3.0 — 2026-09-26

Second board: the **M5Stack Tab5** (ESP32-P4), with a tile per Claude Code session. On the host, the daemon now sees permission approvals as they happen.

### Added

- **M5Stack Tab5** (ESP32-P4, 5" 1280×720 touch panel, BMI270, speaker), `firmware/tab5`, with prebuilt images in the release (bootloader at `0x2000`). One tile per live Claude Code session, up to six, the screen split by their number (1, 2, 2×2, 3×2); each tile carries the session's name above the icon (the project's folder, `#1`/`#2` when two sessions share a project, or a nickname from the daemon's `projects.json`) and a clock below it with how long the current icon has been up. Tap a tile for its detail page: icon, name and clock next to the **hook inspector**, every field of the session's last hook event with its age plus a history of the previous ones; tap again to go back. A two-tone beep from the speaker when a red icon appears (rising for `!`, falling for `X`). The whole display turns with gravity in 90° steps like a tablet, calibrated per unit with `calibrate rot=<upright now> sign=<±1>` (stored in NVS); a tap wakes a screen that switched itself off. With no session lines (a manual test, the no-daemon hooks) a single tile follows the same words as the cube. Verified on a unit with ESP32-P4 rev v1.3 and an ST7121 panel: the USB-C is the P4's USB Serial/JTAG, so it enumerates like the AtomS3R and flashes without a button; the panel needs PSRAM at 200 MHz, which ESP-IDF 5.5 only offers with `CONFIG_IDF_EXPERIMENTAL_FEATURES` (set in `sdkconfig.defaults`).
- **Approval detection**: the daemon learns each session's `claude.exe` from the hook's TCP connection and watches its child processes in snapshots of the process table (`NtQuerySystemInformation` through a small C# helper, about 20 ms), twice a second while a permission is pending or a tool was just called, every two seconds otherwise. A tool process starting under a session showing the red sign means the permission was approved, so the gear returns within about half a second instead of when the tool ends (measured before: median 73 s of red per approval, 90th percentile 260 s). A session whose process is gone is dropped at once. `GET /status` shows `process_watch` and `process_pids`. Windows only; elsewhere the daemon keeps to the hooks.
- **EXT badge**: a blue disc reading "EXT" at the bottom right of the icon while a process started by a tool is alive (a build, a test run, a shell command), driven by the same snapshots through `tool_start` / `tool_stop`.
- Protocol words, `kProtocolVersion` now 6: `version` (2) answers with one `VERSION` line identifying hardware and firmware (board the image was built for, model M5Unified detected, chip and revision, cores, flash size, firmware, ESP-IDF and M5Unified versions, protocol, `features=`, build time, ELF SHA prefix, uptime, reset reason), also sent once at boot; `event <name>\tkey=value…` (3) carries a whole hook event, sent only to a device whose `features=` lists `events`; `tool_start` / `tool_stop` (4); `session <id>\tlabel= state= subagents= tool= project=`, `session_end <id>`, `session_clear` (5), sent only to a device that lists `sessions`; `screenshot` and `view grid` / `view detail <id>` (6): the Tab5 dumps its display as text lines and opens a page on request, what a tap does. The AtomS3R lists no features, answers `ERROR` to `screenshot` and ignores `view`.
- Hooks: `PreToolUse` for every tool (→ `processing`; `AskUserQuestion` still → `question`), `PostToolUseFailure` (→ `processing`; a failed tool used to leave the red sign until the next event) and `PostToolBatch` (→ `processing`). Fourteen identical HTTP hooks, each sending the project root in an `X-Claude-Project` header (`${CLAUDE_PROJECT_DIR}` with `allowedEnvVars`).
- Project per session: the daemon names each session after its root folder (or `projects.json`, `{ "root": "name" }`), adds `project=` and `project_root=` to every event and shows it in `GET /status`. `cwd` is only a fallback now, since it follows the shell's `cd`.
- Daemon: forwards every hook event with all its fields (flattened one level, values cut at 160 characters), keeps the last 50 for `GET /events`, and `-LogEvents` appends each raw payload to `hooks.jsonl`; reads the device's `VERSION` whenever it opens the port (`device_info` in `GET /status`, `GET /version` asks again) and retries every 10 s until a booting device answers; the port is opened as UTF-8.
- `tools/Get-Screenshot.ps1` turns a Tab5 `screenshot` into a PNG (setup lines, a wait, a crop, a scale), and `tools/Make-ReadmeImages.ps1` regenerates every picture of the README from the board.
- README: a picture of every icon and overlay, the six-tile dashboard and the detail page (`docs/images/`), a description of what the project does, and the installation as numbered steps for both boards.
- `monitor-core`: `Orientation` (90° steps with hysteresis and hold time, host-tested), `calibration.h` (the NVS record, shared by both boards), `EventLog` and `EventView` (the hook inspector), `screenshot.h`.

### Changed

- Release assets are named per board: `claude-monitor-<board>-vX.Y.Z{,-merged,-bootloader,-partition-table}.bin` (up to 1.2.0 the AtomS3R's bootloader and partition table were plain `bootloader.bin` / `partition-table.bin`). CI builds both boards on every push.
- The installer stops a running daemon before re-registering the task; the old arguments used to stay in force.
- `LineParser` drops an over-long line whole instead of letting its tail through as a stray word.

### Fixed

- Timers follow real time: the boards count the 33 ms frames that really went by and hand that to `Ui::tick`, so the work ring, the link timeout, the auto-off and the Tab5's clocks no longer run slow when drawing a frame takes longer than a frame (the cube's ran a little slow; the Tab5's big gear made its clocks run at half speed).

## 1.2.0 — 2026-09-10

### Added

- Overlays on a band around the icon: an elapsed-work **ring** while Claude works (one lap per ten minutes: yellow, amber, red), one **dot per live session** when two or more Claude Code sessions are open (coloured by each session's state), and a **hollow grey mark** when the host's heartbeat stops.
- Heartbeat: the daemon sends `ping` every 30 s; once the device has seen one, a minute of silence dims the screen and shows the mark. Hosts that never ping (the no-daemon variant) are unaffected.
- Screen policy: off after thirty minutes idle or thirty minutes without a host, back on at the next state change; the button wakes it as well as toggling it.
- Runtime orientation calibration: `calibrate rot=<0-3> sign=<1|-1> offset=<deg>` / `calibrate reset` over the protocol, `POST /calibrate?…` on the daemon, stored in NVS on the device. No rebuild per unit any more.
- `STATUS` reports `fw=` (app version), `board=`, the calibration in effect (`sign=`, `offset=`), the session codes, `link=`, `work=` (seconds of processing this turn) and `screen=`.
- Daemon: `sessions <codes>` to the device, per-session project folder in `/status`, and dead-session detection: a `processing`/`compacting` session whose transcript has not changed for `-DeadSessionMinutes` (15) is dropped instead of lingering for four hours.
- Host tests for `protocol.cpp` and `tilt.cpp` (`tests/host/`, plain C++17, `run.sh` / `run.ps1`).
- CI (GitHub Actions): host tests and firmware build on every push; on a tag, the four images are attached to the release and the **web flasher** (ESP Web Tools on GitHub Pages, https://icefoxj.github.io/claude-monitor/) is published. The flasher writes the three parts at their offsets, so a stored calibration survives an update; the merged image in the release erases the NVS partition (it pads the gap over it), which the README now says.

### Changed

- `Icons` no longer pushes the canvas from each icon; the state machine composes icon, overlays and then `push()`. Arcs use a segment every 6°, so a full ring is round.
- `Ui::apply` returns whether the state changed; the work timer and the subagent count reset together when a turn ends.

## 1.1.0 — 2026-09-10

### Added

- Three states and their icons: `error` (red circle with a cross, pulses on entry), `paused` (amber hourglass, animated: the sand runs for 12 s, the glass turns over, the sand settles) and `compacting` (grey spinning gear).
- Subagent counter: `subagent_start` / `subagent_stop` over the protocol; while it is above zero the processing gear is drawn smaller with a counter-rotating satellite gear. `STATUS` reports `subagents=`. The counter resets whenever the turn ends.
- Host daemon `host/Monitor-Daemon.ps1` (PowerShell 7): keeps the serial port open, receives Claude Code hook events as HTTP POSTs on `http://localhost:47831/hook`, keeps one state per session and shows the most urgent one, counts subagents across sessions, re-sends everything when the device reappears, frees the port on request (`POST /release`) for flashing, and logs every event with a millisecond timestamp. `host/Install-MonitorDaemon.ps1` registers it as a hidden logon task on Windows.
- Hook coverage: `StopFailure` (`rate_limit` → hourglass, everything else → error), `PreCompact` / `PostCompact`, `SubagentStart` / `SubagentStop`; `Notification` types `elicitation_dialog`, `elicitation_url_dialog` and `agent_needs_input` show the blue `?`, `quota_auto_resume_stale` the red `!`, `quota_auto_resume_fired` the gear and `quota_auto_resume_disabled` the red cross.
- `sdkconfig.defaults` (target `esp32s3`, 8 MB flash) so a fresh clone builds without menuconfig.

### Changed

- Repository split into `components/monitor-core` (protocol, icons, tilt filter, state machine, all board-independent; icons scale with the canvas size) and `firmware/atoms3r` (board wiring only), ready for a second board.
- The documented hook configuration is now twelve identical `"type": "http"` hooks pointing at the daemon; the previous one-process-per-event variant is kept as the no-daemon option.
- `Send-ClaudeState.ps1` hands the state to the daemon when it is running and only writes to the port itself when it is not; it accepts the new states and the two subagent words.

## 1.0.0 — 2026-09-10

First release.

- States `processing` (spinning yellow gear), `waiting_user` (red sign with an exclamation mark), `question` (blue sign with a question mark, for `AskUserQuestion`), `idle` (green check) and `off`, plus the `status` query.
- The two "look at the terminal" signs pulse three times when they appear.
- Continuous auto-rotation from the BMI270 accelerometer: the static icons turn smoothly with the cube at any angle, and hold still when it lies flat.
- The button under the screen toggles the display.
- Hooks for `SessionStart`, `UserPromptSubmit`, `PreToolUse` (`AskUserQuestion`), `PostToolUse`, `Notification`, `Stop`, `StopFailure` and `SessionEnd`, each running `host/Send-ClaudeState.ps1` (Windows) or an `echo` to the port (Linux/macOS).
- ESP-IDF 5.5.0, M5Unified 0.2.21, M5GFX 0.2.28. CC BY 4.0.
