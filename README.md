# claude-monitor

A physical traffic light for [Claude Code](https://claude.com/claude-code), built on the **M5Stack AtomS3R**. Claude Code's hooks report every lifecycle event to a small daemon on your machine; the daemon keeps the USB serial port open and tells the 128×128 display what Claude is doing, so you can stop tabbing back every thirty seconds.

| Icon | Meaning | Claude Code hook event |
|---|---|---|
| Spinning yellow gear | Processing your request | `UserPromptSubmit`, `PostToolUse` |
| Yellow gear with a small satellite gear | Processing, with subagents running | `SubagentStart` / `SubagentStop` |
| Spinning grey gear | Compacting context; back in a minute | `PreCompact` / `PostCompact` |
| Red sign with an exclamation mark | Waiting for a permission (or for Enter after a usage-limit reset) | `Notification` (`permission_prompt`, `quota_auto_resume_stale`) |
| Blue sign with a question mark | Claude, an MCP server or a background agent asked you something | `PreToolUse` (`AskUserQuestion`), `Notification` (`elicitation_*`, `agent_needs_input`) |
| Red circle with a cross | The turn ended with an API error; look at the terminal | `StopFailure` (all types but `rate_limit`), `Notification` (`quota_auto_resume_disabled`) |
| Amber hourglass, sand running, flips over every 13 s | Paused on a usage limit, waiting for it to reset | `StopFailure` (`rate_limit`) → `Notification` (`quota_auto_resume_fired`) |
| Green circle with a check | Done, idle | `Stop`, `SessionStart` |
| Screen off | Session ended | `SessionEnd` |

The three "look at the terminal" signs (red `!`, blue `?`, red `X`) pulse three times when they appear, so the change catches the eye from across the desk. The built-in IMU keeps the icons upright at any tilt, turning them smoothly as you turn the cube. The button under the screen toggles the display. With several Claude Code sessions open, the cube shows the most urgent state among them.

Native **ESP-IDF 5.5** (C++ / CMake / FreeRTOS) with the official VS Code extension. No Arduino, no PlatformIO, no Wi-Fi, no soldering.

> The full write-up — design decisions, geometry of the icons, every gotcha — is in [claude-code-status-monitor-atoms3r.md](claude-code-status-monitor-atoms3r.md). The firmware has evolved since it was written (nine states instead of four, exclamation and question marks instead of "STOP", continuous rotation, an entry pulse, a `status` command, a host daemon with per-session tracking, and a component/board split); this README describes the current code, and [CHANGELOG.md](CHANGELOG.md) lists what each release added.

In a hurry? Every [release](https://github.com/icefoxj/claude-monitor/releases) ships prebuilt binaries: [flash one](#flashing-a-prebuilt-binary) with `esptool`, then set up [the host daemon](#the-host-daemon) and [the hooks](#claude-code-hooks). Building from source needs the ESP-IDF toolchain and is described in [Build and flash](#build-and-flash).

## How it works

The Anthropic API does not expose session state, so the device cannot poll. Instead, Claude Code's **hooks** fire on each lifecycle event and POST the event to a daemon on `localhost`, which maps it to a state and writes one word to the serial port:

```
┌──────────────┐   hook (HTTP POST)   ┌──────────────┐   text line      ┌──────────────┐
│  Claude Code │ ───────────────────► │    daemon    │ ───over USB────► │   AtomS3R    │
│  session A   │  UserPromptSubmit    │  one state   │  "processing\n"  │  draws the   │
│  session B   │  Notification ...    │  per session │                  │  icon        │
└──────────────┘                      └──────────────┘                  └──────────────┘
```

Protocol: one command per line, `\n`-terminated, no JSON, no handshake. State commands: `processing`, `waiting_user`, `question`, `error`, `paused`, `compacting`, `idle`, `off`. Two counters: `subagent_start` / `subagent_stop` (while the count is above zero the gear grows a satellite; the count resets when the turn ends). And `status`, which makes the device answer with one line (`STATUS state=… subagents=… rot=… angle=… ax=… ay=… az=…`) and exists only for calibration and debugging.

The daemon is optional: the hooks can also run a one-line script per event that writes to the port directly (see [Without the daemon](#without-the-daemon)). The daemon is better in every way that matters: no process start-up per event, a single writer on the port, events delivered in order, a log with timestamps, and per-session tracking.

## Hardware

- **M5Stack AtomS3R** — ESP32-S3-PICO-1-N8R8, 8 MB flash, 0.85" 128×128 IPS display, BMI270 IMU, USB-C. That's it.
- The ESP32-S3 speaks USB natively (USB Serial/JTAG), so the PC sees a virtual serial port and opening it does not reset the board.

## Requirements

- VS Code with the **ESP-IDF extension** (`espressif.esp-idf-extension`) and ESP-IDF **5.x** installed through it.
- Install path and project path **without spaces or accents**.
- Linux: add your user to the serial group (`sudo usermod -aG dialout $USER`, then log out/in).
- For the daemon: PowerShell 7 (`pwsh`). It ships with the repository as a script; on Linux/macOS `pwsh` runs the same script with `-PortName /dev/ttyACM0` (untested there). The installer (`Install-MonitorDaemon.ps1`) is Windows-only, because it registers a scheduled task; elsewhere start the daemon from a user service of your choosing.
- To flash a prebuilt release instead of building: any Python 3 with `esptool` (`pip install esptool`). No ESP-IDF needed.

[M5Unified](https://components.espressif.com/components/m5stack/m5unified) (and its dependency M5GFX) come from the ESP Component Registry, declared in `components/monitor-core/idf_component.yml`. The first build downloads them into `firmware/atoms3r/managed_components/`. Tested with ESP-IDF 5.5.0, M5Unified 0.2.21 and M5GFX 0.2.28 (see `dependencies.lock`).

## Flashing a prebuilt binary

Each [release](https://github.com/icefoxj/claude-monitor/releases) carries four files for the AtomS3R: `claude-monitor-atoms3r-vX.Y.Z-merged.bin`, a single image with bootloader, partition table and app that goes at offset `0x0`, and the three parts separately (`bootloader.bin` at `0x0`, `partition-table.bin` at `0x8000`, `claude-monitor-atoms3r-vX.Y.Z.bin` at `0x10000`) for anyone who prefers `idf.py`-style flashing. With `esptool` installed:

```
python -m esptool --chip esp32s3 -p COM5 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 claude-monitor-atoms3r-vX.Y.Z-merged.bin
```

The port is `/dev/ttyACM0` on Linux and `/dev/cu.usbmodemXXXX` on macOS. On a board that has never been flashed, hold the side reset button ~2 s to enter download mode first. If the daemon is already running it owns the port: free it with `POST http://localhost:47831/release` (see [Build and flash](#build-and-flash)) and flash within two minutes. The boot log prints the version the image was built from (`App version: v1.1.0`), which is how you check what is on the cube.

## Build and flash

The ESP-IDF project for the AtomS3R lives in `firmware/atoms3r/`. Open that folder in VS Code, or open `claude-monitor.code-workspace`, which lists it as a workspace folder, so the extension sees a project. Then, from the command palette or the status bar buttons:

1. **ESP-IDF: Set Espressif Device Target** → `esp32s3`
2. Nothing to configure: `sdkconfig.defaults` already sets the target and the **8 MB** flash size, and ESP-IDF applies it when it generates `sdkconfig`. If you do open **ESP-IDF: SDK Configuration Editor**, leave *Channel for console output* at its default: on the S3 that is UART0 with USB Serial/JTAG as a secondary channel, which is what makes the boot log visible over USB.
3. **ESP-IDF: Build your Project**
4. Connect the AtomS3R. For the **first flash**, hold the side reset button ~2 s to enter download mode. **ESP-IDF: Select Port to Use** → pick the new port.
5. **ESP-IDF: Flash your Project**
6. **ESP-IDF: Monitor Device** — you should see the boot log and a green check on the screen. **Close the monitor** before the next step; it holds the port open.

Equivalent CLI, in a terminal with the ESP-IDF environment active (**ESP-IDF: Open ESP-IDF Terminal**, or the "ESP-IDF PowerShell" Start Menu shortcut on Windows):

```
cd firmware/atoms3r
idf.py set-target esp32s3
idf.py build              # sdkconfig.defaults is applied automatically
idf.py -p COM5 flash      # /dev/ttyACM0 on Linux, /dev/cu.usbmodemXXXX on macOS
idf.py -p COM5 monitor    # Ctrl+] to exit
```

On Windows, never prefix `idf.py` with `bash` — that hands the line to WSL. If a flash fails with "could not open port", the board is probably re-enumerating after a reset; retry a few seconds later. **If the daemon is running it owns the port**: ask it to let go before flashing or opening the monitor, and it reconnects on its own two minutes later (or at once with `/reconnect`):

```powershell
Invoke-RestMethod -Method Post http://localhost:47831/release
```

## Manual test

Prove the firmware before touching Claude Code. Monitor closed, device connected.

**Linux / macOS**

```bash
stty -F /dev/ttyACM0 raw -echo        # macOS: stty -f /dev/cu.usbmodemXXXX raw -echo
echo processing   > /dev/ttyACM0      # spinning gear
echo waiting_user > /dev/ttyACM0      # red exclamation sign
echo question     > /dev/ttyACM0      # blue question sign
echo error        > /dev/ttyACM0      # red cross
echo paused       > /dev/ttyACM0      # amber hourglass
echo compacting   > /dev/ttyACM0      # grey gear
echo idle         > /dev/ttyACM0      # green check
echo off          > /dev/ttyACM0      # screen off
```

**Windows** — use the bundled script (find your port with `[System.IO.Ports.SerialPort]::GetPortNames()`). If the daemon is running the script hands the state to it; otherwise it writes to the port itself:

```powershell
.\host\Send-ClaudeState.ps1 -State processing -PortName COM5
.\host\Send-ClaudeState.ps1 -State waiting_user -PortName COM5
.\host\Send-ClaudeState.ps1 -State question -PortName COM5
.\host\Send-ClaudeState.ps1 -State error -PortName COM5
.\host\Send-ClaudeState.ps1 -State paused -PortName COM5
.\host\Send-ClaudeState.ps1 -State compacting -PortName COM5
.\host\Send-ClaudeState.ps1 -State idle -PortName COM5
.\host\Send-ClaudeState.ps1 -State off -PortName COM5
```

The script uses .NET's `SerialPort`, pins DTR/RTS low so the board never resets, retries a busy port a few times, and always exits 0. If nothing happens, something else (usually the monitor) has the port open.

## The host daemon

`host/Monitor-Daemon.ps1` is a PowerShell 7 script that keeps the port open and listens on `http://localhost:47831/`. Install it as a scheduled task that starts hidden at logon (and starts it right away):

```powershell
.\host\Install-MonitorDaemon.ps1 -PortName COM5      # -Uninstall to remove
```

What it does with each event: keeps one state per Claude Code session (`session_id` comes with every hook), sends the device the most urgent state among the live sessions (`error` > `!` = `?` > hourglass > compacting > processing > idle), counts subagents across sessions, drops a session on `SessionEnd` or after four hours of silence, and re-sends everything when the device reappears after a reboot or re-plug. The log at `%LOCALAPPDATA%\claude-monitor\daemon.log` has one line per event with a millisecond timestamp, which is how you find out where time goes when an icon seems late.

Endpoints, all on `http://localhost:47831/`:

| Route | Use |
|---|---|
| `POST /hook` | what the hooks call; body is Claude Code's hook JSON |
| `GET /status` | daemon state, sessions, what the device was last told |
| `GET /serial/status` | asks the device for its `STATUS` line and returns it |
| `POST /state/<state>` | writes one state to the device (what `Send-ClaudeState.ps1` uses) |
| `POST /release[?seconds=120]` | closes the port so `idf.py` can flash; reopens after the delay |
| `POST /reconnect` | reopens the port now |
| `GET /health` | `{"ok":true}` |

## Claude Code hooks

Hooks live in `~/.claude/settings.json` (all sessions) or `.claude/settings.json` in a project. With the daemon, every event is the same one-line HTTP hook and the mapping lives in the daemon:

```json
{
  "hooks": {
    "SessionStart":     [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "UserPromptSubmit": [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "PreToolUse":       [{ "matcher": "AskUserQuestion", "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "PostToolUse":      [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "Notification":     [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "Stop":             [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "StopFailure":      [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "PreCompact":       [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "PostCompact":      [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "SubagentStart":    [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "SubagentStop":     [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }],
    "SessionEnd":       [{ "hooks": [{ "type": "http", "url": "http://localhost:47831/hook", "timeout": 2 }] }]
  }
}
```

The mapping the daemon applies:

| Event | Detail | State | Why |
|---|---|---|---|
| `SessionStart` | | `idle` | turns the screen back on if the last session switched it off |
| `UserPromptSubmit` | | `processing` | |
| `PreToolUse` | `tool_name` = `AskUserQuestion` | `question` | Claude is asking you something |
| `PostToolUse` | | `processing` | back to the gear once you answered a permission or a question |
| `Notification` | `permission_prompt`, `quota_auto_resume_stale` | `waiting_user` | permission prompt, or a usage-limit reset that needs Enter |
| `Notification` | `elicitation_dialog`, `elicitation_url_dialog`, `agent_needs_input` | `question` | an MCP server or a background agent needs your input |
| `Notification` | `quota_auto_resume_fired` | `processing` | the usage limit reset and the task resumed |
| `Notification` | `quota_auto_resume_disabled` | `error` | Claude Code gave up waiting for the limit |
| `Notification` | anything else (`idle_prompt`, `auth_success`, …) | ignored | `idle_prompt` fires a minute after `Stop`, when the green check already says "waiting for you" |
| `Stop` | | `idle` | |
| `StopFailure` | `error_type` = `rate_limit` | `paused` | usage limit hit; Claude Code will wait for the reset |
| `StopFailure` | every other error type | `error` | authentication, billing, server errors, … |
| `PreCompact` | | `compacting` | |
| `PostCompact` | `trigger` = `auto` / `manual` | `processing` / `idle` | auto-compaction happens mid-turn; `/compact` between turns |
| `SubagentStart` / `SubagentStop` | | count ± 1 | |
| `SessionEnd` | | session removed; `off` when it was the last one | |

Open `/hooks` inside a running session (it reloads the configuration) or start a new one, then send a prompt: gear while it thinks, green check when it finishes, exclamation sign when it asks for permission, question sign when it asks you something. Two caveats. First, there is no hook event for the moment you *approve* a permission: the red sign stays until the approved tool finishes (`PostToolUse`), so a long build approved by hand means a long red. Second, Claude Code rewrites `settings.json` itself, for example when it records a newly allowed directory or permission, and a block added by hand while a session was running can be lost in that rewrite; if a hook you added has vanished, re-add it and it sticks.

Hooks run wherever Claude Code runs: the terminal, the VS Code and JetBrains extensions, and Remote Control sessions driven from claude.ai or the phone all fire the hooks on your machine. Cloud sessions on claude.ai/code run hooks in the cloud sandbox from the repository's `.claude/settings.json`, where neither the daemon nor the USB port exists. Plain claude.ai chat has no hooks.

Hook event names change over time — check the [hooks documentation](https://code.claude.com/docs/en/hooks) for your version.

### Without the daemon

Each event can instead run `Send-ClaudeState.ps1` (Windows) or an `echo` (Linux/macOS), with the mapping expressed as matchers. The exec form (`args`) spawns `pwsh.exe` directly, with no shell in between, and `async` keeps the PowerShell start-up off the critical path; every entry has the same shape, so only the first is written out in full:

```json
{
  "hooks": {
    "SessionStart": [{ "hooks": [{
      "type": "command", "command": "pwsh.exe",
      "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "E:/work/claude-monitor/host/Send-ClaudeState.ps1", "-State", "idle"],
      "async": true, "timeout": 10
    }] }],
    "UserPromptSubmit": [{ "hooks": [{ "…same, -State": "processing" }] }],
    "PreToolUse":       [{ "matcher": "AskUserQuestion", "hooks": [{ "…": "question" }] }],
    "PostToolUse":      [{ "hooks": [{ "…": "processing" }] }],
    "Notification": [
      { "matcher": "permission_prompt|quota_auto_resume_stale",                  "hooks": [{ "…": "waiting_user" }] },
      { "matcher": "elicitation_dialog|elicitation_url_dialog|agent_needs_input", "hooks": [{ "…": "question" }] },
      { "matcher": "quota_auto_resume_fired",                                     "hooks": [{ "…": "processing" }] },
      { "matcher": "quota_auto_resume_disabled",                                  "hooks": [{ "…": "error" }] }
    ],
    "Stop":             [{ "hooks": [{ "…": "idle" }] }],
    "StopFailure": [
      { "matcher": "rate_limit", "hooks": [{ "…": "paused" }] },
      { "matcher": "overloaded|authentication_failed|oauth_org_not_allowed|account_on_hold|billing_error|invalid_request|model_not_found|server_error|max_output_tokens|cloud_credential_error|unknown", "hooks": [{ "…": "error" }] }
    ],
    "PreCompact":       [{ "hooks": [{ "…": "compacting" }] }],
    "PostCompact": [
      { "matcher": "auto",   "hooks": [{ "…": "processing" }] },
      { "matcher": "manual", "hooks": [{ "…": "idle" }] }
    ],
    "SubagentStart":    [{ "hooks": [{ "…": "subagent_start" }] }],
    "SubagentStop":     [{ "hooks": [{ "…": "subagent_stop" }] }],
    "SessionEnd":       [{ "hooks": [{ "…same but without async, -State": "off" }] }]
  }
}
```

On Linux/macOS the command is `echo <state> > /dev/ttyACM0 2>/dev/null || true` (put the port in raw mode once with `stty` as in the manual test). The `|| true` / unconditional `exit 0` is the important part: in Claude Code, a hook exiting with code 2 **blocks** the action. A loose cable must never turn into a Claude Code that refuses to work. In this mode there is no per-session tracking: the last event from any session wins.

## Calibrating auto-rotation

The icons are redrawn at the angle of the gravity vector, low-pass filtered, so they turn smoothly with the cube. How the BMI270 is mounted relative to the panel varies, so there are three constants in `firmware/atoms3r/main/main.cpp`:

- **Lying flat** — `kBootRotationOffset` (0–3, steps of 90° clockwise) is added to the display's default rotation at boot. It is also the frame everything is drawn in.
- **Tilted or standing** — the icon angle is `angleSign * atan2(ay, ax) + angleOffset` (`TiltConfig`). If the icon turns the wrong way, flip the sign; if it is consistently off, adjust the offset (radians).

The committed values were verified on one AtomS3R in all four standing positions and under continuous tilt. To check yours, stand the cube on a side and read `angle=` from `http://localhost:47831/serial/status` (or send `status` over the port yourself) and compare it with what looks upright. The angle is held while the cube lies flat (`|az| > 0.80 g`) or the tilt is too small to be reliable (in-plane component below 0.40 g), so a cube resting on a desk never twitches.

## Project layout

The repository is laid out to host more than one board. Everything that does not depend on the hardware is an ESP-IDF component; each board is a small ESP-IDF project that wires that component to its panel, IMU, buttons and USB.

```
claude-monitor/
├── components/monitor-core/        # board-independent: protocol, icons, tilt filter, state machine
│   ├── include/monitor/*.h         # protocol.h, tilt.h, icons.h, ui.h
│   ├── src/*.cpp
│   └── idf_component.yml           # m5stack/m5unified ^0.2
├── firmware/atoms3r/               # ESP-IDF project for the AtomS3R (esp32s3)
│   ├── CMakeLists.txt              # pulls ../../components in via EXTRA_COMPONENT_DIRS
│   ├── sdkconfig.defaults          # target esp32s3, 8 MB flash (idf.py save-defconfig)
│   ├── dependencies.lock           # exact M5Unified / M5GFX versions the build was tested with
│   ├── main/main.cpp               # board wiring: panel, IMU, button, USB Serial/JTAG
│   └── .vscode/                    # c_cpp_properties.json and launch.json (settings.json is not committed)
├── host/
│   ├── Monitor-Daemon.ps1          # the daemon: port owner, HTTP hook receiver, per-session state
│   ├── Install-MonitorDaemon.ps1   # registers it as a logon scheduled task (Windows)
│   └── Send-ClaudeState.ps1        # one-shot sender (via the daemon if running, else the port)
├── claude-monitor.code-workspace   # VS Code multi-root: repo + firmware/atoms3r
├── claude-code-status-monitor-atoms3r.md   # the original article
├── CHANGELOG.md                    # what each release changed
├── CLAUDE.md                       # guidance for Claude Code working on this repo
├── LICENSE                         # CC BY 4.0
├── .clangd                         # lets clangd read the xtensa compile database
└── .devcontainer/                  # optional: espressif/idf Docker image
```

`firmware/atoms3r/.vscode/settings.json` is not committed: it holds machine-specific paths (ESP-IDF install, clangd, COM port) that the ESP-IDF extension writes when you pick the setup, target and port. Build output (`build/`, `managed_components/`, `sdkconfig`) is ignored too; `sdkconfig.defaults` is enough to recreate it.

Everything is drawn into an in-RAM canvas (`M5Canvas`, 32 KB on the AtomS3R) and pushed to the panel in one go, so the animations run without flicker. Icons are procedural — triangles, circles and round-capped strokes, no bitmaps — defined once for a 128 px canvas and scaled by the canvas size, and rotated vertex by vertex, which is why they can sit at any angle.

## Known limitations

- There is no hook event when a permission is approved, so the red sign lasts until the approved tool finishes.
- `Notification` semantics vary between Claude Code versions; if you get a green check where you expected the exclamation sign, that's why.
- ESP-IDF logs and the protocol share the same USB port. Harmless: the daemon reads and discards the log lines, and only reacts to `STATUS` replies.
- The daemon must be running for the HTTP hooks to reach anything; when it is not, Claude Code reports the failed hook and carries on. The scheduled task restarts it at logon and after crashes.
- A manual write through `Send-ClaudeState.ps1` or `POST /state/<state>` bypasses the per-session bookkeeping; the next hook event that changes the aggregate state overrides it.
- Only the AtomS3R is supported today. The code is split so that a second board (the M5Stack Tab5 is the candidate) is another `firmware/<board>/` project, but none exists yet.

## License

Firmware, scripts and the article are released under the [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/) license (CC BY 4.0). See [LICENSE](LICENSE).

## References

- [ESP-IDF VS Code extension](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/)
- [AtomS3R docs](https://docs.m5stack.com/en/core/AtomS3R) · [store page](https://shop.m5stack.com/products/atoms3r-dev-kit)
- [M5Unified](https://github.com/m5stack/M5Unified) on the [Component Registry](https://components.espressif.com/components/m5stack/m5unified)
- [Claude Code hooks](https://code.claude.com/docs/en/hooks)
