# claude-monitor

A physical traffic light for [Claude Code](https://claude.com/claude-code), built on the **M5Stack AtomS3R**. Claude Code's hooks push a one-word line over USB; the 128×128 display shows what Claude is doing so you can stop tabbing back every thirty seconds.

| Icon | Meaning | Claude Code hook event |
|---|---|---|
| Spinning yellow gear | Processing your request | `UserPromptSubmit`, `PostToolUse` |
| Red sign with an exclamation mark | Waiting for a permission | `Notification` (`permission_prompt`) |
| Blue sign with a question mark | Claude asked you a question | `PreToolUse` (`AskUserQuestion`) |
| Green circle with a check | Done, idle | `Stop`, `StopFailure`, `SessionStart` |
| Screen off | Session ended | `SessionEnd` |

The two "needs you" signs pulse three times when they appear, so the change catches the eye from across the desk. The built-in IMU keeps the static icons upright at any tilt, turning them smoothly as you turn the cube. The button under the screen toggles the display.

Native **ESP-IDF 5.5** (C++ / CMake / FreeRTOS) with the official VS Code extension. No Arduino, no PlatformIO, no Wi-Fi, no soldering.

> The full write-up — design decisions, geometry of the icons, every gotcha — is in [claude-code-status-monitor-atoms3r.md](claude-code-status-monitor-atoms3r.md). The firmware has evolved since it was written (exclamation and question marks instead of "STOP", continuous rotation, an entry pulse, a `status` command, a richer hook set); this README describes the current code.

## How it works

The Anthropic API does not expose session state, so the device cannot poll. Instead, Claude Code's **hooks** run a shell command on each lifecycle event, and that command writes a line to the serial port:

```
┌──────────────┐   lifecycle event   ┌──────────────┐   text line      ┌──────────────┐
│  Claude Code │ ──────────────────► │ hook (shell) │ ───over USB────► │   AtomS3R    │
│              │  UserPromptSubmit   │ echo > serial│  "processing\n"  │  draws the   │
│              │  Notification       │     port     │                  │  icon for    │
│              │  Stop               │              │                  │  the state   │
└──────────────┘                     └──────────────┘                  └──────────────┘
```

Protocol: one command per line, `\n`-terminated, no JSON, no handshake. Commands: `processing`, `waiting_user`, `question`, `idle`, `off`. A sixth command, `status`, makes the device answer with one line (`STATUS state=… rot=… angle=… ax=… ay=… az=…`) and exists only for calibration and debugging.

## Hardware

- **M5Stack AtomS3R** — ESP32-S3-PICO-1-N8R8, 8 MB flash, 0.85" 128×128 IPS display, BMI270 IMU, USB-C. That's it.
- The ESP32-S3 speaks USB natively (USB Serial/JTAG), so the PC sees a virtual serial port and opening it does not reset the board.

## Requirements

- VS Code with the **ESP-IDF extension** (`espressif.esp-idf-extension`) and ESP-IDF **5.x** installed through it.
- Install path and project path **without spaces or accents**.
- Linux: add your user to the serial group (`sudo usermod -aG dialout $USER`, then log out/in).

[M5Unified](https://components.espressif.com/components/m5stack/m5unified) (and its dependency M5GFX) come from the ESP Component Registry, declared in `main/idf_component.yml`. The first build downloads them into `managed_components/`. Tested with ESP-IDF 5.5.0, M5Unified 0.2.21 and M5GFX 0.2.28 (see `dependencies.lock`).

## Build and flash

Using the extension (command palette or status bar buttons):

1. **ESP-IDF: Set Espressif Device Target** → `esp32s3`
2. Nothing to configure: `sdkconfig.defaults` already sets the target and the **8 MB** flash size, and ESP-IDF applies it when it generates `sdkconfig`. If you do open **ESP-IDF: SDK Configuration Editor**, leave *Channel for console output* at its default: on the S3 that is UART0 with USB Serial/JTAG as a secondary channel, which is what makes the boot log visible over USB.
3. **ESP-IDF: Build your Project**
4. Connect the AtomS3R. For the **first flash**, hold the side reset button ~2 s to enter download mode. **ESP-IDF: Select Port to Use** → pick the new port.
5. **ESP-IDF: Flash your Project**
6. **ESP-IDF: Monitor Device** — you should see the boot log and a green check on the screen. **Close the monitor** before the next step; it holds the port open.

Equivalent CLI, in a terminal with the ESP-IDF environment active (**ESP-IDF: Open ESP-IDF Terminal**, or the "ESP-IDF PowerShell" Start Menu shortcut on Windows):

```
idf.py set-target esp32s3
idf.py build              # sdkconfig.defaults is applied automatically
idf.py -p COM5 flash      # /dev/ttyACM0 on Linux, /dev/cu.usbmodemXXXX on macOS
idf.py -p COM5 monitor    # Ctrl+] to exit
```

On Windows, never prefix `idf.py` with `bash` — that hands the line to WSL. If a flash fails with "could not open port", the board is probably re-enumerating after a reset; retry a few seconds later.

## Manual test

Prove the firmware before touching Claude Code. Monitor closed, device connected.

**Linux / macOS**

```bash
stty -F /dev/ttyACM0 raw -echo        # macOS: stty -f /dev/cu.usbmodemXXXX raw -echo
echo processing   > /dev/ttyACM0      # spinning gear
echo waiting_user > /dev/ttyACM0      # red exclamation sign
echo question     > /dev/ttyACM0      # blue question sign
echo idle         > /dev/ttyACM0      # green check
echo off          > /dev/ttyACM0      # screen off
```

**Windows** — use the bundled script (find your port with `[System.IO.Ports.SerialPort]::GetPortNames()`):

```powershell
.\Send-ClaudeState.ps1 -State processing -PortName COM5
.\Send-ClaudeState.ps1 -State waiting_user -PortName COM5
.\Send-ClaudeState.ps1 -State question -PortName COM5
.\Send-ClaudeState.ps1 -State idle -PortName COM5
.\Send-ClaudeState.ps1 -State off -PortName COM5
```

The script uses .NET's `SerialPort`, pins DTR/RTS low so the board never resets, retries a busy port a few times, and always exits 0. If nothing happens, something else (usually the monitor) has the port open.

## Claude Code hooks

Hooks live in `~/.claude/settings.json` (all sessions) or `.claude/settings.json` in a project. The mapping this project uses:

| Event | Matcher | State sent | Why |
|---|---|---|---|
| `SessionStart` | | `idle` | turns the screen back on if the last session switched it off |
| `UserPromptSubmit` | | `processing` | |
| `PreToolUse` | `AskUserQuestion` | `question` | Claude is asking you something |
| `PostToolUse` | | `processing` | back to the gear once you answered a permission or a question |
| `Notification` | `permission_prompt` | `waiting_user` | |
| `Stop` | | `idle` | |
| `StopFailure` | | `idle` | an API error would otherwise leave the gear spinning forever |
| `SessionEnd` | | `off` | |

`Notification` is matched on `permission_prompt` only. Its other common type, `idle_prompt`, fires a minute after Claude finishes, when the green check already says "waiting for you".

**Windows** — the exec form (`args`) spawns `pwsh.exe` directly, with no shell in between: nothing to escape, and it does not matter whether Claude Code finds Git Bash or WSL's `bash.exe`. `async` keeps the ~200 ms PowerShell start-up off the critical path. Point `-File` at your clone (forward slashes are fine) and add `"-PortName", "COM6"` to `args` if your port differs:

```json
{
  "hooks": {
    "SessionStart":     [{ "hooks": [{ "type": "command", "command": "pwsh.exe", "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "E:/work/claude-monitor/Send-ClaudeState.ps1", "-State", "idle"],         "async": true, "timeout": 10 }] }],
    "UserPromptSubmit": [{ "hooks": [{ "type": "command", "command": "pwsh.exe", "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "E:/work/claude-monitor/Send-ClaudeState.ps1", "-State", "processing"],   "async": true, "timeout": 10 }] }],
    "PreToolUse":       [{ "matcher": "AskUserQuestion",  "hooks": [{ "type": "command", "command": "pwsh.exe", "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "E:/work/claude-monitor/Send-ClaudeState.ps1", "-State", "question"],     "async": true, "timeout": 10 }] }],
    "PostToolUse":      [{ "hooks": [{ "type": "command", "command": "pwsh.exe", "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "E:/work/claude-monitor/Send-ClaudeState.ps1", "-State", "processing"],   "async": true, "timeout": 10 }] }],
    "Notification":     [{ "matcher": "permission_prompt", "hooks": [{ "type": "command", "command": "pwsh.exe", "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "E:/work/claude-monitor/Send-ClaudeState.ps1", "-State", "waiting_user"], "async": true, "timeout": 10 }] }],
    "Stop":             [{ "hooks": [{ "type": "command", "command": "pwsh.exe", "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "E:/work/claude-monitor/Send-ClaudeState.ps1", "-State", "idle"],         "async": true, "timeout": 10 }] }],
    "StopFailure":      [{ "hooks": [{ "type": "command", "command": "pwsh.exe", "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "E:/work/claude-monitor/Send-ClaudeState.ps1", "-State", "idle"],         "async": true, "timeout": 10 }] }],
    "SessionEnd":       [{ "hooks": [{ "type": "command", "command": "pwsh.exe", "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "E:/work/claude-monitor/Send-ClaudeState.ps1", "-State", "off"],          "timeout": 10 }] }]
  }
}
```

**Linux / macOS** — the same events with `echo` (adjust the port, and put it in raw mode once with `stty` as in the manual test):

```json
{
  "hooks": {
    "SessionStart":     [{ "hooks": [{ "type": "command", "command": "echo idle > /dev/ttyACM0 2>/dev/null || true", "async": true }] }],
    "UserPromptSubmit": [{ "hooks": [{ "type": "command", "command": "echo processing > /dev/ttyACM0 2>/dev/null || true", "async": true }] }],
    "PreToolUse":       [{ "matcher": "AskUserQuestion",  "hooks": [{ "type": "command", "command": "echo question > /dev/ttyACM0 2>/dev/null || true", "async": true }] }],
    "PostToolUse":      [{ "hooks": [{ "type": "command", "command": "echo processing > /dev/ttyACM0 2>/dev/null || true", "async": true }] }],
    "Notification":     [{ "matcher": "permission_prompt", "hooks": [{ "type": "command", "command": "echo waiting_user > /dev/ttyACM0 2>/dev/null || true", "async": true }] }],
    "Stop":             [{ "hooks": [{ "type": "command", "command": "echo idle > /dev/ttyACM0 2>/dev/null || true", "async": true }] }],
    "StopFailure":      [{ "hooks": [{ "type": "command", "command": "echo idle > /dev/ttyACM0 2>/dev/null || true", "async": true }] }],
    "SessionEnd":       [{ "hooks": [{ "type": "command", "command": "echo off > /dev/ttyACM0 2>/dev/null || true" }] }]
  }
}
```

The `|| true` / unconditional `exit 0` is the important part: in Claude Code, a hook exiting with code 2 **blocks** the action. A loose cable must never turn into a Claude Code that refuses to work.

Open `/hooks` inside a running session (it reloads the configuration) or start a new one, then send a prompt: gear while it thinks, green check when it finishes, exclamation sign when it asks for permission, question sign when it asks you something. One caveat: Claude Code rewrites `settings.json` itself, for example when it records a newly allowed directory or permission, and a block added by hand while a session was running can be lost in that rewrite. If a hook you added has vanished, that is why; re-add it and it sticks.

Hook event names change over time — check the [hooks documentation](https://code.claude.com/docs/en/hooks) for your version.

## Calibrating auto-rotation

The static icons are redrawn at the angle of the gravity vector, low-pass filtered, so they turn smoothly with the cube (the gear is exempt: it spins anyway). How the BMI270 is mounted relative to the panel varies, so there are three constants in the orientation section of `main/main.cpp`:

- **Lying flat** — `kBootRotationOffset` (0–3, steps of 90° clockwise) is added to the display's default rotation at boot. It is also the frame everything is drawn in.
- **Tilted or standing** — the icon angle is `kImuAngleSign * atan2(ay, ax) + kImuAngleOffset`. If the icon turns the wrong way, flip the sign; if it is consistently off, adjust the offset (radians).

The committed values were verified on one AtomS3R in all four standing positions and under continuous tilt. To check yours, stand the cube on a side, send `status` over the serial port and compare `angle=` in the reply with what looks upright. The angle is held while the cube lies flat (`|az| > 0.80 g`) or the tilt is too small to be reliable (in-plane component below 0.40 g), so a cube resting on a desk never twitches.

## Project layout

```
claude-monitor/
├── CMakeLists.txt                  # ESP-IDF root (template, untouched)
├── sdkconfig.defaults              # target esp32s3, 8 MB flash (idf.py save-defconfig)
├── main/
│   ├── CMakeLists.txt              # registers main.cpp
│   ├── idf_component.yml           # m5stack/m5unified ^0.2
│   └── main.cpp                    # the whole firmware
├── Send-ClaudeState.ps1            # Windows hook helper
├── claude-code-status-monitor-atoms3r.md   # full article
├── CLAUDE.md                       # guidance for Claude Code working on this repo
├── LICENSE                         # CC BY 4.0
└── .devcontainer/                  # optional: espressif/idf Docker image
```

`.vscode/settings.json` is not committed: it holds machine-specific paths (ESP-IDF install, clangd, COM port) that the ESP-IDF extension writes when you pick the setup, target and port.

Everything is drawn into a 32 KB in-RAM canvas (`M5Canvas`) and pushed to the panel in one go, so the animations run without flicker. Icons are procedural — triangles, circles and round-capped strokes, no bitmaps — and the static ones are rotated vertex by vertex, which is why they can sit at any angle.

## Known limitations

- Each hook opens and closes the port. Works fine with USB Serial/JTAG, but a permanently-open daemon with a named pipe would be the bullet-proof version.
- `Notification` semantics vary between Claude Code versions; if you get a green check where you expected the exclamation sign, that's why.
- ESP-IDF logs and the protocol share the same USB port. Harmless as long as the PC only writes; the `STATUS` reply may arrive interleaved with log lines.
- Two Claude Code sessions at once both drive the same screen; the last event wins.

## License

Firmware, scripts and the article are released under the [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/) license (CC BY 4.0). See [LICENSE](LICENSE).

## References

- [ESP-IDF VS Code extension](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/)
- [AtomS3R docs](https://docs.m5stack.com/en/core/AtomS3R) · [store page](https://shop.m5stack.com/products/atoms3r-dev-kit)
- [M5Unified](https://github.com/m5stack/M5Unified) on the [Component Registry](https://components.espressif.com/components/m5stack/m5unified)
- [Claude Code hooks](https://code.claude.com/docs/en/hooks)
