# Changelog

Versions are annotated git tags (`vX.Y.Z`) on `main`; each one has a [GitHub release](https://github.com/icefoxj/claude-monitor/releases) with prebuilt AtomS3R images. Dates are release dates.

## Unreleased

### Added

- `version` command: the device answers with one `VERSION` line identifying the hardware and the firmware (board the image was built for, model M5Unified detected, chip and revision, cores, flash size, firmware version, ESP-IDF and M5Unified versions, protocol version, project, build time, ELF SHA prefix, uptime, last reset reason). The device also sends the line once at boot, as soon as its USB driver is up. `kProtocolVersion` (2) in `protocol.h`.
- Daemon: reads the `VERSION` line whenever it opens the port and keeps it in `GET /status` (`device_info`); `GET /version` asks the device again and returns the fields parsed.
- **Tab5 build** (`firmware/tab5`, ESP32-P4, work in progress): the icon and, next to it, a hook inspector that lists every field of the last hook event with its age, plus a history of the previous ones. The whole display turns with gravity in 90° steps (landscape: icon left, inspector right; portrait: icon on top), calibrated per unit with `calibrate rot=<upright now> sign=<±1>` in NVS. Touch toggles the screen. Verified on hardware: the USB-C is the P4's USB Serial/JTAG, the ST7121 panel comes up with the PSRAM-at-200-MHz configuration, and the daemon streams events to it.
- `monitor-core`: `Orientation` (sector quantisation with hysteresis and hold time, host-tested) and `calibration.h` (the NVS record, moved out of the AtomS3R's `main.cpp` and shared).
- Approval detection: the daemon learns each session's `claude.exe` from the hook's TCP connection and watches WMI process start/end events; a tool process starting under a session showing the red sign means the permission was approved, so the gear returns within about half a second instead of when the tool ends (measured before: median 73 s of red per approval, 90th percentile 260 s). The same events drive a new **tool running** flag (`tool_start` / `tool_stop`, protocol 4): a blue dot on the right of the band while a tool process is alive; and a session whose process is gone is dropped at once.
- Hooks: `PreToolUse` for every tool (→ `processing`; `AskUserQuestion` still → `question`), `PostToolUseFailure` (→ `processing`; a failed tool used to leave the red sign until the next event) and `PostToolBatch` (→ `processing`). Fourteen identical HTTP hooks now.
- Project per session: the hooks send `${CLAUDE_PROJECT_DIR}` in an `X-Claude-Project` header (`headers` + `allowedEnvVars` in `settings.json`), the daemon keeps the root per session, names it by its folder or by `projects.json`, adds `project=` and `project_root=` to every `event` line and shows it in `GET /status`; the Tab5 prints the project next to each event. `cwd` is only a fallback now, since it follows the shell's `cd`.
- Daemon: retries the `version` query every 10 s until the device answers (a Tab5 still booting when the port opens missed the first one); the installer stops a running instance before re-registering the task, otherwise the old arguments stayed in force.
- Protocol 3: `event <name>\tkey=value…` carries a whole hook event; `VERSION` gained `features=` (`events,touch` on the Tab5, none on the AtomS3R) and the daemon only streams events to a device that lists `events`. `LineParser` now drops an over-long line whole instead of leaving a tail.
- Daemon: forwards every hook event with all its fields (flattened one level, values cut at 160 chars), keeps the last 50 for `GET /events`, and `-LogEvents` appends each raw payload to `hooks.jsonl`. The serial port is opened with UTF-8 encoding.
- `monitor-core`: `EventLog` (last event + history) and `EventView` (draws the inspector into any canvas) for boards with room next to the icon.
- CI builds both boards (matrix) and `tools/collect-images.sh` names the images per board; from the next release the bootloader and partition table are `claude-monitor-<board>-vX.Y.Z-bootloader.bin` / `…-partition-table.bin`.

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
