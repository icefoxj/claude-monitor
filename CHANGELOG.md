# Changelog

Versions are annotated git tags (`vX.Y.Z`) on `main`; each one has a [GitHub release](https://github.com/icefoxj/claude-monitor/releases) with prebuilt AtomS3R images. Dates are release dates.

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
